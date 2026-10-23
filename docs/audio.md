# Audio

AxEng has a small audio system built on [miniaudio](https://miniaud.io/) (vcpkg `miniaudio`). It has two parts:

- **Generated sound effects**: an sfxr/bfxr style synthesiser. Sounds are described by a preset and/or a table of
  synth parameters. Each sound can pre-render several slightly mutated variants and add random pitch and volume
  on every play, so repeated sounds don't sound identical.
- **Music**: encoded tracks (`.ogg`, `.mp3`, `.flac`, `.wav`) loaded as assets, the same way as textures and
  scripts, and played singly or as a shuffled playlist.

Code lives in `src/axeng/core/audio/`:

| File | Contents |
| --- | --- |
| `sfx_synth.h/.cpp` | `SfxParams`, `synthesize()`, `mutate()`, `generate_preset()`. Pure functions, no device needed. |
| `audio_system.h/.cpp` | `AudioSystem`: output device, mixer, sound bank, voices, buses and the music player. |
| `music.h/.cpp` | `Music` asset and `MusicManager` (`AssetManager<Music, MusicManager>`). |
| `miniaudio_impl.c` | The miniaudio + stb_vorbis implementation translation unit (compiled with `-w`). |

Lua bindings are in `lua/bindings/lua_audio_bindings.cpp` and are exposed as `app.audio` on the **main thread
only**.

## When audio is enabled

- Windowed apps get an `AudioSystem` automatically. Headless apps don't, unless the manifest sets `audio = true`.
  `audio = false` disables it for windowed apps. Without an audio system `app.audio` is `nil`.
- If no output device can be opened, `app.audio` still exists and everything works (sounds can be defined,
  rendered and inspected, and time passes as normal: one-shots finish and playlists advance) but
  `app.audio.available` is `false` and the output is discarded.
- The `AXENG_AUDIO_BACKEND` environment variable overrides the device:
  - `none` (or `off`/`0`): never open a device. Tests use this.
  - `null`: use miniaudio's null backend, which runs a real audio thread but discards the output. Useful for
    automated runs of a game without making noise.

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
variants), music loading and shuffling through `Application` with `AXENG_AUDIO_BACKEND=none`, and the Lua
bindings. `OutputDeviceRunsSilently` opens the real device (skipped if there isn't one) and plays a silent sound.
