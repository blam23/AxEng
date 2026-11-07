# Audio

AxEng has a small audio system built on [miniaudio](https://miniaud.io/) (vcpkg `miniaudio`). It has two parts:

- **Generated sound effects**: an sfxr/bfxr style synthesiser. Sounds are described by a preset and/or a table of
  synth parameters. Each sound can pre-render several slightly mutated variants and add random pitch and volume
  on every play, so repeated sounds don't sound identical.
- **Music**: encoded tracks (`.ogg`, `.mp3`, `.flac`, `.wav`) loaded as assets, the same way as textures and
  scripts, and played singly or as a shuffled playlist.
- **Pitched instrument DSP**: generic MIDI-note synth and sampler voices with ADSR envelopes, plus decoded mono/stereo
  audio sample assets. These are low-level voice primitives; arrangement scheduling and music-bus playback are a
  separate layer.

Code lives in `src/axeng/core/audio/`:

| File | Contents |
| --- | --- |
| `sfx_synth.h/.cpp` | `SfxParams`, `synthesize()`, `mutate()`, `generate_preset()`. Pure functions, no device needed. |
| `audio_system.h/.cpp` | `AudioSystem`: output device, mixer, sound bank, voices, buses and the music player. |
| `music.h/.cpp` | `Music` asset and `MusicManager` (`AssetManager<Music, MusicManager>`). |
| `instrument.h/.cpp` | Pitched synth/sampler voice state, envelopes, resampling, and sample decoding. |
| `fm_voice.h/.cpp` | Six-operator FM voice and its algorithm table. |
| `audio_sample.h/.cpp` | Decoded `AudioSample` assets and `AudioSampleManager`. |
| `miniaudio_impl.c` | The miniaudio + stb_vorbis implementation translation unit (compiled with `-w`). |

Lua bindings are in `lua/bindings/lua_audio_bindings.cpp` and are exposed as `app.audio` on the **main thread
only**.

## When audio is enabled

- Windowed apps get an `AudioSystem` automatically. Headless apps don't, unless the manifest sets `audio = true`.
  `audio = false` disables it for windowed apps. Without an audio system `app.audio` is `nil`.
- If no output device can be opened, `app.audio` still exists and everything works (sounds can be defined,
  rendered and inspected, and time passes as normal: one-shots finish and playlists advance) but
  `app.audio.available` is `false` and the output is discarded.
- `--audio-backend` selects the output behavior:
  - `none`: never open a device. Tests use this.
  - `null`: use miniaudio's null backend, which runs a real audio thread but discards the output. Useful for
    automated runs of a game without making noise.
  - `auto` (the default): use the system's normal output device.

## Music assets

Add tracks to `project.json` under `music`; the compiler copies them and lists them in `manifest.music`:

```json
"music": {
    "assembly_line": "assets/music/assembly_line.ogg",
    "night_shift": "assets/music/night_shift.ogg"
}
```

Tracks are validated when the app loads (a bad file logs an error and is skipped). The compressed data stays in
memory and is decoded while it plays, so Ogg Vorbis is the best choice for size. The audio thread reads the
assets, so `Application` destroys the `AudioSystem` before unloading music.

## Audio sample assets

Declare source samples separately from encoded music playlists. The compiler copies these files and writes them to
`manifest.audio_samples`:

```json
"audio_samples": {
    "piano": "assets/music_samples/piano.ogg"
}
```

`Application::audio_samples()` provides the loaded `AudioSampleManager`. Samples decode to immutable interleaved
float PCM on the loading thread; mono and stereo WAV, Ogg Vorbis, MP3, and FLAC are supported. Decoded samples are
limited to 8–192 kHz and five minutes. Keep the manager/asset alive while sampler voices reference its PCM.

`decode_audio_sample()` can decode encoded bytes without an `Application` asset manager. `SamplerInstrumentVoice`
uses per-instance settings for root note, fine tuning, trim, optional loop points, gain, and envelope, so multiple
sampler definitions can share one immutable sample asset without changing one another. Decoded samples are checked
for finite PCM once; voice construction checks only sample metadata so creating a note does not scan the full buffer.

## Pitched instrument voice primitives

`SynthInstrumentVoice` produces MIDI-pitched sine, triangle, band-limited saw, band-limited square, or deterministic
noise notes with velocity, ADSR, detune, slow vibrato, a one-pole low-pass filter, and an optional exponential
pitch drop (up to 48 semitones over 2 seconds). These generic primitives can form electronic percussion such as
808-style kicks, snares, and hats without game-specific engine code. `SamplerInstrumentVoice` resamples mono/stereo PCM at
the requested MIDI pitch and applies an independent ADSR and sampler configuration. Both voice types are
allocation-free after construction and expose `note_off()` for release.

`AudioSystem::play_synth()` and `play_sampler()` play these voices through the existing mixer and return normal
voice IDs. `note_off(id)` begins the voice's release envelope; `stop(id, fade)` remains available for an immediate
fade-out. Sampler PCM must outlive its active voices. Voice creation and control happen on the main thread; voice
rendering and sample decoding do not invoke Lua or allocate on the audio thread.

For authoring, Lua can render one bounded synth note to a mono 16-bit PCM WAV byte string, then register it as a
runtime sample for preview or arrangement use:

```lua
local wav = app.audio.render_synth_sample({
    wave = "triangle",
    gain = 0.3,
    envelope = { attack = 0.1, decay = 0.5, sustain = 0.7, release = 1.0 }
}, 60, { hold_seconds = 2.0, velocity = 0.8, sample_rate = 44100 })
assert(wav and app.audio.load_sample("user_pad_c4", wav))
```

The render captures the note through its release, uses a maximum 30-second hold-plus-release duration and a sample
rate from 8–96 kHz, and limits peaks to 0.95 before WAV encoding. The default sample rate is the audio device rate
up to 48 kHz. `load_sample(name, bytes)` rejects empty/oversized input, invalid audio, and duplicate loaded names;
it does not write files or modify the project manifest. A project editor must persist returned WAV bytes to a managed
project asset and update its manifest before the sample can be used after restart. `inspect_sample(bytes)` validates
encoded audio without registering it and returns its sample rate, channel count, frame count, and duration.
`sample_info(name)` returns the same metadata for a sample that is already loaded, or `nil` when it is not present.

Lua can audition pitched instruments using the loaded sample bank:

```lua
local id = app.audio.play_synth({
    wave = "sine",
    gain = 0.3,
    envelope = { attack = 0.2, decay = 0.5, sustain = 0.7, release = 2.0 }
}, 60, { velocity = 0.8, volume = 1, pan = 0, bus = "music" })

local piano_id = app.audio.play_sampler("piano", {
    root_note = 60,
    start_frame = 0,
    end_frame = 2205,
    tuning_cents = 0,
    gain = 1,
    envelope = { attack = 0.01, decay = 0, sustain = 1, release = 0.2 }
}, 60)
app.audio.note_off(piano_id)
```

Synth tables accept `wave` (`sine`, `triangle`, `saw`, `square`, or `noise`), `gain`, `detune_cents`,
`vibrato_cents`, `vibrato_rate`, `low_pass_hz`, `pitch_drop_semitones`, `pitch_drop_seconds`, and an `envelope`
table. Sampler tables accept `root_note`, `tuning_cents`, trim and loop frame settings, `gain`, `loop`, and
`envelope`. Playback options accept `velocity`, `volume`, `pan`, and `bus` (`sfx`, `ui`, or `music`).
`update_voice()` can ramp volume and pan for instrument voices; pitch updates are only supported for generated SFX.

## FM voices

`FmInstrumentVoice` is a six-operator phase-modulation voice in the style of 1980s FM keyboards. Its parameters use
the familiar 0..99 ranges. Each operator has a four-stage rate/level envelope, a ratio or fixed frequency, detune,
velocity sensitivity, keyboard level and rate scaling, and LFO amplitude sensitivity. A voice has one of 32 algorithms,
feedback, a pitch envelope, an LFO and a transpose. `fm_algorithm(n)` exposes the routing for editors.

Velocity reaches the sound only through each operator's `velocity_sensitivity`, so it can change brightness as well as
level. A carrier with no sensitivity plays at the same level at every velocity. Sounding carriers are averaged, so
`gain` stays close to the voice's peak level whatever the algorithm. The outer `envelope` gates the whole
voice: its `release` is the tail used for scheduling and polyphony, so set it to cover the slowest carrier's release.

```lua
local op = { output_level = 99, coarse = 1, rates = { 99, 40, 30, 50 }, levels = { 99, 70, 0, 0 } }
local mod = { output_level = 75, coarse = 14, rates = { 99, 60, 40, 50 }, levels = { 99, 0, 0, 0 } }
local silent = { output_level = 0 }
local id = app.audio.play_fm({
    algorithm = 1, feedback = 0, transpose = 0, gain = 0.3,
    envelope = { attack = 0, decay = 0, sustain = 1, release = 1.5 },
    pitch_envelope = { rates = { 99, 99, 99, 99 }, levels = { 50, 50, 50, 50 } },
    lfo = { wave = "triangle", speed = 35, delay = 0, pitch_depth = 0, amp_depth = 0, pitch_sensitivity = 0 },
    operators = { op, mod, silent, silent, silent, silent },
}, 60, { velocity = 0.8, bus = "music" })
```

Operator tables also accept `fine`, `detune`, `fixed`, `velocity_sensitivity`, `amp_mod_sensitivity`, `rate_scaling`,
`breakpoint`, `left_depth`, `right_depth`, `left_curve` and `right_curve` (`neg_lin`, `neg_exp`, `pos_exp`, `pos_lin`).
LFO waves are `triangle`, `saw_down`, `saw_up`, `square`, `sine` and `sample_hold`. `render_fm_sample()` matches
`render_synth_sample()`. Silent operators are skipped. `output_trim` (1..16, default 1) lifts a patch whose carriers
peak below full scale, so `gain` can still mean its peak level. `app.audio.fm_algorithm(n)` returns `modulators` (for
each operator, the list that modulates it), `carriers`, `feedback_target` and `feedback_source`, for editors to draw.

## Sequence arrangements

`audio.sequence` accepts and prepares immutable note arrangements on the Lua/main thread. Notes use beat positions;
the tempo converts them to sample frames before playback. The mixer schedules those prepared events with a fixed
64-voice limit and does not access Lua or allocate arrangement voice state in its callback. Arrangements are limited
to 64 instruments, 16384 notes, and 30 minutes. Sampler definitions resolve to loaded `audio_samples` assets during
preparation; those assets must outlive the audio system.

```lua
local sequence = app.audio.sequence
sequence.define("quiet_theme", {
    tempo = 54,
    duration_beats = 64,
    reverb = { wet = 0.25, decay_seconds = 3.5, damping = 0.4 },
    instruments = {
        {
            type = "synth",
            wave = "triangle",
            gain = 0.18,
            envelope = { attack = 1.5, decay = 0.5, sustain = 0.8, release = 3.0 }
        },
        {
            type = "sample",
            sample = "piano",
            root_note = 60,
            gain = 0.5,
            envelope = { attack = 0.01, decay = 0, sustain = 1, release = 1.2 }
        }
    },
    notes = {
        { start_beat = 0, duration_beats = 8, instrument = 1, note = 48, velocity = 0.7 },
        { start_beat = 12, duration_beats = 1, instrument = 2, note = 67, velocity = 0.4 }
    }
})

sequence.play("quiet_theme", 0.5)
local state = sequence.state()      -- active, paused, position_seconds, duration_seconds
sequence.pause()
sequence.seek(18.0)                 -- seconds; active notes and sample offsets are reconstructed
sequence.resume()
sequence.stop(0.5)
```

Instruments are one-based indices into `instruments`; each note supplies `start_beat`, `duration_beats`,
`instrument`, MIDI `note`, and optional `velocity` and `pan`. A synth instrument accepts the pitched synth fields
described above with `type = "synth"`, and an FM instrument accepts the FM fields with `type = "fm"`. A sampler uses `type = "sample"` and a loaded `sample` name plus the sampler
settings above. `sequence.define()` reports invalid tempo, duration, assets, note data, and polyphony as an error
and does not replace a previously prepared arrangement on failure. `sequence.list()` returns defined arrangement
names; `sequence.play()`, `stop()`, `pause()`, `resume()`, `seek(seconds)`, and `state()` control the active
arrangement. Seeking clamps to the arrangement duration and rebuilds active note/envelope/sample positions. It resets
voice and reverb state, then fades the reverb return in over 20 ms; it does not recreate historical reverb tails.
Sequence pause state is independent of `audio.music.pause()` / `resume()`, which control encoded music playback.

Any instrument can set `chorus_send` (0..1, default 0). At 1 it is an even blend of the dry sound and a stereo chorus
on the arrangement mix, which widens the sound at about the same level. The chorus runs before the reverb, is
allocated only when an instrument uses it, and is reset by seeking. `reverb_send` (0..1, default 1) sets how much of an
instrument reaches the reverb. Keep it low for bass: the early reflections are close enough to low notes to partly
cancel them, which makes each note sound as if it starts with a hit.

`reverb` is optional and defaults to dry playback. Its additive wet-return level is 0..1, `decay_seconds` is 0.1..8,
and `damping` is 0..0.95. The bounded four-line stereo effect runs on the arrangement mix; its delay buffers are
prepared when playback starts and are not allocated in the audio callback. Seeking fades the wet return in without
changing the dry signal level. Natural ends, stops, and crossfades drain the reverb for at most 10 seconds. Sequence
playback shares the music bus with encoded tracks and fades the other music source when starting. Delay and automation
remain future work. AxFactory's loading screen prepares its saved procedural tracks
incrementally, one bounded track operation per frame, before entering the requested scene.

## Lua API

```lua
local audio = app.audio            -- nil if the app has no audio system
audio.available                    -- true if an output device is open
audio.sample_rate                  -- device sample rate
```

### Synth parameters

A params table has `wave` (`"square"`, `"saw"`, `"sine"`, `"noise"`, `"triangle"`) plus any of these numbers.
Unsigned values are 0..1, signed sweeps are -1..1, and out of range values are clamped.

| Group | Fields |
| --- | --- |
| Envelope | `attack`, `sustain`, `punch`, `decay` (stage length is value² × 100000 samples at 44.1 kHz) |
| Pitch | `freq`, `freq_limit`, `freq_slide` (+ rises), `freq_delta_slide`, `vibrato_depth`, `vibrato_speed` |
| Arpeggio | `arp_mod` (+ jumps up, - down), `arp_speed` |
| Square duty | `duty`, `duty_sweep` |
| Repeat | `repeat_speed` (restarts the pitch sweep periodically, 0 = off) |
| Phaser | `phaser_offset`, `phaser_sweep` |
| Filters | `lpf_freq` (1 = off), `lpf_sweep`, `lpf_resonance`, `hpf_freq`, `hpf_sweep` |
| Level | `volume` (synth level, default 0.5) |

```lua
audio.presets()                    -- { "pickup", "laser", "explosion", "powerup", "hit", "jump", "blip",
                                   --   "click", "confirm", "deny", "hum", "hiss", "grind", "whirr", "thud",
                                   --   "spin_up", "spin_down", "random" }
audio.generate("click", seed)      -- params table for a random sound from a preset (same seed = same sound)
audio.mutate(params, 0.05, seed)   -- params nudged randomly, like bfxr's mutate button
audio.params()                     -- { { name = "attack", min = 0, max = 1 }, ... } in a stable order (for editors)
audio.waves()                      -- { "square", "saw", "sine", "noise", "triangle" }
```

`hum`, `hiss` and `grind` are designed as loops (machine hum, steam, drilling); `whirr` is a short servo/motor
sound and `spin_up`/`spin_down` are machines starting and stopping.

### Defining and playing sounds

```lua
audio.define("ui_click", {
    preset = "click",              -- optional: start from a generated preset...
    seed = 11,                     -- ...with this seed (defaults to a hash of the name)
    params = { decay = 0.1 },      -- optional: override/define synth params
    mutation = 0.03,               -- how far each variant may drift from the params
    variants = 4,                  -- pre-rendered variants (default 6 if mutation > 0, else 1; max 32)
    pitch_variation = 0.6,         -- random +/- semitones per play
    volume = 0.5,                  -- playback volume
    volume_variation = 0.1,        -- random +/- fraction of volume per play
    max_instances = 3,             -- the oldest instance fades out when exceeded (default 8)
    cooldown = 0.03,               -- seconds; plays during the cooldown are ignored
    loop = false,                  -- render as a seamless loop
    bus = "ui",                    -- "sfx" (default) or "ui"
})

local id = audio.play("ui_click", { volume = 1, pitch = 1, pan = 0, loop = nil, fade_in = 0 })
-- id is 0 if nothing played (unknown sound, cooldown, or silent sound)
audio.set(id, { volume = 0.5, pan = -0.3, pitch = 1.2, ramp = 0.1 })  -- glide a playing voice; volume and
                                   -- pitch mean the same as in play() (they scale the sound's own level/pitch)
audio.stop(id, fade)               -- fade in seconds (default 0.05)
audio.stop_sound("ui_click", fade) -- every voice of a sound
audio.stop_all(fade)
audio.is_playing(id)
audio.voice_count()

audio.has(name)
audio.sounds()                     -- names of defined sounds
audio.prepare(name)                -- render every variant now (otherwise rendered on first use)
audio.info(name)                   -- { variants, rendered_variants, length, peak, rms, loop, bus } for variant 0
audio.waveform(name, buckets)      -- { min = {...}, max = {...} } sample range per slice of variant 0 (default 128)
```

Variants are rendered lazily on the calling (main) thread, so very long sounds with many variants are best
prepared during loading. Variant 0 always uses the exact params; the others are mutated copies. A play never
picks the same variant twice in a row.

Redefining a sound normally leaves voices that are already playing alone. `audio.define(name, def, { live = true })`
updates them instead: playing loops switch to the new sound in place (with a 30 ms crossfade, keeping their
position, pitch and pan) and pick up the new volume; one-shots finish, and loops stop looping if `loop` was
turned off. Editors use this to tweak a loop while it plays.

Loops (`loop = true`) ignore attack, punch and decay: the envelope is flat for the sustain length (at least
0.25 s), and the end is crossfaded into the start. With `repeat_speed`, the loop is a whole number of repeats.

Use `info(name).peak` and `rms` when tuning levels. A peak of exactly 1 means the synth clipped; lower the
params `volume` and raise the playback `volume` instead.

### Buses

`master`, `sfx`, `ui` and `music`. A voice's level is `voice volume × bus × master`; changes glide over ~30 ms.

```lua
audio.set_volume("music", 0.6)     -- returns false for an unknown bus
audio.get_volume("music")
```

The final mix goes through a soft limiter, so many overlapping sounds compress rather than hard clip.

### Music

```lua
audio.music.list()                         -- names of loaded tracks
audio.music.info(name)                     -- { length, sample_rate, channels }
audio.music.play(name, { fade = 1, loop = true, gap = 2 })
audio.music.shuffle(names_or_nil, { fade = 1, gap = 2, loop = true, shuffle = true })
audio.music.next(fade)                     -- crossfade to the next playlist track
audio.music.stop(fade)
audio.music.pause() / audio.music.resume() / audio.music.paused()
audio.music.current()                      -- current track name, or nil (also nil during a gap)
audio.music.playing()                      -- true while a track or playlist is active
```

`shuffle(nil)` uses every loaded track. Starting or skipping crossfades over `fade` seconds; tracks that end
naturally are followed by `gap` seconds of silence. Each pass through the playlist is reshuffled, and the first
track of a pass is never the track that just played.

## Threading

The sound bank (`define`, `play`'s variant selection and rendering) is used from the main thread only. Voices,
buses and music are shared with the audio thread under a mutex; the mixer runs in 64 frame chunks. Removing a
voice or music stream can free memory on the audio thread. Without a device there is no audio thread: the
elapsed time is mixed into a discarded buffer at the start of each voice/music API call instead (tests can opt
out of this with `Config::manualMix` and drive `AudioSystem::mix()` themselves).

## Tests

`src/tests/core/test_audio.cpp` covers the synth (presets, determinism, envelope length, loop seams, mutation),
the mixer driven offline through `AudioSystem::mix()` (panning, buses, loops, instance limits, cooldowns,
variants), music loading and shuffling through `Application` with its audio device disabled, and the Lua
bindings. `OutputDeviceRunsSilently` opens the real device (skipped if there isn't one) and plays a silent sound.
