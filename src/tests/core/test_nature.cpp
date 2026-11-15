#include <gtest/gtest.h>

#include "axeng/core/audio/audio_system.h"
#include "axeng/core/audio/nature.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
	using namespace ax::audio;

	float peak_of(const std::vector<float>& samples)
	{
		float result{};
		for (const auto s : samples) result = std::max(result, std::abs(s));
		return result;
	}

	float rms_of(const std::vector<float>& samples, std::size_t from = 0, std::size_t to = SIZE_MAX)
	{
		to = std::min(to, samples.size());
		double sum{};
		for (auto i{ from }; i < to; ++i) sum += samples[i] * samples[i];
		return to > from ? static_cast<float>(std::sqrt(sum / static_cast<double>(to - from))) : 0.0f;
	}

	// Zero crossings per sample over the louder samples: a cheap brightness and pitch measure.
	float crossing_rate(const std::vector<float>& samples)
	{
		const auto gate{ peak_of(samples) * 0.05f };
		std::size_t crossings{}, counted{};
		for (std::size_t i{ 1 }; i < samples.size(); ++i)
		{
			if (std::abs(samples[i]) < gate && std::abs(samples[i - 1]) < gate) continue;
			++counted;
			crossings += (samples[i - 1] < 0.0f) != (samples[i] < 0.0f);
		}
		return counted ? static_cast<float>(crossings) / static_cast<float>(counted) : 0.0f;
	}

	std::vector<float> render(NatureKind kind, std::uint64_t seed = 1, bool loop = false, std::uint8_t species = 0)
	{
		return render_nature({ .kind = kind, .species = species }, { .loop = loop, .seed = seed });
	}
}

TEST(Nature, EveryKindIsBoundedAudibleAndItsOwnLength)
{
	for (std::size_t index{}; index < static_cast<std::size_t>(NatureKind::count); ++index)
	{
		const auto kind{ static_cast<NatureKind>(index) };
		const auto name{ nature_kind_name(kind) };
		EXPECT_EQ(nature_kind_from_name(name), kind);
		const auto samples{ render(kind, 3, nature_kind_is_bed(kind)) };
		const auto expected{ nature_default_length(kind) * static_cast<float>(sfx_sample_rate) };
		EXPECT_NEAR(static_cast<float>(samples.size()), expected, expected * 0.05f) << name;
		const auto peak{ peak_of(samples) };
		EXPECT_LE(peak, 1.0f) << name;
		EXPECT_NEAR(peak, 0.8f, 0.05f) << name;
		EXPECT_GT(rms_of(samples), 0.01f) << name;
		for (const auto s : samples) ASSERT_TRUE(std::isfinite(s)) << name;
	}
	EXPECT_FALSE(nature_kind_from_name("thunder"));
}

TEST(Nature, EachSeedIsADifferentTakeAndTheSameSeedTheSame)
{
	for (const auto kind : { NatureKind::surf_wave, NatureKind::bird_phrase, NatureKind::scuttle, NatureKind::gull })
	{
		EXPECT_EQ(render(kind, 5), render(kind, 5)) << nature_kind_name(kind);
		EXPECT_NE(render(kind, 5), render(kind, 6)) << nature_kind_name(kind);
	}
}

TEST(Nature, BedsLoopWithoutASeam)
{
	for (const auto kind : { NatureKind::swell, NatureKind::wind, NatureKind::crickets })
	{
		ASSERT_TRUE(nature_kind_is_bed(kind));
		const auto samples{ render(kind, 2, true) };
		EXPECT_LE(loop_seam(samples), 1.01f) << nature_kind_name(kind);
		// Its level at the wrap matches its level just inside: no jump in the modulation (crickets come and go by
		// design, so only the continuous beds).
		if (kind == NatureKind::crickets) continue;
		const auto edge{ static_cast<std::size_t>(sfx_sample_rate / 4) };
		const auto end{ rms_of(samples, samples.size() - edge) }, start{ rms_of(samples, 0, edge) };
		EXPECT_NEAR(end, start, std::max(end, start) * 0.6f) << nature_kind_name(kind);
	}
}

