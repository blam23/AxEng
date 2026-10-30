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
   `Light2D` added in one instanced draw;
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

`lighting_active(settings, lights, glows)` says whether a frame needs the passes. `Window::lighting_ran()` and
`lighting_stats()` (`app.window.lighting_stats()` in Lua) report what the last frame drew.

## Cost

The cost is fill rate in the light map, not draw calls: all lights are one draw and all glows another, whatever their
number. At 1920x1080 with 4x MSAA on the development machine, 1000 lights of 192 screen pixels' radius cost about
0.6 ms at half resolution, and 5000 about 2 ms (`LightRendererTest.DISABLED_ReportsGpuTimeForManyLights`). Callers with
very many lights should cull ones smaller than a light map texel and lower `resolutionDivisor` as coverage grows.
