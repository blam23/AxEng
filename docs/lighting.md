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

## Ambient occlusion

`Window::submit_ambient_occlusion(AmbientOcclusion2D)` darkens the light map round sprites drawn with
`sprite_flag_ao_caster`, by day and night, like the noise (cloud) shadow: it keeps the lighting passes running in full
daylight. `AmbientOcclusion` (`ambient_occlusion.{h,cpp,wgsl}`) draws the casters' silhouettes with the radiance occluder
sprite pipeline into a mask, `offset` world pixels from where they stand (the way their shadow falls), over the screen
plus a margin a shadow can fall in from. Sprites drawn as themselves and stand-ins (`sprite_flag_ao_only`: casters
that are not drawn, for things drawn as shapes or baked into the ground) are kept in two sets: each set's silhouettes
take less of its own shadows (`selfDarkness`) but all of the other's, so a flying player's shadow falls on a building.
Both masks are blurred together (sigma half of `radius`) into a factor at the light map's size, `1 - darkness * shadow`,
which the apply pass multiplies the light by after clamping it, so lights brightening past daylight cannot wash it out.
The masks are `resolution` screen pixels per texel. Each of the four silhouette draws has a camera buffer of its own,
since all are written before the frame's commands run.

## Shadows (radiance cascades)

With `LightingSettings::radiance.enabled`, lights are blocked by occluders with soft, correct penumbrae, using 2D
radiance cascades (`RadianceCascades`, `radiance_cascades.h`, `radiance.wgsl`). The cost does not depend on how many
lights there are.

**Which lights.** A light is traced when `Light2D::shadows` is above 0, it is positive, omni or a cone (not a beam), at
least `minLightTexels` GI texels in radius, inside the grid, and before the first `LightOccluder2D` (so a deck's own
lights stay analytic and its occluder still hides what is beneath). At most 2046 are traced. The rest are drawn as
before. Light leaves a disc of `Light2D::sourceRadius` world pixels round the light (never smaller than 2.5 probe
spacings, below which sources fall between the rays): give a light on a solid thing a source radius inside it: what blocks
within 1.75 times the source is taken as its body and lets its light out. Close to its source the cascades' few nearby rays cannot resolve a light and it would flicker
as it moves, so a traced light's near part (out to four times its source plus four probe spacings,
`radiance_near_scale` and `radiance_near_spacings`) is drawn plainly with marched shadows, fading out as the cascades
fade in (`radiance_near_part`, `Light2D::nearReach`). The cascades take that fade, like any shape, at their probes, so
it must be gentle: a short one shows as a ring round the light.

