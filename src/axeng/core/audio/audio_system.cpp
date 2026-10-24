#include "axeng/core/audio/audio_system.h"

#include "axeng/core/audio/music.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <numbers>
#include <random>

#include "miniaudio.h"
#include "spdlog/spdlog.h"

namespace
{
	constexpr std::array<std::string_view, static_cast<std::size_t>(ax::audio::Bus::Count)> s_bus_names
	{
		"master",
		"sfx",
		"ui",
		"music",
	};

	constexpr std::size_t s_max_voices{ 64 };
	constexpr std::uint32_t s_max_variants{ 32 };
	constexpr std::uint32_t s_chunk_frames{ 64 };
	constexpr float s_steal_fade{ 0.02f };
	constexpr float s_bus_ramp_seconds{ 0.03f };

	std::uint64_t mix_seed(std::uint64_t a, std::uint64_t b) noexcept
	{
		auto z{ a + 0x9E3779B97F4A7C15ull * (b + 1) };
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}

	struct Ramp
	{
		float value{ 0.0f };
		float target{ 0.0f };
		float step{ 0.0f };
		std::uint32_t remaining{ 0 };

		void set(float newTarget, std::uint32_t frames) noexcept
		{
			target = newTarget;
			if (frames == 0)
			{
				value = newTarget;
				remaining = 0;
				step = 0.0f;
				return;
			}
			remaining = frames;
			step = (target - value) / static_cast<float>(frames);
		}

		float tick() noexcept
		{
			if (remaining > 0)
			{
				value += step;
				if (--remaining == 0)
					value = target;
			}
			return value;
		}

		void advance(std::uint32_t frames) noexcept
		{
			if (frames >= remaining)
			{
				value = target;
				remaining = 0;
			}
			else
			{
				value += step * static_cast<float>(frames);
				remaining -= frames;
			}
		}
	};

	using SampleBuffer = std::shared_ptr<const std::vector<float>>;

	struct Voice
	{
		ax::audio::VoiceId id{};
		std::uint32_t soundId{};
		SampleBuffer buffer{};
		double position{ 0.0 };
		float pitch{ 1.0f };
		// The random pitch variation picked for this play; pitch is this times the caller's pitch.
		float pitchVariation{ 1.0f };
		// Gain is level * volume: level is the definition's volume with this play's random variation
		// applied (variation is kept so a live redefinition can rescale it), volume is the caller's.
		float level{ 1.0f };
		float variation{ 1.0f };
		float volume{ 1.0f };
		Ramp gain{};
		Ramp pan{};
		bool loop{ false };
		bool stopping{ false };
		ax::audio::Bus bus{ ax::audio::Bus::Sfx };
		// The previous buffer while crossfading to a live-redefined one.
		SampleBuffer fadeFrom{};
		double fadePosition{ 0.0 };
		std::uint32_t fadeRemaining{ 0 };
		std::uint32_t fadeLength{ 0 };
	};

	struct MusicStream
	{
		const ax::Music* music{ nullptr };
		ma_decoder decoder{};
		Ramp gain{};
		bool stopping{ false };
		bool finished{ false };
		bool decoderInitialised{ false };

		MusicStream() = default;
		MusicStream(const MusicStream&) = delete;
		MusicStream& operator=(const MusicStream&) = delete;

		~MusicStream()
		{
			if (decoderInitialised)
				ma_decoder_uninit(&decoder);
		}
	};

	struct SoundEntry
	{
		std::uint32_t id{};
		ax::audio::SoundDefinition definition{};
		std::vector<SampleBuffer> variants{};
		std::uint32_t lastVariant{ ~0u };
		std::chrono::steady_clock::time_point lastPlay{};
		bool played{ false };
	};

	float soft_clip(float x) noexcept
	{
		constexpr float knee{ 0.9f };
		const auto mag{ std::abs(x) };
		if (mag <= knee)
			return x;
		const auto shaped{ knee + (1.0f - knee) * std::tanh((mag - knee) / (1.0f - knee)) };
		return std::copysign(shaped, x);
	}

	std::uint32_t seconds_to_frames(float seconds, std::uint32_t rate) noexcept
	{
		if (!(seconds > 0.0f))
			return 0;
		return static_cast<std::uint32_t>(std::min(seconds, 600.0f) * static_cast<float>(rate));
	}

