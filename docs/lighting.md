# 2D lighting

`Window` can darken the world and light it with coloured 2D lights as a post-process. It is off by default; with it
off, or with full-white ambient light and nothing to draw, frames render exactly as before.

## Lit and unlit content

Lighting multiplies the world by a light map, so overlays and the interface must be drawn after it. Content is
**unlit** when it is in screen space, when a shape has `unlit = true` (`Shape::unlit`, or `unlit` in a Lua shape
description), or when a sprite has `sprite_flag_unlit` in `SpriteData::useRegion`. Everything else is lit. Static
sprite batches are always lit.

While lighting runs, `ShapeRenderer::draw` holds back unlit shapes drawn in the render event
(`ShapeRenderer::set_unlit_capture`), sprites are uploaded lit first then unlit, and `DebugShapes` are split the same
way. A frame then renders:

1. the lit scene pass (render event shapes, opaque lit sprites), the lit OIT pass (with particles) and its composite;
2. lit debug shapes;
3. the light map: an RGBA16F target at 1/1, 1/2 or 1/4 of the window, cleared to the ambient colour, with every
   `Light2D` added in one instanced draw (split around any occluders, below);
4. the apply pass: one fullscreen triangle multiplying the target by the light map (no scene read-back, so it works
   on the MSAA target directly), clamped to `maxExposure` (at most 2);
5. glows: every `Glow2D` added over the result in one instanced draw, for halos that stay bright in the dark;
6. the unlit scene pass, unlit OIT pass and composite, and unlit debug shapes;
7. ImGui.

## API

```cpp
window.set_lighting({ .enabled = true, .ambient = { 0.12f, 0.14f, 0.24f }, .resolutionDivisor = 2 });
// Each frame, before the render pass. The vectors are swapped in and come back empty with spare capacity.
window.submit_lights(lights, glows);
```

`Light2D` is in world pixels: position, radius, intensity, colour and a falloff exponent for `(1 - d^2)^falloff`. A
cone limits it to `coneOuter` (cosine of the half angle) around `direction`, fully lit within `coneInner`;
`coneOuter <= -1` lights every direction. `Glow2D` is a soft radial halo with a bright core.

Particle emitters add lights of their own, which are included automatically: see [particles.md](particles.md#lights).

`LightOccluder2D` is a convex quad (four world-pixel corners) that resets the light map to ambient beneath it, hiding
every light before `firstLight`; lights from `firstLight` on are drawn after it. Pass them, in `firstLight` order, with
`window.submit_lights(lights, glows, occluders)`. Use them for things standing over the world, such as moving decks.
An occluder with a non-zero `layer` names a light layer. A particle emitter's lights go in its
`ParticleConfig::lightLayer`: layer 0 (the default), or a layer with no occluder this frame, goes beneath every occluder;
any other layer is drawn just after the occluder of that layer, so a deck's own effects light the deck.
`LightRenderer::encode` calls `extraLights(pass, layer)` once with layer 0 and once after each layered occluder. Glows are
not occluded: drop glows beneath an occluder on the CPU.

`lighting_active(settings, lights, glows)` says whether a frame needs the passes. `Window::lighting_ran()` and
`lighting_stats()` (`app.window.lighting_stats()` in Lua) report what the last frame drew.

## Cost

The cost is fill rate in the light map, not draw calls: all lights are one draw and all glows another, whatever their
number. At 1920x1080 with 4x MSAA on the development machine, 1000 lights of 192 screen pixels' radius cost about
0.6 ms at half resolution, and 5000 about 2 ms (`LightRendererTest.DISABLED_ReportsGpuTimeForManyLights`). Callers with
very many lights should cull ones smaller than a light map texel and lower `resolutionDivisor` as coverage grows.

## Shadows (radiance cascades)

With `LightingSettings::radiance.enabled`, lights are blocked by occluders with soft, correct penumbrae, using 2D
radiance cascades (`RadianceCascades`, `radiance_cascades.h`, `radiance.wgsl`). The cost does not depend on how many
lights there are.

**Which lights.** A light is traced when `Light2D::shadows` is above 0, it is positive, omni or a cone (not a beam), at
least `minLightTexels` GI texels in radius, inside the grid, and before the first `LightOccluder2D` (so a deck's own
lights stay analytic and its occluder still hides what is beneath). At most 2046 are traced. The rest are drawn as
before. Light leaves a disc of `Light2D::sourceRadius` world pixels round the light (never smaller than 2.5 probe
spacings, below which sources fall between the rays): give a light on a solid thing a source radius that covers it,
or its own body shades it.

**Occluders.** Sprites with `sprite_flag_occluder` (where their alpha is at least a half, so shadows follow their
outlines) and `RadianceCircle`s from `Window::submit_radiance_circles`. Static sprite batches and distance-field
sprites do not occlude.

**Passes**, all render (fragment) passes using only core WebGPU, so they need nothing beyond what the rest of the
renderer does on Dawn or in a browser:

1. A world-aligned grid round the view plus `margin` (capped at `maxMarginFraction` of the view each side), a GI texel
   being `divisor` screen pixels. Its origin snaps to the coarsest probe spacing so probes stay put while the camera
   moves. `radiance_grid` picks the fewest cascades that reach the largest traced light.
2. Scene (RGBA16F): occluder albedo with alpha 1, traced lights as discs with alpha 2 + their index.
3. Jump flood from the occluders' edge texels, then a distance field (packed with the offset to the nearest surface).
4. Cascades from the top down, pre-averaged and direction-first, each texel tracing four rays (sixteen with the
   `bilinearFix`) by sphere tracing and merging with the cascade above. A ray hitting a light gets its radiance scaled
   so that, unblocked, a probe receives exactly the light's analytic falloff and cone; blocked, the share of the source
   it can see. A ray hitting an occluder gets `bounce` × albedo × last frame's light map.
5. Resolve into the light map in place of its ambient clear: outside occluders the cascade 0 probes (those inside solids
   left out); inside an occluder the light just outside it, leaning towards the brighter side, so a building's lit
   side is lit and its far edge dark. Ambient is scaled by `mix(1, sky visibility within aoRange, aoStrength)`.
   Analytic lights, occluders, noise shadows and glows follow as before.

`debugView` draws what the cascades see instead of the light: 1 occluders and lights, 2 distance field, 3 traced light
only, 4 sky visibility. `lighting_stats()` adds `radiance_width`, `radiance_height`, `radiance_cascades`,
`radiance_lights` and `radiance_circles`.

**Cost** (`LightRendererTest.DISABLED_ReportsGpuTimeForRadianceCascades`: 1920x1080, zoom 1.5, 60 lights, 800 circles,
development machine; the lighting passes alone take 0.24 ms):

| divisor, probe spacing | bilinear fix | ms |
|------------------------|--------------|----|
| 4, 2 | on  | 1.3 |
| 2, 2 | on  | 2.7 |
| 2, 2 | off | 1.6 |
| 2, 1 | on  | 8.1 |
| 2, 1 | off | 3.4 |

Without the bilinear fix light leaks in blotches through small gaps between occluders, and small sources ring.
`LightRendererTest.DISABLED_RadianceProfileDump` prints traced against analytic falloff for tuning
(`AXENG_RADIANCE_DUMP_SOURCE`, `AXENG_RADIANCE_DUMP_NO_FIX`).