TEST(Nature, KindsSoundAsDesigned)
{
	// The deep swell is far darker than the wind, and the wind than the leaves.
	const auto swell{ crossing_rate(render(NatureKind::swell, 1, true)) };
	const auto wind{ crossing_rate(render(NatureKind::wind, 1, true)) };
	const auto rustle{ crossing_rate(render(NatureKind::rustle)) };
	EXPECT_LT(swell, wind * 0.5f) << swell << " " << wind;
	EXPECT_LT(wind, rustle * 0.5f) << wind << " " << rustle;
	// Crickets sing near 4.5 kHz: two crossings a cycle.
	const auto crickets{ crossing_rate(render(NatureKind::crickets, 1, true)) };
	EXPECT_NEAR(crickets * static_cast<float>(sfx_sample_rate) / 2.0f, 4500.0f, 500.0f);
	// A dove coos low, a chirper chips high.
	const auto dove{ crossing_rate(render(NatureKind::bird_phrase, 1, false, 3)) };
	const auto chirper{ crossing_rate(render(NatureKind::bird_phrase, 1, false, 2)) };
	EXPECT_LT(dove * 4.0f, chirper);
	// A hull groans lower than a rope creaks.
	EXPECT_LT(crossing_rate(render(NatureKind::creak)), crossing_rate(render(NatureKind::rope)));
	// A scuttle is clicks: mostly quiet between them.
	const auto scuttle{ render(NatureKind::scuttle) };
	const auto loud{ std::ranges::count_if(scuttle, [&](float s) { return std::abs(s) > 0.1f * peak_of(scuttle); }) };
	EXPECT_LT(static_cast<float>(loud) / static_cast<float>(scuttle.size()), 0.25f);
	// A wave swells, crashes and washes back: loudest in its first half, fading at its end.
	const auto wave{ render(NatureKind::surf_wave, 4) };
	const auto quarter{ wave.size() / 4 };
	EXPECT_GT(rms_of(wave, quarter / 2, quarter * 2), rms_of(wave, quarter * 3) * 2.0f);
}

TEST(Nature, ParametersShapeTheSound)
{
	// Brighter and higher.
	const auto plain{ crossing_rate(render_nature({ .kind = NatureKind::wind }, { .loop = true, .seed = 1 })) };
	const auto bright{ crossing_rate(render_nature({ .kind = NatureKind::wind, .brightness = 2.5f }, { .loop = true, .seed = 1 })) };
	EXPECT_GT(bright, plain * 1.3f);
	// A length of its own, and the volume is the peak.
	const auto shorter{ render_nature({ .kind = NatureKind::gull, .length = 0.6f, .volume = 0.4f }, { .seed = 1 }) };
	EXPECT_NEAR(static_cast<float>(shorter.size()), 0.6f * static_cast<float>(sfx_sample_rate), 10.0f);
	EXPECT_NEAR(peak_of(shorter), 0.4f, 0.02f);
	// Clamping and mutation stay in range.
	NatureParams wild{ .kind = static_cast<NatureKind>(99), .length = 99.0f, .pitch = -3.0f, .density = 1e9f, .species = 7 };
	clamp_nature(wild);
	EXPECT_EQ(wild.kind, NatureKind::swell);
	EXPECT_LE(wild.length, 8.0f);
	EXPECT_GE(wild.pitch, 0.25f);
	EXPECT_LE(wild.density, 4.0f);
	EXPECT_LT(wild.species, 4);
	const auto mutated{ mutate_nature({ .kind = NatureKind::bird_phrase }, 1.0f, 9) };
	EXPECT_EQ(mutated.kind, NatureKind::bird_phrase);
	EXPECT_NE(mutated.pitch, 1.0f);
	EXPECT_EQ(mutate_nature({ .kind = NatureKind::wind }, 0.0f, 9).pitch, 1.0f);
}