	float sanitise(float value, float lo, float hi, float fallback) noexcept
	{
		return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
	}

	// Linearly interpolated sample at a fractional position (which must be inside the buffer).
	float read_sample(const std::vector<float>& samples, double position, bool loop) noexcept
	{
		const auto size{ samples.size() };
		const auto i0{ std::min(static_cast<std::size_t>(position), size - 1) };
		const auto frac{ static_cast<float>(position - static_cast<double>(i0)) };
		const auto s0{ samples[i0] };
		const auto s1{ i0 + 1 < size ? samples[i0 + 1] : (loop ? samples[0] : 0.0f) };
		return s0 + (s1 - s0) * frac;
	}

	// Crossfade when a playing loop switches to a live-redefined buffer.
	constexpr float s_live_fade{ 0.03f };
}

struct ax::audio::AudioSystem::Impl
{
	explicit Impl(const Config& config)
		: rng{ static_cast<std::mt19937::result_type>(config.seed) }
		, musicRng{ static_cast<std::mt19937::result_type>(mix_seed(config.seed, 1)) }
	{
		busTargets.fill(1.0f);
		busGains.fill(1.0f);
		sampleRate = config.sampleRate == 0 ? 48000 : config.sampleRate;
		lastPump = std::chrono::steady_clock::now();

		if (!config.openDevice || config.manualMix)
		{
			virtualClock = !config.manualMix;
			return;
		}

		if (config.nullBackend)
		{
			const ma_backend backends[]{ ma_backend_null };
			if (ma_context_init(backends, 1, nullptr, &context) != MA_SUCCESS)
			{
				spdlog::warn("<Audio> Failed to create the null audio context, audio disabled");
				virtualClock = true;
				return;
			}
			contextOpen = true;
		}

		auto deviceConfig{ ma_device_config_init(ma_device_type_playback) };
		deviceConfig.playback.format = ma_format_f32;
		deviceConfig.playback.channels = 2;
		deviceConfig.sampleRate = config.sampleRate;
		deviceConfig.dataCallback = &Impl::data_callback;
		deviceConfig.pUserData = this;

		if (ma_device_init(contextOpen ? &context : nullptr, &deviceConfig, &device) != MA_SUCCESS)
		{
			spdlog::warn("<Audio> No audio output device available, audio disabled");
			virtualClock = true;
			return;
		}

		sampleRate = device.sampleRate;
		if (ma_device_start(&device) != MA_SUCCESS)
		{
			spdlog::warn("<Audio> Failed to start the audio output device, audio disabled");
			ma_device_uninit(&device);
			virtualClock = true;
			return;
		}

		deviceOpen = true;
		spdlog::info("<Audio> Opened '{}' at {} Hz", device.playback.name, sampleRate);
	}

	~Impl()
	{
		if (deviceOpen)
			ma_device_uninit(&device);
		if (contextOpen)
			ma_context_uninit(&context);
	}

	static void data_callback(ma_device* device, void* output, const void*, ma_uint32 frames)
	{
		static_cast<Impl*>(device->pUserData)->mix(static_cast<float*>(output), frames);
	}

	// Without a device, mixes the wall-clock time since the last call into a discarded buffer so voices
	// finish, stopped voices are cleaned up and playlists advance. Called on the main thread.
	void pump() noexcept
	{
		if (!virtualClock)
			return;
		const auto now{ std::chrono::steady_clock::now() };
		const auto elapsed{ std::chrono::duration<double>(now - lastPump).count() };
		lastPump = now;
		// Long stalls are clamped; sounds then just finish a little later than real time.
		auto frames{ static_cast<std::uint32_t>(std::min(elapsed, 0.25) * static_cast<double>(sampleRate)) };
		while (frames > 0)
		{
			const auto count{ std::min<std::uint32_t>(frames, static_cast<std::uint32_t>(pumpScratch.size() / 2)) };
			mix(pumpScratch.data(), count);
			frames -= count;
		}
	}

	bool virtualClock{ false };
	std::chrono::steady_clock::time_point lastPump{};
	std::array<float, 2048> pumpScratch{};

	// Everything below `mutex` is shared with the audio thread.
	std::mt19937 rng;
	std::map<std::string, SoundEntry> sounds{};
	std::uint32_t nextSoundId{ 1 };

