#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "axeng/core/audio/sfx_synth.h"
#include "axeng/core/helpers.h"

namespace ax
{
	class Music;
}

namespace ax::audio
{
	enum class Bus : std::uint8_t
	{
		Master,
		Sfx,
		Ui,
		Music,
		Count,
	};

	std::string_view bus_name(Bus bus) noexcept;
	std::optional<Bus> bus_from_name(std::string_view name) noexcept;

	// 0 is never a valid voice.
	using VoiceId = std::uint64_t;

	struct SoundDefinition
	{
		SfxParams params{};
		std::uint64_t seed{ 0 };
		// How far each variant's parameters may drift from `params` (see audio::mutate).
		float mutation{ 0.0f };
		// Number of pre-rendered variants to choose between. Variant 0 always uses `params` unchanged.
		std::uint32_t variants{ 1 };
		// Random playback pitch offset per play, in +/- semitones.
		float pitchVariation{ 0.0f };
		float volume{ 1.0f };
		// Random playback volume offset per play, as a +/- fraction of volume.
		float volumeVariation{ 0.0f };
		// When exceeded, the oldest instance of this sound is faded out.
		std::uint32_t maxInstances{ 8 };
		// Minimum seconds between plays of this sound. Plays during the cooldown are ignored.
		float cooldown{ 0.0f };
		bool loop{ false };
		Bus bus{ Bus::Sfx };
	};

	struct PlayOptions
	{
		float volume{ 1.0f };
		// Playback rate multiplier, applied on top of the definition's pitch variation.
		float pitch{ 1.0f };
		// -1 = left, 1 = right.
		float pan{ 0.0f };
		std::optional<bool> loop{};
		float fadeIn{ 0.0f };
	};

	struct VoiceUpdate
	{
		std::optional<float> volume{};
		std::optional<float> pitch{};
		std::optional<float> pan{};
		// Seconds to glide to the new volume/pan.
		float ramp{ 0.05f };
	};

	struct SoundInfo
	{
		std::uint32_t variants{};
		std::uint32_t renderedVariants{};
		// Length of variant 0 in seconds.
		float length{};
		// Peak and RMS level of variant 0.
		float peak{};
		float rms{};
		bool loop{};
		Bus bus{};
	};

	struct PlaylistOptions
	{
		bool shuffle{ true };
		// Start again (reshuffled) after the last track instead of stopping.
		bool repeat{ true };
		// Crossfade used when starting the playlist or skipping tracks.
		float fade{ 1.0f };
		// Silence between tracks that end naturally.
		float gap{ 2.0f };
	};

	class AudioSystem
	{
	public:
		DISABLE_COPY_AND_MOVE(AudioSystem);

		struct Config
		{
			// When false no output device is opened. Time still passes: API calls mix the elapsed time into a
			// discarded buffer, so sounds finish and playlists advance as if a device were playing them.
			bool openDevice{ true };
			// Only mix when mix() is called explicitly (used by tests to control time). Implies no device.
			bool manualMix{ false };
			// 0 uses the device's native rate (or 48000 without a device).
			std::uint32_t sampleRate{ 0 };
			// Uses miniaudio's null backend: a real audio thread that discards its output.
			bool nullBackend{ false };
			std::uint64_t seed{ 0 };
		};

		// Reads AXENG_AUDIO_BACKEND: "none" disables the device, "null" uses the null backend.
		static Config config_from_environment();

		explicit AudioSystem(const Config& config);
		~AudioSystem();

		bool device_open() const noexcept;
		std::uint32_t sample_rate() const noexcept;

		// Sound bank. These functions must be called from a single thread (the Lua main thread).
		// With `live`, voices already playing a redefined looping sound switch to the new sound in place.
		bool define_sound(const std::string& name, const SoundDefinition& definition, bool live = false);
		bool has_sound(const std::string& name) const;
		const SoundDefinition* sound_definition(const std::string& name) const;
		std::optional<SoundInfo> sound_info(const std::string& name) const;
		std::vector<std::string> sound_names() const;
		// Renders every variant now instead of on first use.
		bool prepare_sound(const std::string& name);
		// Min/max sample of variant 0 in `buckets` equal slices, for waveform previews. Empty if unknown.
		std::vector<std::pair<float, float>> sound_waveform(const std::string& name, std::uint32_t buckets);

		// Returns 0 if the sound doesn't exist, is cooling down, or has no audio.
		VoiceId play(const std::string& name, const PlayOptions& options = {});
		bool update_voice(VoiceId id, const VoiceUpdate& update);
		void stop(VoiceId id, float fade = 0.05f);
		void stop_sound(const std::string& name, float fade = 0.05f);
		void stop_all(float fade = 0.05f);
		bool is_playing(VoiceId id) const;
		std::size_t voice_count() const;

		void set_bus_volume(Bus bus, float volume);
		float bus_volume(Bus bus) const;

		// Music. The Music assets must outlive the audio system.
		bool play_music(const Music& music, float fade = 1.0f);
		bool play_playlist(std::vector<const Music*> tracks, const PlaylistOptions& options = {});
		bool next_track(float fade = 1.0f);
		void stop_music(float fade = 1.0f);
		void set_music_paused(bool paused);
		bool music_paused() const;
		// Name of the track currently playing, or empty if none (including between tracks).
		std::string current_music() const;
		bool playlist_active() const;

		// Mixes interleaved stereo float frames. Called by the device thread, or manually when no device is open.
		void mix(float* output, std::uint32_t frames) noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
