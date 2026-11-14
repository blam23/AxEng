// Radiance cascades: see radiance_cascades.h and docs/lighting.md.
//
// The GI grid is width x height texels; cascade 0 has a probe every s0 texels. Cascade c has probes 2^c times further
// apart, casts 4^(c+1) rays per probe and stores 4^c pre-averaged directions, direction first: the texture is split
// into 2^c x 2^c blocks, one per stored direction, each holding every probe. So every cascade is the same size.

struct Rc {
	grid: vec4<f32>, // width, height (texels), world origin x, y
	probes: vec4<f32>, // probe columns, rows (cascade 0), spacing s0 (texels), texels per world pixel
	cascade: vec4<f32>, // index, count, interval 0 (texels), last cascade that keeps its visibility (sky occlusion)
	params: vec4<f32>, // jump step, bounce, ao strength, debug view
	ambient: vec4<f32>, // rgb, 1 when last frame's light map is bound
	view: vec4<f32>, // camera x, y, zoom, light map divisor
	viewport: vec4<f32>, // viewport width, height; last frame's viewport width, height
	previous: vec4<f32>, // last frame's camera x, y, zoom; 1 for the bilinear fix
};

@group(0) @binding(0) var<uniform> rc: Rc;
@group(0) @binding(1) var texA: texture_2d<f32>;
@group(0) @binding(2) var texB: texture_2d<f32>;
@group(0) @binding(3) var texU: texture_2d<u32>;
@group(0) @binding(4) var texF: texture_2d<f32>;
@group(0) @binding(5) var samp: sampler;
// Circles are two vec4s each: (centre, radius, -), albedo. Lights are three: (position, radius, source radius),
// (colour * intensity, falloff), (direction, cone inner, cone outer). All in world pixels.
@group(0) @binding(6) var<storage, read> items: array<vec4<f32>>;
@group(0) @binding(7) var texE: texture_2d<f32>;

const PI: f32 = 3.14159265;
const TAU: f32 = 6.28318531;
const NO_SEED: u32 = 0xFFFFu;
const MAX_STEPS: u32 = 40u;
const HIT_DISTANCE: f32 = 0.75;
const SEEN_MARGIN: f32 = 1.0;

// Sources smaller than a few probe spacings fall between the rays, so none is smaller than this (texels).
fn source_radius(worldRadius: f32) -> f32 {
	return max(worldRadius * rc.probes.w, 2.5 * rc.probes.z);
}

struct FullscreenOut {
	@builtin(position) pos: vec4<f32>,
};

@vertex
fn vs_full(@builtin(vertex_index) vertex: u32) -> FullscreenOut {
	var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
	var out: FullscreenOut;
	out.pos = vec4<f32>(positions[vertex], 0.0, 1.0);
	return out;
}

// ---- Scene: occluders (albedo, alpha 1) and emitters (colour, alpha 2 + light index) ----------------------------

struct InstanceOut {
	@builtin(position) pos: vec4<f32>,
	@location(0) local: vec2<f32>,
	@location(1) @interpolate(flat) albedo: vec4<f32>,
};

fn grid_clip(world: vec2<f32>) -> vec4<f32> {
	let texel = (world - rc.grid.zw) * rc.probes.w;
	return vec4<f32>(texel.x / rc.grid.x * 2.0 - 1.0, 1.0 - texel.y / rc.grid.y * 2.0, 0.0, 1.0);
}

fn quad_corner(vertex: u32) -> vec2<f32> {
	var corners = array<vec2<f32>, 6>(vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, -1.0), vec2<f32>(1.0, 1.0),
		vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, 1.0), vec2<f32>(-1.0, 1.0));
	return corners[vertex];
}