	ma_context context{};
	ma_device device{};
	bool contextOpen{ false };
	bool deviceOpen{ false };
	std::uint32_t sampleRate{ 48000 };

	mutable std::mutex mutex{};
	std::vector<Voice> voices{};
	VoiceId nextVoiceId{ 1 };
	std::array<float, static_cast<std::size_t>(Bus::Count)> busTargets{};
	std::array<float, static_cast<std::size_t>(Bus::Count)> busGains{};

	std::vector<std::unique_ptr<MusicStream>> music{};
	std::vector<const Music*> playlist{};
	std::vector<std::size_t> playlistOrder{};
	std::size_t playlistPosition{ 0 };
	PlaylistOptions playlistOptions{};
	bool playlistActive{ false };
	std::uint64_t gapRemaining{ 0 };
	bool waitingForGap{ false };
	const Music* lastTrack{ nullptr };
	bool musicPaused{ false };
	std::mt19937 musicRng;
	std::array<float, s_chunk_frames * 2> musicScratch{};

	std::uint32_t frames_for(float seconds) const noexcept { return seconds_to_frames(seconds, sampleRate); }

	float bus_gain(Bus bus) const noexcept
	{
		return busGains[static_cast<std::size_t>(Bus::Master)] * busGains[static_cast<std::size_t>(bus)];
	}

	Voice* find_voice(VoiceId id) noexcept
	{
		const auto it{ std::ranges::find(voices, id, &Voice::id) };
		return it != voices.end() ? &*it : nullptr;
	}

	void stop_voice(Voice& voice, float fade) noexcept
	{
		voice.stopping = true;
		voice.gain.set(0.0f, std::max<std::uint32_t>(frames_for(fade), 32));
	}

	SampleBuffer render_variant(SoundEntry& entry, std::uint32_t index)
	{
		auto& slot{ entry.variants[index] };
		if (slot)
			return slot;

		const auto& def{ entry.definition };
		const auto params{ index == 0 ? def.params : mutate(def.params, def.mutation, mix_seed(def.seed, index)) };
		slot = std::make_shared<const std::vector<float>>(
			synthesize(params, SynthOptions{ .loop = def.loop, .seed = mix_seed(def.seed, index + 1000) }));
		return slot;
	}

	// Picks the order the playlist will be played in. Never repeats the last track straight away.
	void build_playlist_order()
	{
		playlistOrder.resize(playlist.size());
		for (std::size_t i{}; i < playlistOrder.size(); ++i)
			playlistOrder[i] = i;
		if (playlistOptions.shuffle && playlistOrder.size() > 1)
		{
			std::ranges::shuffle(playlistOrder, musicRng);
			if (playlist[playlistOrder.front()] == lastTrack)
			{
				std::uniform_int_distribution<std::size_t> dist{ 1, playlistOrder.size() - 1 };
				std::swap(playlistOrder.front(), playlistOrder[dist(musicRng)]);
			}
		}
		playlistPosition = 0;
	}

	const Music* next_playlist_track()
	{
		if (playlist.empty())
			return nullptr;
		if (playlistPosition >= playlistOrder.size())
		{
			if (!playlistOptions.repeat && !playlistOrder.empty())
				return nullptr;
			build_playlist_order();
		}
		return playlist[playlistOrder[playlistPosition++]];
	}

	bool start_stream(const Music& track, float fade)
	{
		auto stream{ std::make_unique<MusicStream>() };
		stream->music = &track;
		const auto config{ ma_decoder_config_init(ma_format_f32, 2, sampleRate) };
		if (ma_decoder_init_memory(track.data().data(), track.data().size(), &config, &stream->decoder) != MA_SUCCESS)
		{
			spdlog::error("<Audio> Failed to decode music '{}'", track.asset_name());
			return false;
		}
		stream->decoderInitialised = true;
		stream->gain.value = fade > 0.0f ? 0.0f : 1.0f;
		stream->gain.set(1.0f, frames_for(fade));
		for (auto& other : music)
		{
			if (!other->stopping)
			{
				other->stopping = true;
				other->gain.set(0.0f, std::max<std::uint32_t>(frames_for(fade), 64));
			}
		}
		music.push_back(std::move(stream));
		lastTrack = &track;
		waitingForGap = false;
		return true;
	}

