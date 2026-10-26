struct Uniforms {
	viewport: vec4<f32>, // width, height
	camera: vec4<f32>, // x, y, zoom
	ambient: vec4<f32>, // rgb, max exposure
};

@group(0) @binding(0) var<uniform> u: Uniforms;
// Lights are three vec4s each: (position, radius, intensity), (colour, falloff), (direction, cone inner, cone outer).
// Glows are two: (position, radius, intensity), colour.
@group(0) @binding(1) var<storage, read> items: array<vec4<f32>>;

struct InstanceOut {
	@builtin(position) pos: vec4<f32>,
	// -1..1 across the instance's radius.
	@location(0) local: vec2<f32>,
	@location(1) @interpolate(flat) colour: vec4<f32>,
	@location(2) @interpolate(flat) cone: vec4<f32>,
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
	let a = items[instance * 3u];
	let b = items[instance * 3u + 1u];
	let c = items[instance * 3u + 2u];
	let local = corner(vertex);
	var out: InstanceOut;
	out.pos = to_clip(a.xy + local * a.z);
	out.local = local;
	out.colour = vec4<f32>(b.rgb * a.w, b.w);
	out.cone = c;
	return out;
}

@fragment
fn fs_light(in_: InstanceOut) -> @location(0) vec4<f32> {
	let d2 = dot(in_.local, in_.local);
	var attenuation = pow(max(1.0 - d2, 0.0), in_.colour.w);
	if (in_.cone.w > -1.0) {
		let len = sqrt(d2);
		let facing = select(1.0, dot(in_.local / max(len, 0.0001), in_.cone.xy), len > 0.0001);
		attenuation *= smoothstep(in_.cone.w, in_.cone.z, facing);
	}
	return vec4<f32>(in_.colour.rgb * attenuation, 0.0);
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

struct FullscreenOut {
	@builtin(position) pos: vec4<f32>,
};

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