**Marched lights.** A light that would be traced but cannot be (outside the grid, below `minLightTexels`, past the
traced limit) is marched instead, so lamps crossing the grid's edge as the view moves or zooms keep their shadows.
A light with `shadows` 2 (`Light2D::marched_shadows`) is not traced: it is drawn plainly, and each
pixel marches the cascades' distance field towards it for a soft shadow as wide as its source subtends (light discs do
not block; a pixel inside an occluder takes the light where it leaves it towards the light, fading over 16 world
pixels). Its shape is exact and nothing is sampled on the cascades' fixed grid, so it does not crawl as it moves: use it
for the few lights that move (a player's). It needs the grid but adds nothing to its reach; about 0.1 to 0.2 ms for a
player's two lights at 1080p. Without the cascades it is drawn unshadowed.

**Occluders.** Sprites with `sprite_flag_occluder` (where their alpha is at least a half, so shadows follow their
outlines) and `RadianceCircle`s from `Window::submit_radiance_circles`. Static sprite batches and distance-field
sprites do not occlude.

**Passes**, all render (fragment) passes using only core WebGPU, so they need nothing beyond what the rest of the
renderer does on Dawn or in a browser:

1. A world-aligned grid round the view plus `margin` (capped at `maxMarginFraction` of the view each side), a GI texel
   being about `divisor` screen pixels: its size in world pixels is a power of the square root of two, so the probes
   stay put in the world while the view zooms within a half octave (otherwise each zoom step shifts them all and the
   light flickers), changing once at each half octave. When it changes, the old grid's last result fades out over the new one
   for 12 frames (not traced again, only its resolve pointed at the current view: the grid is fixed in the world and
   lamps stay put), so the light does not jump. Its origin snaps to the coarsest probe spacing so probes stay put while the camera
   moves. `radiance_grid` picks the fewest cascades that reach the largest traced light.
2. Scene (RGBA16F): occluders and traced lights' discs are drawn apart and combined: occluder albedo with alpha 1,
   discs with alpha 2 + their index, and an occluder within 1.75 times a light's source (`OWN_SCALE`: its own body, a
   lamp's head or a machine, corners and all) alpha -(2 + index). A body glows with its light and blocks all others,
   shaped as drawn. Where nothing covers a source (a light with no body) it is see-through; give a light on a solid
   thing a source inside its body, so its body is all the cascades (and marched shadows) see of it. How far each light's body reaches from it is gathered (an atomic max) and stamped into its disc's
   texels, so a ray crossing a disc walks a texel at a time only where it could meet the body. Consecutive lights
   with the same position and source radius (a lamp and its cone) share a disc and add together (up to four).
3. Jump flood from the occluders' edge texels, then a distance field (packed with the offset to the nearest surface).
4. Cascades from the top down, pre-averaged and direction-first, each texel tracing four rays (sixteen with the
   `bilinearFix`) by sphere tracing and merging with the cascade above. A ray hitting a light gets its radiance scaled
   so that, unblocked, a probe receives exactly the light's analytic falloff and cone; blocked, the share of the source
   it can see. Lights' discs do not block: a ray takes a disc's light and carries on through it (straight to its far
   side, or to its body), so lamps do not shadow each other's light or the sky except by their bodies; a ray beginning
   inside a disc does not take it again.
   A ray hitting a light counts by how much of its wedge (its cascade's angle between rays) covers the true
   disc, not the texels it was drawn into (drawn a texel wider), so a moving light does not flicker. A ray hitting an
   occluder gets `bounce` × albedo × last frame's light map. Rays passing within 0.75 texels of a light's disc hit it
   (so they cannot step past its edge); rays skimming an occluder carry on a texel at a time. The bilinear fix leaves out upper probes inside occluders, whose rays would run into them.
5. Resolve into the light map in place of its ambient clear: outside occluders the cascade 0 probes (those inside solids
   and inside lights' discs, which see only that light, left out); inside an occluder the light just past where it ends in each of a ring
   of directions, nearer exits counting most, so a building's lit side is lit and its far edge dark. Depth and the edge come from the distance field blended between texels, and the
   two are blended across the edge, so neither shows the GI texels. Ambient is scaled by `mix(1, sky visibility within aoRange, aoStrength)` (sky occlusion, off by default:
   `aoStrength` 0).
   Analytic lights, occluders, noise shadows and glows follow as before.

`LightingSettings::lightOnly` (a debug view) draws the light map, then glows, in place of the scene.
`debugView` draws what the cascades see instead of the light: 1 occluders and lights (a light's own body half red), 2 distance field, 3 traced light
only, 4 sky visibility. `lighting_stats()` adds `radiance_width`, `radiance_height`, `radiance_cascades`,
`radiance_lights` and `radiance_circles`.

**Reuse.** When nothing the cascade passes read has changed (the grid, traced lights, circles and the occluder
signature the window passes, a hash of its occluder sprites), encode traces nothing and only re-points the last
result's resolve at the view (`RadianceCascades::reused`, `lighting_stats().radiance_reused`). Moving lights should be
marched so that frames stay reusable. Probes inside occluders are never traced. `fixCascades` can limit the bilinear
fix to the lowest cascades, but each one left out rings round lights.

**Cost** (`LightRendererTest.DISABLED_ReportsGpuTimeForRadianceCascades`: 1920x1080, zoom 1.5, 60 lights, 800 circles,
development machine; the lighting passes alone take 0.24 ms):

| divisor, probe spacing | bilinear fix | ms |
|------------------------|--------------|----|
| 4, 2 | on  | 1.5 |
| 2, 2 | on  | 3.0 |
| 2, 2 | off | 1.8 |
| 2, 1 | on  | 9.4 |
| 2, 1 | off | 4.4 |

Without the bilinear fix light leaks in blotches through small gaps between occluders, and small sources ring.
`LightRendererTest.DISABLED_RadianceProfileDump` prints traced against analytic falloff for tuning
(`AXENG_RADIANCE_DUMP_SOURCE`, `AXENG_RADIANCE_DUMP_NO_FIX`); `DISABLED_RadianceInteriorDump` and `DISABLED_RadianceTwoTreesDump` write close-ups of lit
occluders (`radiance_interior_<spacing>.png`, `radiance_two_trees.png`, next to the tests);
`DISABLED_RadianceMovingLightFlickerDump` and `DISABLED_RadianceGameFlickerDump` (`AXENG_FLICKER_ZOOM`) measure how
much a moving light flickers beyond its analytic change; `DISABLED_RadianceNearProfileDump` prints traced against
analytic light near a source.

Gaps between occluders narrower than a few probe spacings let little light through further away, where coarse probes
carry it.