	void advance_playlist(float fade)
	{
		for (std::size_t attempt{}; attempt < playlist.size(); ++attempt)
		{
			const auto* track{ next_playlist_track() };
			if (track == nullptr)
				break;
			if (start_stream(*track, fade))
				return;
		}
		playlistActive = false;
	}

	void mix_voices(float* out, std::uint32_t frames) noexcept
	{
		const auto rateScale{ static_cast<double>(sfx_sample_rate) / static_cast<double>(sampleRate) };
		for (auto& voice : voices)
		{
			const auto& samples{ *voice.buffer };
			const auto size{ samples.size() };
			if (size == 0)
			{
				voice.stopping = true;
				voice.gain.set(0.0f, 0);
				continue;
			}

			const auto busGain{ bus_gain(voice.bus) };
			const auto step{ rateScale * static_cast<double>(voice.pitch) };
			const auto angle{ (std::clamp(voice.pan.value, -1.0f, 1.0f) + 1.0f) * std::numbers::pi_v<float> * 0.25f };
			const auto left{ std::cos(angle) * std::numbers::sqrt2_v<float> * busGain };
			const auto right{ std::sin(angle) * std::numbers::sqrt2_v<float> * busGain };
			voice.pan.advance(frames);

			for (std::uint32_t f{}; f < frames; ++f)
			{
				if (voice.position >= static_cast<double>(size))
				{
					if (!voice.loop)
					{
						voice.stopping = true;
						voice.gain.set(0.0f, 0);
						break;
					}
					voice.position = std::fmod(voice.position, static_cast<double>(size));
				}

				auto sample{ read_sample(samples, voice.position, voice.loop) };
				if (voice.fadeRemaining > 0)
				{
					const auto& old{ *voice.fadeFrom };
					if (!old.empty())
					{
						voice.fadePosition = std::fmod(voice.fadePosition, static_cast<double>(old.size()));
						const auto t{ static_cast<float>(voice.fadeRemaining) / static_cast<float>(voice.fadeLength) };
						sample = sample * (1.0f - t) + read_sample(old, voice.fadePosition, true) * t;
						voice.fadePosition += step;
					}
					if (--voice.fadeRemaining == 0)
						voice.fadeFrom.reset();
				}
				sample *= voice.gain.tick();
				out[f * 2] += sample * left;
				out[f * 2 + 1] += sample * right;
				voice.position += step;
			}
		}

		std::erase_if(voices, [](const Voice& v) { return v.stopping && v.gain.remaining == 0 && v.gain.value <= 0.0f; });
	}

	void mix_music(float* out, std::uint32_t frames) noexcept
	{
		if (musicPaused)
			return;

		const auto busGain{ bus_gain(Bus::Music) };
		bool currentFinished{ false };
		for (auto& stream : music)
		{
			ma_uint64 read{};
			ma_decoder_read_pcm_frames(&stream->decoder, musicScratch.data(), frames, &read);
			for (ma_uint64 f{}; f < read; ++f)
			{
				const auto gain{ stream->gain.tick() * busGain };
				out[f * 2] += musicScratch[f * 2] * gain;
				out[f * 2 + 1] += musicScratch[f * 2 + 1] * gain;
			}
			if (read < frames)
			{
				stream->finished = true;
				if (!stream->stopping)
					currentFinished = true;
			}
		}

		std::erase_if(music, [](const std::unique_ptr<MusicStream>& s)
		{
			return s->finished || (s->stopping && s->gain.remaining == 0 && s->gain.value <= 0.0f);
		});

		if (currentFinished && playlistActive)
		{
			waitingForGap = true;
			gapRemaining = frames_for(playlistOptions.gap);
		}

		if (waitingForGap)
		{
			if (gapRemaining > frames)
				gapRemaining -= frames;
			else
				advance_playlist(0.0f);
		}
	}

