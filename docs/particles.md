# GPU particles

AxEng exposes a fixed-capacity 2D particle emitter through `app.particles`. Particle records are allocated
in WebGPU storage buffers and remain GPU-resident. The compute shader initializes slots, evaluates lifetime
and analytic motion, tests particle bounds against the camera, and compacts visible indices. The render
shader reads those same particle records and the GPU-generated visible list; indirect drawing avoids issuing
instances for particles outside the view. No particle-state readback is used.

The frame encoder runs particle compute work before render passes. Particles render in the transparent
weighted-blended OIT pass, after transparent sprites. Particle positions use the engine's world-space pixel
coordinates and the active camera transform.

## Lua API

```lua
local emitter = app.particles.create({
    capacity = 1024,
    emission_rate = 30,
    duration = 1.5,
    explosiveness = 0.25,
    loop = false,
    seed = 42,
    position = { x = 100, y = 80 },
    rotation = 0,
    local_space = false,
    spawn_shape = "circle",
    spawn_size = { x = 20, y = 10 },
    direction = -1.57,
    spread = 0.8,
    spawn_radius = 3,
    lifetime = { min = 0.3, max = 1.0 },
    speed = { min = 20, max = 60 },
    size_start = 8,
    size_end = 1,
    particle_rotation = { min = -1, max = 1 },
    angular_velocity = { min = -4, max = 4 },
    start_color = { r = 1, g = 0.5, b = 0.1, a = 1 },
    end_color = { r = 0.2, g = 0.05, b = 0, a = 0 },
    gravity = { x = 0, y = 40 },
    z = 0,
})

emitter:set_position(120, 90)
emitter:set_rotation(0.2)
emitter:play()
emitter:stop()
emitter:clear()
emitter:set_visible(false)
local emission_finished = emitter:finished()
```

`set_config(table)` updates editable fields while retaining the emitter's fixed capacity. Capacity is selected
when the emitter is created; recreate the emitter to change it. Invalid values raise Lua errors.
`app.particles.set_paused(true)` freezes all emitters without clearing their state; pass `false` to resume.
`app.particles.set_render_enabled(false)` disables drawing for all emitters without changing their individual
state
`app.particles.suspend_all()` remembers each emitter's playing and visible state, then stops and hides it;
`app.particles.resume_all()` restores those states. Emitters that were already stopped or hidden remain so.
including its estimated particle reach. It does not alter visibility or simulation culling.

| Field | Meaning |
| --- | --- |
| `capacity` | Fixed GPU slot count; 1–65536 |
| `emission_rate` | Average particles per second over an emission cycle |
| `duration` | Length of an emission cycle in seconds (0.01–3600) |
| `explosiveness` | 0–1: 0 distributes births evenly across the cycle; 1 places them at its start |
| `loop` | Repeat emission cycles |
| `seed` | Seed for deterministic per-particle random ranges |
| `position`, `rotation`, `z` | Emitter transform and depth |
| `local_space` | Keep particle launch data relative to the emitter transform |
| `spawn_shape` | `point`, `circle`, or `rectangle`; circle uses `spawn_radius`, rectangle uses `spawn_size` |
| `direction`, `spread` | Direction and angular variation in radians |
| `lifetime`, `speed` | `{ min, max }` ranges in seconds and world units per second |
| `particle_rotation`, `angular_velocity` | `{ min, max }` ranges in radians and radians per second |
| `size_start`, `size_end` | Particle size interpolated over its lifetime |
| `start_color`, `end_color` | RGBA colors interpolated over its lifetime |
| `gravity` | Constant world-space acceleration |

## Lights

An emitter can light the scene (see [lighting.md](lighting.md)) in two independent ways. Both only show while
lighting is on, and both follow the emitter's `visible` and suspended state.

```lua
light = {
    enabled = true,
    color = { r = 1, g = 0.6, b = 0.2 },
    radius = 96,
    intensity = 1,
    falloff = 2,
    offset = { x = 0, y = 0 },
    radius_wave = { amplitude = 12, frequency = 2, phase = 0 },
    intensity_wave = { amplitude = 0.2, frequency = 7, phase = 0 },
    color_wave = { color = { r = 1, g = 0.9, b = 0.5 }, amplitude = 1, frequency = 3, phase = 0 },
},
particle_light = { enabled = true, radius_scale = 8, intensity = 1, falloff = 2 },
```

`light` is one light at the emitter (`offset` is rotated with the emitter). It is on while the emitter plays and
fades out linearly over the longest particle lifetime after it stops. Each wave adds
`amplitude * sin(2 * pi * frequency * t + phase)` to the radius (world pixels) or intensity, with `t` the seconds
since the emitter was created and `frequency` in hertz. The colour wave instead blends from `color` towards its own
`color` by `(0.5 + 0.5 * sin) * amplitude` (amplitude 0..1). Omitted waves have an amplitude of 0.

`particle_light` makes every live particle a light of its own, in the particle's current colour with its alpha
scaling `intensity`, and a radius of its current size times `radius_scale`. It is drawn on the GPU from the same
visible lists as the particles, so it costs one indirect draw per emitter and no readback. Particles up to
`max(size_start, size_end) * radius_scale` outside the view are still simulated so their light reaches in. The cost
is fill rate in the light map, so keep `radius_scale` and the particle count modest.

The expected total births per cycle are `emission_rate × duration`. Explosiveness compresses those evenly
spaced births into the first `duration × (1 - explosiveness)` seconds; at exactly 1 they occur at the cycle
start. Fractional emission counts carry into subsequent cycles.

`finished()` reports that a non-looping emitter has finished emitting; it does not query the GPU for remaining
live particles.

## Motion and culling

Particle records store their spawn origin, initial velocity, birth time, lifetime, and rotation data. Position,
rotation, size, and color are derived analytically on the GPU from age and the emitter parameters. This avoids
CPU particle updates and lets the compute pass skip an emitter while its conservative reachable AABB is
outside the camera. The bound covers recorded spawn origins, maximum speed and lifetime, gravity, spawn
area, and particle size. Its origin history is retained conservatively, so moving emitters can make the
bound larger but cannot cause visible particles to be wrongly dropped.

When an emitter is considered, compute derives each particle's current rotated AABB, excludes non-intersecting
particles from the GPU-generated visible list, and marks expired slots reusable. Indirect drawing therefore
does not issue particle instances outside the camera. Visibility is tested against the camera's world-space
viewport bounds, which are derived from the current zoom; the rotated bounds include a one-pixel safety margin.
The F5 **Particle emitter AABBs** debug toggle draws the same whole-emitter bounds for diagnosis.
Per-particle motion is analytic rather than stepped;
when an emitter is outside its conservative bound, the engine skips its compute dispatch and derives current
particle age from GPU birth timestamps when it becomes visible again. If it has been culled long enough to
make the wrapped GPU clock ambiguous, stale slots are cleared. Emission timing continues while culled; births
that would have happened off-screen are not accumulated as a backlog.

Particle movement currently uses constant gravity and no collision or drag. The render shape is a soft-edged
circle with alpha transparency. Texture particles, curves beyond start/end interpolation, collision,
trails, and additive rendering are not part of this first implementation.