@vertex
fn vs_circle(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> InstanceOut {
	let a = items[instance * 2u];
	// Half a texel bigger, so circles smaller than a texel still cover their centre.
	let radius = a.z + 0.5 / rc.probes.w;
	let local = quad_corner(vertex);
	var out: InstanceOut;
	out.pos = grid_clip(a.xy + local * radius);
	out.local = local;
	out.albedo = vec4<f32>(items[instance * 2u + 1u].rgb, 1.0);
	return out;
}

@vertex
fn vs_emitter(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> InstanceOut {
	let a = items[instance * 3u];
	let b = items[instance * 3u + 1u];
	let radius = source_radius(a.w) / rc.probes.w;
	let local = quad_corner(vertex);
	var out: InstanceOut;
	out.pos = grid_clip(a.xy + local * radius);
	out.local = local;
	// Half floats hold whole numbers exactly up to 2048, so at most 2046 lights are traced.
	out.albedo = vec4<f32>(b.rgb / max(max(b.r, max(b.g, b.b)), 0.0001), 2.0 + f32(instance));
	return out;
}

@fragment
fn fs_instance(in_: InstanceOut) -> @location(0) vec4<f32> {
	if (dot(in_.local, in_.local) > 1.0) {
		discard;
	}
	return in_.albedo;
}

// ---- Distance field: jump flooding from the solid texels that touch empty ones ----------------------------------

fn solid(p: vec2<i32>) -> bool {
	let size = vec2<i32>(rc.grid.xy);
	if (any(p < vec2<i32>(0)) || any(p >= size)) {
		return false;
	}
	return textureLoad(texA, p, 0).a > 0.5;
}

@fragment
fn fs_seed(in_: FullscreenOut) -> @location(0) vec4<u32> {
	let p = vec2<i32>(in_.pos.xy);
	if (solid(p) && (!solid(p + vec2<i32>(1, 0)) || !solid(p - vec2<i32>(1, 0))
		|| !solid(p + vec2<i32>(0, 1)) || !solid(p - vec2<i32>(0, 1)))) {
		return vec4<u32>(u32(p.x), u32(p.y), 0u, 0u);
	}
	return vec4<u32>(NO_SEED, NO_SEED, 0u, 0u);
}

@fragment
fn fs_jump(in_: FullscreenOut) -> @location(0) vec4<u32> {
	let p = vec2<i32>(in_.pos.xy);
	let size = vec2<i32>(rc.grid.xy);
	let step = i32(rc.params.x);
	var best = vec2<u32>(NO_SEED, NO_SEED);
	var bestDistance = 1e20;
	for (var y = -1; y <= 1; y++) {
		for (var x = -1; x <= 1; x++) {
			let q = p + vec2<i32>(x, y) * step;
			if (any(q < vec2<i32>(0)) || any(q >= size)) {
				continue;
			}
			let seed = textureLoad(texU, q, 0).xy;
			if (seed.x == NO_SEED) {
				continue;
			}
			let delta = vec2<f32>(seed) - vec2<f32>(p);
			let distance = dot(delta, delta);
			if (distance < bestDistance) {
				bestDistance = distance;
				best = seed;
			}
		}
	}
	return vec4<u32>(best, 0u, 0u);
}

// Texels to the nearest surface (positive outside, at least 0.5; negative inside solids) as a half float in the low
// 16 bits, and the offset to the nearest surface texel in the high 16 (x + 128 | y + 128 << 8, while within reach).
@fragment
fn fs_distance(in_: FullscreenOut) -> @location(0) vec4<u32> {
	let p = vec2<i32>(in_.pos.xy);
	let seed = textureLoad(texU, p, 0).xy;
	var distance = 30000.0;
	var offset = vec2<i32>(0);
	if (seed.x != NO_SEED) {
		offset = vec2<i32>(seed) - p;
		distance = length(vec2<f32>(offset));
	}
	var signed = max(distance - 0.5, 0.5);
	if (solid(p)) {
		signed = -(distance + 0.5);
	}
	let near = vec2<u32>(clamp(offset, vec2<i32>(-127), vec2<i32>(127)) + vec2<i32>(128));
	return vec4<u32>((pack2x16float(vec2<f32>(signed, 0.0)) & 0xFFFFu) | (near.x << 16u) | (near.y << 24u), 0u, 0u, 0u);
}

fn distance_at(p: vec2<i32>) -> f32 {
	return unpack2x16float(textureLoad(texU, p, 0).r).x;
}

// ---- Cascades ---------------------------------------------------------------------------------------------------

fn to_world(texel: vec2<f32>) -> vec2<f32> {
	return texel / rc.probes.w + rc.grid.zw;
}

// The radiance leaving a solid texel towards a probe at origin (texels).
fn hit_radiance(p: vec2<i32>, at: vec2<f32>, origin: vec2<f32>, dir: vec2<f32>) -> vec3<f32> {
	let scene = textureLoad(texB, p, 0);
	let k = rc.probes.w;
	if (scene.a > 1.5) {
		let light = u32(scene.a - 1.5) * 3u;
		let a = items[light];
		let b = items[light + 1u];
		let c = items[light + 2u];
		let toProbe = origin - (a.xy - rc.grid.zw) * k;
		let d = length(toProbe);
		let radius = max(a.z * k, 0.0001);
		let source = source_radius(a.w);
		let r = d / radius;
		var attenuation = pow(max(1.0 - r * r, 0.0), b.w);
		if (c.w > -1.0) {
			let facing = select(1.0, dot(toProbe / max(d, 0.0001), c.xy), d > 0.0001);
			attenuation *= mix(1.0, smoothstep(c.w, c.z, facing), smoothstep(0.0, 0.12, r));
		}
		// The share of directions that see the source disc is asin(source / d) / pi. Scaling by its inverse, a probe
		// that sees the whole disc gets the light's analytic value and a partly hidden one gets that share of it.
		// Rays stop within HIT_DISTANCE of a solid texel, so the disc they see is wider than its texel centres reach.
		let seen = source + SEEN_MARGIN;
		// Inside the disc every ray hits at once.
		let compensation = select(PI / asin(min(seen / max(d, 0.0001), 1.0)), 1.0, d <= source);
		return b.rgb * attenuation * compensation;
	}
	if (rc.params.y > 0.0 && rc.ambient.w > 0.5) {
		// Bounce: the albedo times last frame's light just in front of the surface.
		let world = to_world(at - dir);
		let screen = (world - rc.previous.xy) * rc.previous.z + rc.viewport.zw * 0.5;
		let uv = screen / rc.viewport.zw;
		if (all(uv >= vec2<f32>(0.0)) && all(uv <= vec2<f32>(1.0))) {
			let light = textureSampleLevel(texE, samp, uv, 0.0).rgb;
			return scene.rgb * light * rc.params.y;
		}
	}
	return vec3<f32>(0.0);
}

// The light along dir from begin for reach texels (rgb), and whether nothing blocked it (a). Emitters are measured
// from the probe.
fn trace(probe: vec2<f32>, begin: vec2<f32>, dir: vec2<f32>, reach: f32) -> vec4<f32> {
	let size = rc.grid.xy;
	var t = 0.0;
	for (var i = 0u; i < MAX_STEPS; i++) {
		let at = begin + dir * t;
		if (any(at < vec2<f32>(0.0)) || any(at >= size)) {
			break;
		}
		let p = vec2<i32>(at);
		let packed = textureLoad(texU, p, 0).r;
		let distance = unpack2x16float(packed).x;
		if (distance < 0.0) {
			return vec4<f32>(hit_radiance(p, at, probe, dir), 0.0);
		}
		// Grazing rays would step past a surface's edge texels; one passing this close hits the nearest of them.
		if (distance < HIT_DISTANCE) {
			let surface = p + vec2<i32>(i32((packed >> 16u) & 0xFFu), i32(packed >> 24u)) - vec2<i32>(128);
			return vec4<f32>(hit_radiance(surface, at, probe, dir), 0.0);
		}
		t += distance;
		if (t >= reach) {
			break;
		}
	}
	return vec4<f32>(0.0, 0.0, 0.0, 1.0);
}

// The next cascade's pre-averaged direction index at this probe's position, bilinear between its four nearest
// probes, never filtering across into a neighbouring direction's block.
fn upper(probe: vec2<u32>, index: u32, c: u32) -> vec4<f32> {
	let probes = vec2<u32>(rc.probes.xy);
	let blocks = 2u << c;
	let blockSize = vec2<f32>(probes / blocks);
	let block = vec2<f32>(f32(index % blocks), f32(index / blocks));
	let at = clamp((vec2<f32>(probe) + 0.5) * 0.5, vec2<f32>(0.5), blockSize - vec2<f32>(0.5));
	return textureSampleLevel(texF, samp, (block * blockSize + at) / vec2<f32>(probes), 0.0);
}

// One direction of the next cascade at one of its probes (clamped to the grid).
fn upper_at(q: vec2<i32>, index: u32, c: u32) -> vec4<f32> {
	let blocks = 2u << c;
	let blockSize = vec2<i32>(vec2<u32>(rc.probes.xy) / blocks);
	let block = vec2<i32>(i32(index % blocks), i32(index / blocks));
	return textureLoad(texF, block * blockSize + clamp(q, vec2<i32>(0), blockSize - vec2<i32>(1)), 0);
}

fn merge(near: vec4<f32>, far: vec4<f32>) -> vec4<f32> {
	return vec4<f32>(near.rgb + near.a * far.rgb, near.a * far.a);
}

@fragment
fn fs_cascade(in_: FullscreenOut) -> @location(0) vec4<f32> {
	let texel = vec2<u32>(in_.pos.xy);
	let c = u32(rc.cascade.x);
	let top = c + 1u >= u32(rc.cascade.y);
	let blocks = 1u << c;
	let blockSize = vec2<u32>(rc.probes.xy) / blocks;
	let block = texel / blockSize;
	let probe = texel % blockSize;
	let stored = block.x + block.y * blocks;
	let spacing = rc.probes.z * f32(blocks);
	let origin = (vec2<f32>(probe) + 0.5) * spacing;
	let rays = f32(4u << (2u * c));
	let scale = f32(1u << (2u * c));
	let start = rc.cascade.z * (scale - 1.0) / 3.0;
	let end = rc.cascade.z * (scale * 4.0 - 1.0) / 3.0;
	// The next cascade's four nearest probes and their bilinear weights.
	let up = (vec2<f32>(probe) + 0.5) * 0.5 - vec2<f32>(0.5);
	let base = vec2<i32>(floor(up));
	let f = up - floor(up);
	let bilinearFix = rc.previous.w > 0.5 && !top;
	var sum = vec4<f32>(0.0);
	for (var j = 0u; j < 4u; j++) {
		let index = stored * 4u + j;
		let angle = (f32(index) + 0.5) * TAU / rays;
		let dir = vec2<f32>(cos(angle), sin(angle));
		let begin = origin + dir * start;
		if (bilinearFix) {
			// The bilinear fix: one ray to where each of the four upper probes' intervals begin, merged with that probe,
			// so the light continues without the parallax between the cascades' probes (which makes rings).
			var ray = vec4<f32>(0.0);
			for (var b = 0; b < 4; b++) {
				let offset = vec2<i32>(b & 1, b >> 1);
				let w = select(1.0 - f, f, vec2<bool>(offset.x == 1, offset.y == 1));
				let q = base + offset;
				let upperOrigin = (vec2<f32>(q) + 0.5) * spacing * 2.0;
				let segment = upperOrigin + dir * end - begin;
				let span = max(length(segment), 0.0001);
				var near = trace(origin, begin, segment / span, span);
				if (near.a > 0.0) {
					near = merge(near, upper_at(q, index, c));
				}
				ray += near * (w.x * w.y);
			}
			sum += ray;
		} else {
			var ray = trace(origin, begin, dir, end - start);
			if (ray.a > 0.0 && !top) {
				ray = merge(ray, upper(probe, index, c));
			}
			sum += ray;
		}
	}
	sum *= 0.25;
	// Beyond the sky occlusion range nothing counts as blocking the sky.
	if (f32(c) > rc.cascade.w) {
		sum.a = 1.0;
	}
	return sum;
}

// ---- Resolve into the light map ---------------------------------------------------------------------------------

fn scene_at(g: vec2<f32>) -> vec4<f32> {
	return textureLoad(texB, vec2<i32>(clamp(g, vec2<f32>(0.0), rc.grid.xy - vec2<f32>(1.0))), 0);
}

// Cascade 0 at g (texels), bilinear between the four nearest probes; unless anySolid, probes inside solids only count
// when every probe is.
fn gather(g: vec2<f32>, anySolid: bool) -> vec4<f32> {
	let s0 = rc.probes.z;
	let probes = vec2<i32>(rc.probes.xy);
	let q = g / s0 - vec2<f32>(0.5);
	let base = floor(q);
	let f = q - base;
	var sum = vec4<f32>(0.0);
	var total = 0.0;
	for (var i = 0; i < 4; i++) {
		let offset = vec2<i32>(i & 1, i >> 1);
		let probe = clamp(vec2<i32>(base) + offset, vec2<i32>(0), probes - vec2<i32>(1));
		let w = select(1.0 - f, f, vec2<bool>(offset.x == 1, offset.y == 1));
		var weight = w.x * w.y;
		if (!anySolid && scene_at((vec2<f32>(probe) + 0.5) * s0).a > 0.5) {
			weight *= 0.0001;
		}
		sum += textureLoad(texF, probe, 0) * weight;
		total += weight;
	}
	return sum / max(total, 1e-8);
}

// An occluder texel takes the light just outside it: the mean of samples on a ring a little beyond its depth that
// land outside every solid. The side of a building facing a light is lit, the far side dark.
fn interior(g: vec2<f32>, depth: f32) -> vec4<f32> {
	var radius = depth + 1.5;
	for (var attempt = 0; attempt < 3; attempt++) {
		var sum = vec4<f32>(0.0);
		var count = 0.0;
		for (var i = 0; i < 8; i++) {
			let angle = f32(i) * (TAU / 8.0);
			let at = g + vec2<f32>(cos(angle), sin(angle)) * radius;
			if (any(at < vec2<f32>(0.0)) || any(at >= rc.grid.xy) || scene_at(at).a > 0.5) {
				continue;
			}
			sum += gather(at, false);
			count += 1.0;
		}
		if (count > 0.0) {
			return sum / count;
		}
		radius = radius * 2.0 + 2.0;
	}
	return vec4<f32>(0.0, 0.0, 0.0, 1.0);
}

@fragment
fn fs_resolve(in_: FullscreenOut) -> @location(0) vec4<f32> {
	let screen = in_.pos.xy * rc.view.w;
	let world = (screen - rc.viewport.xy * 0.5) / rc.view.z + rc.view.xy;
	let g = (world - rc.grid.zw) * rc.probes.w;
	let debugView = u32(rc.params.w);
	if (any(g < vec2<f32>(0.0)) || any(g >= rc.grid.xy)) {
		return vec4<f32>(select(rc.ambient.rgb, vec3<f32>(0.0), debugView == 3u), 1.0);
	}
	let scene = scene_at(g);
	var light: vec4<f32>;
	if (scene.a > 0.5 && scene.a < 1.5) {
		light = interior(g, -distance_at(vec2<i32>(g)));
	} else {
		light = gather(g, scene.a > 1.5);
	}
	if (debugView == 1u) {
		if (scene.a > 1.5) {
			return vec4<f32>(scene.rgb, 1.0);
		}
		return vec4<f32>(select(vec3<f32>(1.0), vec3<f32>(1.0, 0.25, 0.25), scene.a > 0.5), 1.0);
	}
	if (debugView == 2u) {
		let distance = distance_at(vec2<i32>(g));
		let shade = clamp(abs(distance) / 32.0, 0.0, 1.0);
		return vec4<f32>(select(vec3<f32>(shade), vec3<f32>(shade, 0.0, 0.0), distance < 0.0), 1.0);
	}
	if (debugView == 3u) {
		return vec4<f32>(light.rgb, 1.0);
	}
	if (debugView == 4u) {
		return vec4<f32>(vec3<f32>(light.a), 1.0);
	}
	return vec4<f32>(rc.ambient.rgb * mix(1.0, light.a, rc.params.z) + light.rgb, 1.0);
}