	void mix(float* output, std::uint32_t frames) noexcept
	{
		std::fill_n(output, static_cast<std::size_t>(frames) * 2, 0.0f);

		std::lock_guard lock{ mutex };
		std::uint32_t done{};
		while (done < frames)
		{
			const auto count{ std::min(s_chunk_frames, frames - done) };
			auto* const out{ output + static_cast<std::size_t>(done) * 2 };

			// Bus volumes glide to avoid zipper noise when a slider is dragged.
			const auto maxStep{ static_cast<float>(count) / (s_bus_ramp_seconds * static_cast<float>(sampleRate)) };
			for (std::size_t b{}; b < busGains.size(); ++b)
				busGains[b] += std::clamp(busTargets[b] - busGains[b], -maxStep, maxStep);

			mix_voices(out, count);
			mix_music(out, count);
			for (std::uint32_t i{}; i < count * 2; ++i)
				out[i] = soft_clip(out[i]);
			done += count;
		}
	}
};

std::string_view ax::audio::bus_name(Bus bus) noexcept
{
	const auto index{ static_cast<std::size_t>(bus) };
	return index < s_bus_names.size() ? s_bus_names[index] : std::string_view{};
}

std::optional<ax::audio::Bus> ax::audio::bus_from_name(std::string_view name) noexcept
{
	for (std::size_t i{}; i < s_bus_names.size(); ++i)
	{
		if (s_bus_names[i] == name)
			return static_cast<Bus>(i);
	}
	return std::nullopt;
}

ax::audio::AudioSystem::Config ax::audio::AudioSystem::default_config()
{
	Config config{};
	config.seed = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	return config;
}

ax::audio::AudioSystem::AudioSystem(const Config& config)
	: m_impl{ std::make_unique<Impl>(config) }
{
}

ax::audio::AudioSystem::~AudioSystem() = default;

bool ax::audio::AudioSystem::device_open() const noexcept
{
	return m_impl->deviceOpen;
}

std::uint32_t ax::audio::AudioSystem::sample_rate() const noexcept
{
	return m_impl->sampleRate;
}

bool ax::audio::AudioSystem::define_sound(const std::string& name, const SoundDefinition& definition, bool live)
{
	if (name.empty())
		return false;

	auto def{ definition };
	clamp_params(def.params);
	def.variants = std::clamp<std::uint32_t>(def.variants, 1, s_max_variants);
	def.mutation = sanitise(def.mutation, 0.0f, 1.0f, 0.0f);
	def.pitchVariation = sanitise(def.pitchVariation, 0.0f, 24.0f, 0.0f);
	def.volume = sanitise(def.volume, 0.0f, 4.0f, 1.0f);
	def.volumeVariation = sanitise(def.volumeVariation, 0.0f, 1.0f, 0.0f);
	def.maxInstances = std::max<std::uint32_t>(def.maxInstances, 1);
	def.cooldown = sanitise(def.cooldown, 0.0f, 60.0f, 0.0f);
	if (def.bus == Bus::Master || def.bus >= Bus::Count)
		def.bus = Bus::Sfx;

	auto [it, inserted]{ m_impl->sounds.try_emplace(name) };
	auto& entry{ it->second };
	if (inserted)
		entry.id = m_impl->nextSoundId++;
	entry.definition = def;
	entry.variants.assign(def.variants, nullptr);
	entry.lastVariant = ~0u;

	if (live && !inserted)
	{
		// Playing loops switch to the new sound where they are, with a short crossfade. One-shots finish
		// as they were.
		const auto buffer{ m_impl->render_variant(entry, 0) };
		std::lock_guard lock{ m_impl->mutex };
		for (auto& voice : m_impl->voices)
		{
			if (voice.stopping || voice.soundId != entry.id)
				continue;
			if (voice.loop && def.loop && !buffer->empty())
			{
				voice.fadeFrom = std::move(voice.buffer);
				voice.fadePosition = voice.position;
				voice.fadeLength = std::max<std::uint32_t>(m_impl->frames_for(s_live_fade), 1);
				voice.fadeRemaining = voice.fadeLength;
				voice.buffer = buffer;
				voice.position = std::fmod(voice.position, static_cast<double>(buffer->size()));
			}
			else
			{
				voice.loop = false;
			}
			voice.level = def.volume * voice.variation;
			voice.gain.set(voice.level * voice.volume, m_impl->frames_for(s_live_fade));
			voice.bus = def.bus;
		}
	}
	return true;
}

bool ax::audio::AudioSystem::has_sound(const std::string& name) const
{
	return m_impl->sounds.contains(name);
}

