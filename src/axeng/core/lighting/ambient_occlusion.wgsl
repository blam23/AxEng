// Ambient occlusion: the silhouettes of things that stand up (sprite_flag_ao_caster) are drawn into a mask, shifted
// the way their shadows fall, blurred into soft shadows and multiplied into the light map, like the cloud shadows.
// Sprites drawn as themselves and stand-ins (sprite_flag_ao_only, for things drawn as shapes or baked into the ground)
// are kept apart: each set's own silhouettes take less of its own shadows, but all of the other set's, so a flying
// player's shadow falls on the building beneath them.

struct Ao {
	// The mask: its size in texels, and 1 over that.
	mask: vec4<f32>,
	// Screen pixels per light map texel, the margin round the screen the mask covers (screen pixels), and the screen size.
	screen: vec4<f32>,
	// A blur pass: its direction (one texel step), the gaussian's sigma and the reach in steps.
	blur: vec4<f32>,
	// How dark a covered spot gets and how much of that a caster's own silhouette takes.
	shade: vec4<f32>,
};

@group(0) @binding(0) var<uniform> ao: Ao;
// Blurring across: the sprites' and the stand-ins' shifted silhouettes. Down: the first blur. Applying: the second.
@group(0) @binding(1) var source: texture_2d<f32>;
@group(0) @binding(2) var standIns: texture_2d<f32>;
@group(0) @binding(3) var linearSampler: sampler;
// Applying: the sprites' and the stand-ins' silhouettes where they stand.
@group(0) @binding(4) var spriteSilhouettes: texture_2d<f32>;
@group(0) @binding(5) var standInSilhouettes: texture_2d<f32>;

struct FullscreenOut {
	@builtin(position) pos: vec4<f32>,
};

@vertex
fn vs_fullscreen(@builtin(vertex_index) vertex: u32) -> FullscreenOut {
	var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
	var out: FullscreenOut;
	out.pos = vec4<f32>(positions[vertex], 0.0, 1.0);
	return out;
}

// One direction of a separable gaussian blur of both sets: the first pass reads their coverage (red) from the two
// masks, the second the first's result (red and green).
fn blur(pos: vec2<f32>, first: bool) -> vec2<f32> {
	let uv = pos * ao.mask.zw;
	let sigma = max(ao.blur.z, 0.001);
	let steps = i32(ao.blur.w);
	var sum = vec2<f32>(0.0);
	var total = 0.0;
	for (var i = -steps; i <= steps; i = i + 1) {
		let t = f32(i);
		let weight = exp(-0.5 * t * t / (sigma * sigma));
		let at = uv + ao.blur.xy * t * ao.mask.zw;
		let texel = textureSampleLevel(source, linearSampler, at, 0.0);
		var value = texel.rg;
		if (first) {
			value = vec2<f32>(texel.r, textureSampleLevel(standIns, linearSampler, at, 0.0).r);
		}
		sum = sum + value * weight;
		total = total + weight;
	}
	return sum / max(total, 0.0001);
}

@fragment
fn fs_blur_first(in_: FullscreenOut) -> @location(0) vec4<f32> {
	return vec4<f32>(blur(in_.pos.xy, true), 0.0, 1.0);
}

@fragment
fn fs_blur_second(in_: FullscreenOut) -> @location(0) vec4<f32> {
	return vec4<f32>(blur(in_.pos.xy, false), 0.0, 1.0);
}

// The light's factor over a light map's texels (the lighting's apply pass multiplies the light by it once it is
// clamped, so lights brightening past full daylight cannot wash the shadows out): the soft shadows, each set's lighter
// over its own casters.
@fragment
fn fs_apply(in_: FullscreenOut) -> @location(0) vec4<f32> {
	let screen = in_.pos.xy * ao.screen.x;
	let uv = (screen + vec2<f32>(ao.screen.y)) / (ao.screen.zw + vec2<f32>(2.0 * ao.screen.y));
	let shadow = textureSampleLevel(source, linearSampler, uv, 0.0).rg;
	let ownSprite = textureSampleLevel(spriteSilhouettes, linearSampler, uv, 0.0).r;
	let ownStandIn = textureSampleLevel(standInSilhouettes, linearSampler, uv, 0.0).r;
	let sprites = shadow.r * mix(1.0, ao.shade.y, ownSprite);
	let standIns = shadow.g * mix(1.0, ao.shade.y, ownStandIn);
	// Two shadows over each other darken like one deeper one, never past black.
	let dark = clamp(1.0 - (1.0 - sprites * ao.shade.x) * (1.0 - standIns * ao.shade.x), 0.0, 1.0);
	let light = 1.0 - dark;
	return vec4<f32>(light, light, light, 1.0);
}
