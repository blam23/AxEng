#pragma once

#include "axeng/core/audio/audio_system.h"
#include "axeng/core/audio/fm_sfx.h"
#include "axeng/core/audio/fm_voice.h"
#include "axeng/core/audio/instrument.h"
#include "axeng/core/audio/sfx_synth.h"
#include "axeng/core/lua/lua_bindings.h"

#include <optional>
#include <string>

// Conversions between Lua tables and audio definitions, shared by app.audio and by projects that keep their own copy
// of sound definitions (they need no audio device). See docs/audio.md for the table formats. Failures log an error.
namespace ax::lua::bindings
{
	// Reads sfxr parameters over `params` and clamps them. Returns false for an unknown wave name.
	bool sfx_params_from_table(const sol::table& table, ax::audio::SfxParams& params);
	sol::table sfx_params_to_table(sol::state_view lua, const ax::audio::SfxParams& params);

	ax::audio::InstrumentEnvelope instrument_envelope_from_table(const sol::table& table,
		ax::audio::InstrumentEnvelope envelope);

	// An FM patch (the format of `type = "fm"` instruments). Rejects out-of-range fields and missing operators.
	std::optional<ax::audio::FmInstrumentDefinition> fm_definition_from_table(const sol::table& table);
	// The reverse of fm_definition_from_table, with `type = "fm"`. Fields at their defaults are left out, apart from
	// gain, envelope and each operator's envelope, output level and coarse/fine ratio.
	sol::table fm_definition_to_table(sol::state_view lua, const ax::audio::FmInstrumentDefinition& definition);

	// A sound definition for AudioSystem::define_sound. Sounds with an `fm` table or an `fm_preset` play on the FM
	// voice; the rest use `preset` and `params`.
	std::optional<ax::audio::SoundDefinition> sound_definition_from_table(const std::string& name,
		const sol::table& table);
}