const ax::audio::SoundDefinition* ax::audio::AudioSystem::sound_definition(const std::string& name) const
{
	const auto it{ m_impl->sounds.find(name) };
	return it != m_impl->sounds.end() ? &it->second.definition : nullptr;
}

std::optional<ax::audio::SoundInfo> ax::audio::AudioSystem::sound_info(const std::string& name) const
{
	const auto it{ m_impl->sounds.find(name) };
	if (it == m_impl->sounds.end())
		return std::nullopt;

	auto& entry{ it->second };
	const auto first{ m_impl->render_variant(entry, 0) };
	SoundInfo info{};
	info.variants = static_cast<std::uint32_t>(entry.variants.size());
	info.renderedVariants = static_cast<std::uint32_t>(std::ranges::count_if(entry.variants, [](const auto& v) { return v != nullptr; }));
	info.length = static_cast<float>(first->size()) / static_cast<float>(sfx_sample_rate);
	double sumSquares{};
	for (const auto sample : *first)
	{
		info.peak = std::max(info.peak, std::abs(sample));
		sumSquares += static_cast<double>(sample) * sample;
	}
	if (!first->empty())
		info.rms = static_cast<float>(std::sqrt(sumSquares / static_cast<double>(first->size())));
	info.loop = entry.definition.loop;
	info.bus = entry.definition.bus;
	return info;
}

std::vector<std::string> ax::audio::AudioSystem::sound_names() const
{
	std::vector<std::string> names{};
	names.reserve(m_impl->sounds.size());
	for (const auto& [name, _] : m_impl->sounds)
		names.push_back(name);
	return names;
}

bool ax::audio::AudioSystem::prepare_sound(const std::string& name)
{
	const auto it{ m_impl->sounds.find(name) };
	if (it == m_impl->sounds.end())
		return false;
	for (std::uint32_t i{}; i < it->second.variants.size(); ++i)
		m_impl->render_variant(it->second, i);
	return true;
}

std::vector<std::pair<float, float>> ax::audio::AudioSystem::sound_waveform(const std::string& name, std::uint32_t buckets)
{
	const auto it{ m_impl->sounds.find(name) };
	if (it == m_impl->sounds.end() || buckets == 0)
		return {};

	const auto samples{ m_impl->render_variant(it->second, 0) };
	buckets = std::min<std::uint32_t>(buckets, 4096);
	std::vector<std::pair<float, float>> result(buckets, { 0.0f, 0.0f });
	const auto size{ samples->size() };
	if (size == 0)
		return result;
	for (std::uint32_t b{}; b < buckets; ++b)
	{
		const auto begin{ static_cast<std::size_t>(static_cast<std::uint64_t>(b) * size / buckets) };
		const auto end{ std::max(begin + 1, static_cast<std::size_t>(static_cast<std::uint64_t>(b + 1) * size / buckets)) };
		auto lo{ 0.0f };
		auto hi{ 0.0f };
		for (auto i{ begin }; i < end && i < size; ++i)
		{
			lo = std::min(lo, (*samples)[i]);
			hi = std::max(hi, (*samples)[i]);
		}
		result[b] = { lo, hi };
	}
	return result;
}

