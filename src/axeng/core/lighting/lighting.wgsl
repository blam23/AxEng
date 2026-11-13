struct Uniforms {
	viewport: vec4<f32>, // width, height
	camera: vec4<f32>, // x, y, zoom
	ambient: vec4<f32>, // rgb, max exposure
	noiseTransform: vec4<f32>, // scale xy, offset xy
	noiseShape: vec4<f32>, // coverage, softness, darkness, seed
};

@group(0) @binding(0) var<uniform> u: Uniforms;
// Lights are four vec4s each: (position, radius, intensity), (colour, falloff), (direction, cone inner, cone outer),
// (length, reserved). Glows are two: (position, radius, intensity), colour.
@group(0) @binding(1) var<storage, read> items: array<vec4<f32>>;

struct InstanceOut {
	@builtin(position) pos: vec4<f32>,
	// -1..1 across the instance's radius; for a beam, along and across it in radii, x scaled by (half length + radius).
	@location(0) local: vec2<f32>,
	@location(1) @interpolate(flat) colour: vec4<f32>,
	@location(2) @interpolate(flat) cone: vec4<f32>,
	// A beam's half length in radii, and 1 for a beam (0 otherwise).
	@location(3) @interpolate(flat) beam: vec2<f32>,
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
	} else {
		out.pos = to_clip(a.xy + local * a.z);
		out.beam = vec2<f32>(0.0);
	}
	out.local = local;
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
	return vec4<f32>(in_.colour.rgb * attenuation, 0.0);
}

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

@vertex
fn vs_apply(@builtin(vertex_index) vertex: u32) -> FullscreenOut {
	var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
	var out: FullscreenOut;
	out.pos = vec4<f32>(positions[vertex], 0.0, 1.0);
	return out;
}

fn applied_light(position: vec2<f32>) -> vec3<f32> {
	let light = textureSampleLevel(lightMap, lightSampler, position / u.viewport.xy, 0.0).rgb;
	return min(light, vec3<f32>(u.ambient.w));
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
