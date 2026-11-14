struct Uniforms {
	viewport: vec4<f32>, // width, height
	camera: vec4<f32>, // x, y, zoom
	ambient: vec4<f32>, // rgb, max exposure
	noiseTransform: vec4<f32>, // scale xy, offset xy
	noiseShape: vec4<f32>, // coverage, softness, darkness, seed
	tonemap: vec4<f32>, // curve (0 clamp, 1 reinhard, 2 aces, 3 filmic), exposure, white, saturation
	tonemapExtra: vec4<f32>, // gamma, preserve hue (1)
};

@group(0) @binding(0) var<uniform> u: Uniforms;
// Lights are four vec4s each: (position, radius, intensity), (colour, falloff), (direction, cone inner, cone outer),
// (length, source radius, shadows, near reach). Glows are two: (position, radius, intensity), colour.
@group(0) @binding(1) var<storage, read> items: array<vec4<f32>>;

// Lights with marched shadows (shadows 2) march through the radiance cascades' distance field: signed texels to the
// nearest surface as a half float in the low 16 bits, the offset to that surface texel in the high 16 (see
// radiance.wgsl), and the scene, whose alpha tells occluders (1, or below -1.5 for a light's own body) from lights'
// see-through discs (2 and above).
struct March {
	grid: vec4<f32>, // world origin xy, texels per world pixel, 1 when there is a distance field
	size: vec4<f32>, // width, height (texels)
};
@group(1) @binding(0) var<uniform> march: March;
@group(1) @binding(1) var marchDistance: texture_2d<u32>;
@group(1) @binding(2) var marchScene: texture_2d<f32>;

struct InstanceOut {
	@builtin(position) pos: vec4<f32>,
	// -1..1 across the instance's radius; for a beam, along and across it in radii, x scaled by (half length + radius).
	@location(0) local: vec2<f32>,
	@location(1) @interpolate(flat) colour: vec4<f32>,
	@location(2) @interpolate(flat) cone: vec4<f32>,
	// A beam's half length in radii, and 1 for a beam (0 otherwise).
	@location(3) @interpolate(flat) beam: vec2<f32>,
	// A traced light's near part (see fs_light): where it starts fading and where it is gone, in radii; 0, 0 otherwise.
	@location(4) @interpolate(flat) near: vec2<f32>,
	// A light with marched shadows: its position and radius (world pixels) and source radius + 1; all 0 otherwise.
	@location(5) @interpolate(flat) marched: vec4<f32>,
};

struct FullscreenOut {
	@builtin(position) pos: vec4<f32>,
};

// Instances are octagons around the unit circle, a fan of six triangles (18 vertices): about 17% fewer pixels than a
// square, all of which fall outside the light.
fn corner(index: u32) -> vec2<f32> {
	let fan = index / 3u;
	let point = index % 3u;
	let k = select(fan + point, 0u, point == 0u);
	let angle = (f32(k) + 0.5) * 0.78539816;
	// 1 / cos(pi / 8): the octagon's sides touch the circle.
	return vec2<f32>(cos(angle), sin(angle)) * 1.0823922;
}

fn to_clip(world: vec2<f32>) -> vec4<f32> {
	let screen = (world - u.camera.xy) * u.camera.z + u.viewport.xy * 0.5;
	return vec4<f32>(screen.x / u.viewport.x * 2.0 - 1.0, 1.0 - screen.y / u.viewport.y * 2.0, 0.0, 1.0);
}