ax::audio::VoiceId ax::audio::AudioSystem::play(const std::string& name, const PlayOptions& options)
{
	m_impl->pump();
	auto& impl{ *m_impl };
	const auto it{ impl.sounds.find(name) };
	if (it == impl.sounds.end())
		return 0;

	auto& entry{ it->second };
	const auto& def{ entry.definition };
	const auto now{ std::chrono::steady_clock::now() };
	if (entry.played && def.cooldown > 0.0f && std::chrono::duration<float>(now - entry.lastPlay).count() < def.cooldown)
		return 0;

	auto variant{ 0u };
	const auto variantCount{ static_cast<std::uint32_t>(entry.variants.size()) };
	if (variantCount > 1)
	{
		std::uniform_int_distribution<std::uint32_t> dist{ 0, variantCount - 1 };
		variant = dist(impl.rng);
		if (variant == entry.lastVariant)
			variant = (variant + 1 + dist(impl.rng) % (variantCount - 1)) % variantCount;
	}
	auto buffer{ impl.render_variant(entry, variant) };
	if (buffer->empty())
		return 0;

	entry.lastVariant = variant;
	entry.lastPlay = now;
	entry.played = true;

	std::uniform_real_distribution<float> unit{ -1.0f, 1.0f };
	const auto semitones{ def.pitchVariation * unit(impl.rng) };
	const auto pitchVariation{ std::exp2(semitones / 12.0f) };
	const auto pitch{ sanitise(options.pitch, 0.01f, 16.0f, 1.0f) * pitchVariation };
	const auto variation{ 1.0f + def.volumeVariation * unit(impl.rng) };
	const auto playVolume{ sanitise(options.volume, 0.0f, 4.0f, 1.0f) };
	const auto volume{ def.volume * variation * playVolume };

	std::lock_guard lock{ impl.mutex };

	// Enforce the per-sound and global voice limits by fading out the oldest voices.
	std::size_t instances{};
	std::size_t active{};
	for (const auto& voice : impl.voices)
	{
		if (voice.stopping)
			continue;
		++active;
		if (voice.soundId == entry.id)
			++instances;
	}
	for (auto& voice : impl.voices)
	{
		if (instances < def.maxInstances)
			break;
		if (!voice.stopping && voice.soundId == entry.id)
		{
			impl.stop_voice(voice, s_steal_fade);
			--instances;
			--active;
		}
	}
	if (active >= s_max_voices)
	{
		auto victim{ std::ranges::find_if(impl.voices, [](const Voice& v) { return !v.stopping && !v.loop; }) };
		if (victim == impl.voices.end())
			victim = std::ranges::find_if(impl.voices, [](const Voice& v) { return !v.stopping; });
		if (victim != impl.voices.end())
			impl.stop_voice(*victim, s_steal_fade);
	}

	Voice voice{};
	voice.id = impl.nextVoiceId++;
	voice.soundId = entry.id;
	voice.buffer = std::move(buffer);
	voice.pitch = pitch;
	voice.pitchVariation = pitchVariation;
	voice.variation = variation;
	voice.level = def.volume * variation;
	voice.volume = playVolume;
	voice.loop = options.loop.value_or(def.loop);
	voice.bus = def.bus;
	voice.pan.set(sanitise(options.pan, -1.0f, 1.0f, 0.0f), 0);
	if (options.fadeIn > 0.0f)
		voice.gain.set(volume, impl.frames_for(options.fadeIn));
	else
		voice.gain.set(volume, 0);
	impl.voices.push_back(std::move(voice));
	return impl.voices.back().id;
}

bool ax::audio::AudioSystem::update_voice(VoiceId id, const VoiceUpdate& update)
{
	m_impl->pump();
	std::lock_guard lock{ m_impl->mutex };
	auto* voice{ m_impl->find_voice(id) };
	if (voice == nullptr || voice->stopping)
		return false;

	const auto frames{ m_impl->frames_for(update.ramp) };
	// Volume and pitch mean the same as in play(): they scale the sound's own (varied) level and pitch.
	if (update.volume)
	{
		voice->volume = sanitise(*update.volume, 0.0f, 4.0f, voice->volume);
		voice->gain.set(voice->level * voice->volume, frames);
	}
	if (update.pan)
		voice->pan.set(sanitise(*update.pan, -1.0f, 1.0f, voice->pan.target), frames);
	if (update.pitch)
		voice->pitch = voice->pitchVariation * sanitise(*update.pitch, 0.01f, 16.0f, 1.0f);
	return true;
}

void ax::audio::AudioSystem::stop(VoiceId id, float fade)
{
	m_impl->pump();
	std::lock_guard lock{ m_impl->mutex };
	if (auto* voice{ m_impl->find_voice(id) }; voice != nullptr && !voice->stopping)
		m_impl->stop_voice(*voice, fade);
}

void ax::audio::AudioSystem::stop_sound(const std::string& name, float fade)
{
	m_impl->pump();
	const auto it{ m_impl->sounds.find(name) };
	if (it == m_impl->sounds.end())
		return;

	std::lock_guard lock{ m_impl->mutex };
	for (auto& voice : m_impl->voices)
	{
		if (!voice.stopping && voice.soundId == it->second.id)
			m_impl->stop_voice(voice, fade);
	}
}