TEST(Nature, TheSoundBankPlaysThemOnTheAmbienceBus)
{
	EXPECT_EQ(bus_from_name("ambience"), Bus::Ambience);
	EXPECT_EQ(bus_name(Bus::Ambience), "ambience");
	AudioSystem audio{ AudioSystem::Config{ .openDevice = false, .manualMix = true, .sampleRate = 48000, .seed = 1 } };
	ASSERT_TRUE(audio.define_sound("surf", { .nature = NatureParams{ .kind = NatureKind::surf_wave }, .variants = 3,
		.bus = Bus::Ambience }));
	ASSERT_TRUE(audio.define_sound("wind", { .nature = NatureParams{ .kind = NatureKind::wind }, .loop = true,
		.bus = Bus::Ambience }));
	const auto surf{ audio.sound_info("surf") };
	ASSERT_TRUE(surf);
	EXPECT_TRUE(surf->nature);
	EXPECT_EQ(surf->bus, Bus::Ambience);
	EXPECT_NEAR(surf->length, 6.0f, 0.3f);
	const auto wind{ audio.sound_info("wind") };
	ASSERT_TRUE(wind);
	EXPECT_TRUE(wind->loop);
	EXPECT_LE(wind->seam, 1.01f);
	EXPECT_NE(audio.sound_render_key("surf"), audio.sound_render_key("wind"));
	audio.set_bus_volume(Bus::Ambience, 0.5f);
	EXPECT_NEAR(audio.bus_volume(Bus::Ambience), 0.5f, 1e-6f);
	EXPECT_NE(audio.play("surf"), 0u);
}

TEST(Nature, DISABLED_WriteWavs)
{
	// Writes three takes of every kind (and each bird species) as 16-bit WAV files for listening, to
	// AXENG_NATURE_WAV_DIR or the working directory.
	char* folder{};
	std::size_t folderLength{};
	_dupenv_s(&folder, &folderLength, "AXENG_NATURE_WAV_DIR");
	const std::string base{ folder ? std::string{ folder } + "/" : std::string{} };
	std::free(folder);
	const auto write{ [&](const std::string& name, const std::vector<float>& samples)
	{
		std::ofstream file{ base + name + ".wav", std::ios::binary };
		ASSERT_TRUE(file.good());
		const auto put{ [&](const void* data, std::size_t bytes)
		{ file.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes)); } };
		const std::uint32_t rate{ sfx_sample_rate }, dataBytes{ static_cast<std::uint32_t>(samples.size() * 2) };
		const std::uint32_t riff{ 36 + dataBytes }, fmtBytes{ 16 }, byteRate{ rate * 2 };
		const std::uint16_t pcm{ 1 }, mono{ 1 }, align{ 2 }, bits{ 16 };
		put("RIFF", 4); put(&riff, 4); put("WAVEfmt ", 8); put(&fmtBytes, 4); put(&pcm, 2); put(&mono, 2);
		put(&rate, 4); put(&byteRate, 4); put(&align, 2); put(&bits, 2); put("data", 4); put(&dataBytes, 4);
		for (const auto s : samples)
		{
			const auto value{ static_cast<std::int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f) };
			put(&value, 2);
		}
	} };
	for (std::size_t index{}; index < static_cast<std::size_t>(NatureKind::count); ++index)
	{
		const auto kind{ static_cast<NatureKind>(index) };
		const auto species{ kind == NatureKind::bird_phrase ? 4 : 1 };
		for (int s{}; s < species; ++s)
			for (std::uint64_t seed{ 1 }; seed <= 3; ++seed)
				write(std::string{ nature_kind_name(kind) } + (species > 1 ? "_" + std::to_string(s) : "") + "_" + std::to_string(seed),
					render(kind, seed, nature_kind_is_bed(kind), static_cast<std::uint8_t>(s)));
	}
}

TEST(Nature, DISABLED_RenderCost)
{
	// Prints how long each kind takes to render, per second of sound, for the plan's numbers.
	for (std::size_t index{}; index < static_cast<std::size_t>(NatureKind::count); ++index)
	{
		const auto kind{ static_cast<NatureKind>(index) };
		const auto started{ std::chrono::steady_clock::now() };
		std::size_t samples{};
		for (std::uint64_t seed{ 1 }; seed <= 4; ++seed)
			samples += render_nature({ .kind = kind, .reverb = 0.15f }, { .loop = nature_kind_is_bed(kind), .seed = seed }).size();
		const auto ms{ std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() };
		std::printf("%-12s %6.2f ms per second of sound%c", std::string{ nature_kind_name(kind) }.c_str(),
			ms / (static_cast<double>(samples) / sfx_sample_rate), 10);
	}
}