@vertex
fn vs_light(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> InstanceOut {
	let a = items[instance * 4u];
	let b = items[instance * 4u + 1u];
	let c = items[instance * 4u + 2u];
	let e = items[instance * 4u + 3u];
	var local = corner(vertex);
	var out: InstanceOut;
	out.colour = vec4<f32>(b.rgb * a.w, b.w);
	out.cone = c;
	if (e.x > 0.0) {
		// A beam: the octagon stretched over the capsule round the line, half its length plus the radius each way
		// along it and the radius across. Grown by sqrt(2), the stretched octagon covers the capsule's ends too.
		local = local * 1.4142136;
		let half = e.x * 0.5;
		let across = vec2<f32>(-c.y, c.x);
		out.pos = to_clip(a.xy + c.xy * half + c.xy * (local.x * (half + a.z)) + across * (local.y * a.z));
		out.beam = vec2<f32>(half / max(a.z, 0.0001), 1.0);
		out.local = local;
	} else {
		// A near part only reaches nearReach: its octagon shrinks to that, local staying in radii.
		let reach = select(a.z, min(a.z, e.w), e.w > 0.0);
		out.pos = to_clip(a.xy + local * reach);
		out.beam = vec2<f32>(0.0);
		out.local = local * (reach / max(a.z, 0.0001));
	}
	out.near = select(vec2<f32>(0.0), vec2<f32>(e.y, e.w) / max(a.z, 0.0001), e.w > 0.0);
	out.marched = select(vec4<f32>(0.0), vec4<f32>(a.xy, a.z, e.y + 1.0), e.z >= 1.5 && e.x <= 0.0);
	return out;
}

@fragment
fn fs_light(in_: InstanceOut) -> @location(0) vec4<f32> {
	if (in_.beam.y > 0.5) {
		// The distance to the line, in radii.
		let along = abs(in_.local.x * (in_.beam.x + 1.0));
		let beyond = max(along - in_.beam.x, 0.0);
		let d2 = beyond * beyond + in_.local.y * in_.local.y;
		return vec4<f32>(in_.colour.rgb * pow(max(1.0 - d2, 0.0), in_.colour.w), 0.0);
	}
	let d2 = dot(in_.local, in_.local);
	var attenuation = pow(max(1.0 - d2, 0.0), in_.colour.w);
	if (in_.cone.w > -1.0) {
		let len = sqrt(d2);
		let facing = select(1.0, dot(in_.local / max(len, 0.0001), in_.cone.xy), len > 0.0001);
		// Right at the apex the angle is meaningless and the cone's edges sweep across whatever the light sits on, so
		// a cone light is a plain light there and only becomes a cone further out.
		let cone = smoothstep(in_.cone.w, in_.cone.z, facing);
		attenuation *= mix(1.0, cone, smoothstep(0.0, 0.12, len));
	}
	if (in_.near.y > 0.0) {
		// The near part of a traced light, the cascades taking over beyond (near_weight in radiance.wgsl).
		attenuation *= 1.0 - smoothstep(in_.near.x, in_.near.y, sqrt(d2));
	}
	if (in_.marched.w > 0.0 && attenuation > 0.0) {
		attenuation *= marched_visibility(in_.marched.xy + in_.local * in_.marched.z, in_.marched.xy, in_.marched.w - 1.0);
	}
	return vec4<f32>(in_.colour.rgb * attenuation, 0.0);
}

// The distance field at a texel: x signed texels to the nearest surface, safe to step by; y the distance that shades,
// to an occluder or a light's own body; z 1 inside a light's see-through disc. A see-through disc does not block, but
// the body inside it does: its texels hold how far in from the disc's edge the body begins (radiance.wgsl, fs_stamp),
// so y is the distance to the body, as if round.
fn march_texel(p: vec2<i32>) -> vec3<f32> {
	let size = vec2<i32>(march.size.xy);
	if (any(p < vec2<i32>(0)) || any(p >= size)) {
		return vec3<f32>(1000.0, 1000.0, 0.0);
	}
	let packed = textureLoad(marchDistance, p, 0).r;
	let d = unpack2x16float(packed & 0xFFFFu).x;
	let offset = vec2<i32>(i32((packed >> 16u) & 0xFFu), i32(packed >> 24u)) - vec2<i32>(128);
	let surface = select(clamp(p + offset, vec2<i32>(0), size - vec2<i32>(1)), p, d < 0.0);
	let scene = textureLoad(marchScene, surface, 0);
	if (scene.a > 1.5) {
		return vec3<f32>(d, d + scene.g, select(0.0, 1.0, d < 0.0));
	}
	return vec3<f32>(d, d, 0.0);
}

// The distance at g, blended between texel centres near surfaces (where it sets a penumbra) so shadows are not stepped.
fn march_distance(g: vec2<f32>) -> vec3<f32> {
	let nearest = march_texel(vec2<i32>(floor(g)));
	if (abs(nearest.x) > 3.0 && abs(nearest.y) > 3.0) {
		return nearest;
	}
	let q = g - vec2<f32>(0.5);
	let base = vec2<i32>(floor(q));
	let f = q - floor(q);
	let d00 = march_texel(base).xy;
	let d10 = march_texel(base + vec2<i32>(1, 0)).xy;
	let d01 = march_texel(base + vec2<i32>(0, 1)).xy;
	let d11 = march_texel(base + vec2<i32>(1, 1)).xy;
	return vec3<f32>(mix(mix(d00, d10, f.x), mix(d01, d11, f.x), f.y), nearest.z);
}

// How much of a light's source (world pixels) a point sees past the occluders: a soft shadow marched along the way to
// it through the distance field, its penumbra as wide as the source subtends. Lights' discs do not block. A point
// inside an occluder takes the light where it leaves it towards the light, fading the deeper it is, so a building's
// top is lit on the light's side.
fn marched_visibility(world: vec2<f32>, light: vec2<f32>, source: f32) -> f32 {
	if (march.grid.w < 0.5) {
		return 1.0;
	}
	let k = march.grid.z;
	let g = (world - march.grid.xy) * k;
	let l = (light - march.grid.xy) * k;
	if (any(g < vec2<f32>(0.0)) || any(g >= march.size.xy)) {
		return 1.0;
	}
	let toLight = l - g;
	let dist = length(toLight);
	let sourceTexels = max(source * k, 1.0);
	let dir = toLight / dist;
	let spread = sourceTexels / dist;
	// The march stops this far short of the light: where a traced light's own body may reach (OWN_SCALE times its source
	// in radiance.wgsl) and a margin, so that body does not shade the light's own near part.
	let stop = sourceTexels * MARCH_OWN_SCALE + MARCH_SOURCE_MARGIN;
	if (dist <= stop) {
		return 1.0;
	}
	var t = 0.0;
	var inside = 1.0;
	var here = march_distance(g);
	if (here.y < 0.0) {
		for (var i = 0; i < 16; i++) {
			here = march_distance(g + dir * t);
			if (here.y >= 0.0) {
				break;
			}
			t += max(-here.y, 0.5);
		}
		inside = 1.0 - smoothstep(0.0, max(MARCH_INTERIOR_DEPTH * k, 2.0), t);
		if (inside <= 0.0) {
			return 0.0;
		}
	}
	t += 0.5;
	// -1 deep in shadow to 1 fully seen; the geometric shadow's edge at 0.
	var seen = 1.0;
	for (var i = 0; i < MARCH_STEPS; i++) {
		if (t >= dist - stop) {
			break;
		}
		let s = march_distance(g + dir * t);
		// What lies within the light's own source (its own body) does not shade it.
		if (dist - t - max(s.y, 0.0) >= stop) {
			seen = min(seen, s.y / (spread * t));
		}
		if (seen <= -1.0) {
			break;
		}
		// Inside a see-through disc only the body inside it can block, so on by the distance to that.
		t += clamp(select(s.x, s.y, s.z > 0.5), 0.5, 64.0);
	}
	seen = max(seen, -1.0);
	return inside * 0.25 * (1.0 + seen) * (1.0 + seen) * (2.0 - seen);
}

const MARCH_STEPS: i32 = 64;
// Texels past a light's source its march stops at: a body's corners just inside the source poke out about this far
// once drawn on the grid.
const MARCH_SOURCE_MARGIN: f32 = 2.0;
// As OWN_SCALE in radiance.wgsl.
const MARCH_OWN_SCALE: f32 = 1.75;
// World pixels into an occluder over which a marched light on its top fades out.
const MARCH_INTERIOR_DEPTH: f32 = 16.0;

// Occluders are two vec4s of corners, drawn as two triangles: 0 1 2 and 0 2 3.
@vertex
fn vs_occluder(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> FullscreenOut {
	var order = array<u32, 6>(0u, 1u, 2u, 0u, 2u, 3u);
	let k = order[vertex];
	let pair = items[instance * 2u + k / 2u];
	let corner = select(pair.xy, pair.zw, (k % 2u) == 1u);
	var out: FullscreenOut;
	out.pos = to_clip(corner);
	return out;
}

// Back to ambient, hiding the lights already drawn beneath.
@fragment
fn fs_occluder(in_: FullscreenOut) -> @location(0) vec4<f32> {
	return vec4<f32>(u.ambient.rgb, 1.0);
}

fn hash21(p : vec2<f32>) -> f32 {
	let x = bitcast<u32>(i32(p.x));
	let y = bitcast<u32>(i32(p.y));
	var h = x ^ (y + 0x9e3779b9u + (x << 6u) + (x >> 2u));
	h = (h ^ (h >> 16u)) * 0x7feb352du;
	h = (h ^ (h >> 15u)) * 0x846ca68bu;
	h = h ^ (h >> 16u);
	return f32(h >> 8u) * (1.0 / 16777216.0);
}

fn value_noise(p : vec2<f32>) -> f32 {
	let i = floor(p);
	let f = fract(p);
	let u = f * f * (3.0 - 2.0 * f);
	return mix(
		mix(hash21(i), hash21(i + vec2<f32>(1.0, 0.0)), u.x),
		mix(hash21(i + vec2<f32>(0.0, 1.0)), hash21(i + vec2<f32>(1.0, 1.0)), u.x),
		u.y);
}

fn fbm(p : vec2<f32>) -> f32 {
	var sum = 0.0;
	var amplitude = 0.5;
	var q = p;
	for (var i = 0; i < 4; i = i + 1) {
		sum = sum + value_noise(q) * amplitude;
		q = q * 2.03 + vec2<f32>(17.1, 9.7);
		amplitude = amplitude * 0.5;
	}
	return sum / 0.9375;
}

@vertex
fn vs_shadow(@builtin(vertex_index) vertex: u32) -> FullscreenOut {
	var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
	var out: FullscreenOut;
	out.pos = vec4<f32>(positions[vertex], 0.0, 1.0);
	return out;
}

@fragment
fn fs_shadow(in_: FullscreenOut) -> @location(0) vec4<f32> {
	let screen = in_.pos.xy * max(u.camera.w, 1.0);
	let world = (screen - u.viewport.xy * 0.5) / u.camera.z + u.camera.xy;
	let noiseUv = world / max(u.noiseTransform.xy, vec2<f32>(1.0)) + u.noiseTransform.zw
		+ vec2<f32>(u.noiseShape.w * 0.73, u.noiseShape.w * 1.17);
	let noise = fbm(noiseUv);
	let cloud = smoothstep(u.noiseShape.x, u.noiseShape.x + max(u.noiseShape.y, 0.001), noise);
	let light = 1.0 - cloud * u.noiseShape.z;
	return vec4<f32>(light, light, light, 1.0);
}

@vertex
fn vs_glow(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> InstanceOut {
	let a = items[instance * 2u];
	let colour = items[instance * 2u + 1u];
	let local = corner(vertex);
	var out: InstanceOut;
	out.pos = to_clip(a.xy + local * a.z);
	out.local = local;
	out.colour = vec4<f32>(colour.rgb * colour.a * a.w, 0.0);
	out.cone = vec4<f32>(0.0);
	out.beam = vec2<f32>(0.0);
	return out;
}

@fragment
fn fs_glow(in_: InstanceOut) -> @location(0) vec4<f32> {
	let d = length(in_.local);
	let halo = pow(max(1.0 - d, 0.0), 2.0);
	let core = 1.0 - smoothstep(0.0, 0.3, d);
	return vec4<f32>(in_.colour.rgb * (halo * 0.7 + core * 0.5), 0.0);
}

@group(0) @binding(1) var lightMap: texture_2d<f32>;
@group(0) @binding(2) var lightSampler: sampler;
// The ambient occlusion's factor over the light map (red; white without it), applied once the light is clamped.
@group(0) @binding(3) var aoFactor: texture_2d<f32>;

@vertex
fn vs_apply(@builtin(vertex_index) vertex: u32) -> FullscreenOut {
	var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
	var out: FullscreenOut;
	out.pos = vec4<f32>(positions[vertex], 0.0, 1.0);
	return out;
}

fn aces_curve(x: vec3<f32>) -> vec3<f32> {
	return clamp(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14), vec3<f32>(0.0), vec3<f32>(1.0));
}

fn hable_curve(x: vec3<f32>) -> vec3<f32> {
	let a = 0.15; let b = 0.5; let c = 0.1; let d = 0.2; let e = 0.02; let f = 0.3;
	return (x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f) - e / f;
}

// Light (0 up) curved into 0..1 (keep in step with curve_channel in light_renderer.cpp).
fn curve(x_: vec3<f32>) -> vec3<f32> {
	let x = max(x_, vec3<f32>(0.0));
	let kind = u32(u.tonemap.x + 0.5);
	let white = u.tonemap.z;
	var c: vec3<f32>;
	if (kind == 1u) {
		c = x * (1.0 + x / (white * white)) / (1.0 + x);
	} else if (kind == 2u) {
		c = aces_curve(x) / max(aces_curve(vec3<f32>(white)).x, 0.0001);
	} else if (kind == 3u) {
		c = hable_curve(x) / max(hable_curve(vec3<f32>(white)).x, 0.0001);
	} else {
		c = select(vec3<f32>(0.0), x / u.ambient.w, u.ambient.w > 0.0);
	}
	return clamp(c, vec3<f32>(0.0), vec3<f32>(1.0));
}

// See Tonemap in light_renderer.h.
fn tonemap(light: vec3<f32>) -> vec3<f32> {
	var x = max(light * u.tonemap.y, vec3<f32>(0.0));
	if (u.tonemap.w != 1.0) {
		let luma = dot(x, vec3<f32>(0.2126, 0.7152, 0.0722));
		x = max(mix(vec3<f32>(luma), x, u.tonemap.w), vec3<f32>(0.0));
	}
	var c: vec3<f32>;
	if (u.tonemapExtra.y > 0.5) {
		let peak = max(x.r, max(x.g, x.b));
		c = select(vec3<f32>(0.0), x * (curve(vec3<f32>(peak)).x / peak), peak > 0.0);
	} else {
		c = curve(x);
	}
	if (u.tonemapExtra.x != 1.0) {
		c = pow(c, vec3<f32>(u.tonemapExtra.x));
	}
	return c * u.ambient.w;
}

fn applied_light(position: vec2<f32>) -> vec3<f32> {
	let uv = position / u.viewport.xy;
	let light = textureSampleLevel(lightMap, lightSampler, uv, 0.0).rgb;
	return tonemap(light) * textureSampleLevel(aoFactor, lightSampler, uv, 0.0).r;
}

// The target times the light, for exposures up to 1.
@fragment
fn fs_apply(in_: FullscreenOut) -> @location(0) vec4<f32> {
	return vec4<f32>(applied_light(in_.pos.xy), 1.0);
}

// For exposures above 1: halved, and the blend doubles it back (see LightRenderer::init). It costs a bit of the
// light's precision on 8-bit targets, so it is only used when lights may brighten.
@fragment
fn fs_apply_bright(in_: FullscreenOut) -> @location(0) vec4<f32> {
	return vec4<f32>(applied_light(in_.pos.xy) * 0.5, 0.5);
}