void ax::audio::AudioSystem::stop_all(float fade)
{
	m_impl->pump();
	std::lock_guard lock{ m_impl->mutex };
	for (auto& voice : m_impl->voices)
	{
		if (!voice.stopping)
			m_impl->stop_voice(voice, fade);
	}
}

bool ax::audio::AudioSystem::is_playing(VoiceId id) const
{
	m_impl->pump();
	std::lock_guard lock{ m_impl->mutex };
	const auto* voice{ m_impl->find_voice(id) };
	return voice != nullptr && !voice->stopping;
}

std::size_t ax::audio::AudioSystem::voice_count() const
{
	m_impl->pump();
	std::lock_guard lock{ m_impl->mutex };
	return static_cast<std::size_t>(std::ranges::count_if(m_impl->voices, [](const Voice& v) { return !v.stopping; }));
}

void ax::audio::AudioSystem::set_bus_volume(Bus bus, float volume)
{
	const auto index{ static_cast<std::size_t>(bus) };
	if (index >= m_impl->busTargets.size())
		return;
	std::lock_guard lock{ m_impl->mutex };
	m_impl->busTargets[index] = sanitise(volume, 0.0f, 4.0f, m_impl->busTargets[index]);
}

float ax::audio::AudioSystem::bus_volume(Bus bus) const
{
	const auto index{ static_cast<std::size_t>(bus) };
	if (index >= m_impl->busTargets.size())
		return 0.0f;
	std::lock_guard lock{ m_impl->mutex };
	return m_impl->busTargets[index];
}

bool ax::audio::AudioSystem::play_music(const Music& music, float fade)
{
	m_impl->pump();
	std::lock_guard lock{ m_impl->mutex };
	m_impl->playlistActive = false;
	m_impl->playlist.clear();
	m_impl->waitingForGap = false;
	return m_impl->start_stream(music, fade);
}

bool ax::audio::AudioSystem::play_playlist(std::vector<const Music*> tracks, const PlaylistOptions& options)
{
	m_impl->pump();
	std::erase(tracks, nullptr);
	if (tracks.empty())
		return false;

	std::lock_guard lock{ m_impl->mutex };
	m_impl->playlist = std::move(tracks);
	m_impl->playlistOptions = options;
	m_impl->playlistOptions.fade = sanitise(options.fade, 0.0f, 60.0f, 1.0f);
	m_impl->playlistOptions.gap = sanitise(options.gap, 0.0f, 600.0f, 2.0f);
	m_impl->playlistActive = true;
	m_impl->build_playlist_order();
	m_impl->advance_playlist(m_impl->playlistOptions.fade);
	return m_impl->playlistActive;
}

bool ax::audio::AudioSystem::next_track(float fade)
{
	m_impl->pump();
	std::lock_guard lock{ m_impl->mutex };
	if (!m_impl->playlistActive)
		return false;
	m_impl->advance_playlist(fade);
	return m_impl->playlistActive;
}

void ax::audio::AudioSystem::stop_music(float fade)
{
	m_impl->pump();
	std::lock_guard lock{ m_impl->mutex };
	m_impl->playlistActive = false;
	m_impl->waitingForGap = false;
	for (auto& stream : m_impl->music)
	{
		stream->stopping = true;
		stream->gain.set(0.0f, std::max<std::uint32_t>(m_impl->frames_for(fade), 64));
	}
}

void ax::audio::AudioSystem::set_music_paused(bool paused)
{
	std::lock_guard lock{ m_impl->mutex };
	m_impl->musicPaused = paused;
}

bool ax::audio::AudioSystem::music_paused() const
{
	std::lock_guard lock{ m_impl->mutex };
	return m_impl->musicPaused;
}

std::string ax::audio::AudioSystem::current_music() const
{
	m_impl->pump();
	std::lock_guard lock{ m_impl->mutex };
	for (auto it{ m_impl->music.rbegin() }; it != m_impl->music.rend(); ++it)
	{
		if (!(*it)->stopping && !(*it)->finished)
			return (*it)->music->asset_name();
	}
	return {};
}

bool ax::audio::AudioSystem::playlist_active() const
{
	m_impl->pump();
	std::lock_guard lock{ m_impl->mutex };
	return m_impl->playlistActive;
}

void ax::audio::AudioSystem::mix(float* output, std::uint32_t frames) noexcept
{
	m_impl->mix(output, frames);
}
