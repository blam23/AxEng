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
	previous: vec4<f32>, // last frame's camera x, y, zoom; the cascades using the bilinear fix
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
// Per traced light, 16 times how far (texels) its own body reaches from it (see fs_combine); 0 for none. Written by the
// combine pass, read (without atomics, which cost the cascades dearly) through bodyReach.
@group(0) @binding(8) var<storage, read_write> bodies: array<atomic<u32>>;
@group(0) @binding(9) var<storage, read> bodyReach: array<u32>;

const PI: f32 = 3.14159265;
const TAU: f32 = 6.28318531;
const NO_SEED: u32 = 0xFFFFu;
const MAX_STEPS: u32 = 32u;
const HIT_DISTANCE: f32 = 0.75;
// Samples on the ring round an occluder texel that light it.
const INTERIOR_SAMPLES: i32 = 12;
// Texels a light's disc is drawn beyond its source. Probes within it take the light's analytic value, as inside it.
const DISC_MARGIN: f32 = 1.0;
// How far round a light's source what blocks is still its own body (times the source): a body the source sits inside,
// a square one's corners included. Its body then is the only surface it shows, shaped as drawn.
const OWN_SCALE: f32 = 1.75;
// How far a traced light's near part reaches: as radiance_near_scale and radiance_near_spacings.
const NEAR_SCALE: f32 = 4.0;
const NEAR_SPACINGS: f32 = 4.0;
// Softens how much more a near exit counts than a far one (texels).
const INTERIOR_FALLOFF: f32 = 2.0;
// Lights sharing one emitter disc that are added together, besides the one drawn last.
const MAX_COINCIDENT: u32 = 3u;

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
// Occluders and lights' discs are drawn into separate targets and combined: an occluder inside a light's disc (a lamp's
// own body) is that light's, alpha -(2 + index): it glows with the light and blocks every other. The rest of a disc is
// see-through (alpha 2 + index).

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
	// Out to what the light owns (see fs_combine), its source a texel wider (so rays only partly over its edge still
	// reach it, see hit_radiance) within.
	let core = source_radius(a.w) + DISC_MARGIN;
	let owned = max(source_radius(a.w) * OWN_SCALE, core) + DISC_MARGIN;
	let local = quad_corner(vertex);
	var out: InstanceOut;
	out.pos = grid_clip(a.xy + local * (owned / rc.probes.w));
	out.local = local;
	// The core's share of the radius; half floats hold whole numbers exactly up to 2048, so at most 2046 lights are
	// traced.
	out.albedo = vec4<f32>(core / owned, 0.0, 0.0, 2.0 + f32(instance));
	return out;
}

@fragment
fn fs_instance(in_: InstanceOut) -> @location(0) vec4<f32> {
	if (dot(in_.local, in_.local) > 1.0) {
		discard;
	}
	return in_.albedo;
}

// A light's disc: red 1 within its source (the core), 0 in the ring beyond out to what it owns.
@fragment
fn fs_emitter(in_: InstanceOut) -> @location(0) vec4<f32> {
	let d2 = dot(in_.local, in_.local);
	if (d2 > 1.0) {
		discard;
	}
	return vec4<f32>(select(0.0, 1.0, d2 <= in_.albedo.r * in_.albedo.r), 0.0, 0.0, in_.albedo.a);
}

// ---- Distance field: jump flooding from the solid texels that touch empty ones ----------------------------------

fn scene_alpha(p: vec2<i32>) -> f32 {
	let size = vec2<i32>(rc.grid.xy);
	if (any(p < vec2<i32>(0)) || any(p >= size)) {
		return 0.0;
	}
	return textureLoad(texA, p, 0).a;
}

// Occluders, lights' own bodies and lights' discs alike: the distance field leads rays to all of them.
fn solid(p: vec2<i32>) -> bool {
	return abs(scene_alpha(p)) > 0.5;
}

// The scene from the occluders (texA) and lights' discs (texB). An occluder inside a disc is that light's own body; how
// far the farthest of its texels lies from the light goes in bodies, so rays crossing the disc know where to look
// for it.
@fragment
fn fs_combine(in_: FullscreenOut) -> @location(0) vec4<f32> {
	let p = vec2<i32>(in_.pos.xy);
	let occluder = textureLoad(texA, p, 0);
	let disc = textureLoad(texB, p, 0);
	if (disc.a > 1.5) {
		// What blocks within the light's reach is its own body, corners and all: it glows with the light and blocks
		// every other, shaped as drawn.
		if (occluder.a > 0.5) {
			let index = u32(disc.a - 1.5);
			let centre = (items[index * 3u].xy - rc.grid.zw) * rc.probes.w;
			let reach = length(vec2<f32>(p) + vec2<f32>(0.5) - centre) + 1.0;
			atomicMax(&bodies[index], u32(reach * 16.0));
			return vec4<f32>(occluder.rgb, -disc.a);
		}
		// The source is see-through where nothing covers it (a light with no body); the ring beyond is empty.
		if (disc.r > 0.5) {
			return vec4<f32>(0.0, 0.0, 0.0, disc.a);
		}
	}
	return occluder;
}

// Then each see-through disc texel takes its light's body reach in red and, in green, how far inside the disc's edge the
// body begins (huge with no body): rays crossing it have them with the alpha they load anyway, and marched lights
// (lighting.wgsl) soften their shadows by the distance to the body rather than to the see-through disc round it. Its
// colour is not needed there.
@fragment
fn fs_stamp(in_: FullscreenOut) -> @location(0) vec4<f32> {
	let scene = textureLoad(texA, vec2<i32>(in_.pos.xy), 0);
	if (scene.a > 1.5) {
		let index = u32(scene.a - 1.5);
		let reach = f32(bodyReach[index]) / 16.0;
		let drawn = source_radius(items[index * 3u].w) + DISC_MARGIN;
		return vec4<f32>(reach, select(10000.0, max(drawn - reach, 0.0), reach > 0.0), scene.b, scene.a);
	}
	return scene;
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

// The radiance of traced light `light` (its first vec4's index) towards a probe at origin (texels).
fn light_radiance(light: u32, origin: vec2<f32>, dir: vec2<f32>) -> vec3<f32> {
	let k = rc.probes.w;
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
		// The angle from the light along the ray back towards the probe, not to the probe itself: rays keep their
		// directions as the light moves (and far out are finely spaced), while the probes far out are coarse, so the
		// cone's edge, taken at them, would wobble.
		let facing = select(1.0, dot(-dir, c.xy), d > 0.0001);
		attenuation *= mix(1.0, smoothstep(c.w, c.z, facing), smoothstep(0.0, 0.12, r));
	}
	// The share of directions that see the source disc is asin(source / d) / pi. Scaling by its inverse, a probe
	// that sees the whole disc gets the light's analytic value and a partly hidden one gets that share of it.
	// Inside the disc every ray hits at once.
	let compensation = select(PI / asin(min(source / max(d, 0.0001), 1.0)), 1.0, d <= source + DISC_MARGIN);
	// Near the source the light is drawn plainly (radiance_near_part); the cascades fade in beyond.
	let reach = min(source * NEAR_SCALE + NEAR_SPACINGS * rc.probes.z, radius);
	return b.rgb * attenuation * compensation * smoothstep(source, reach, d);
}

// The radiance leaving a solid texel towards a probe at origin (texels).
fn hit_radiance(p: vec2<i32>, at: vec2<f32>, origin: vec2<f32>, dir: vec2<f32>) -> vec3<f32> {
	let scene = textureLoad(texB, p, 0);
	if (abs(scene.a) > 1.5) {
		// The last of consecutive lights with the same position and source radius covers the others' discs exactly
		// (a lamp and its cone), so they shine through it together.
		var light = u32(abs(scene.a) - 1.5) * 3u;
		let disc = items[light];
		// How much of the ray's wedge (its share of the probe's directions) covers the true disc, not the texels it was
		// drawn into: as a light moves, rays then catch it gradually instead of all at once, which would flicker.
		let centre = (disc.xy - rc.grid.zw) * rc.probes.w;
		let source = source_radius(disc.w);
		// The ray's wedge is as wide where it meets the disc as its cascade's angle between rays at that distance, so
		// summed over the rays the coverage is the disc's width (rays passing just outside reach the wider texels).
		let d = length(centre - origin);
		// How close the ray passes the centre ahead of it (behind it, how far it already is).
		let rel = centre - at;
		let across = select(length(rel), abs(rel.x * dir.y - rel.y * dir.x), dot(rel, dir) >= 0.0);
		let wedge = max(d * TAU / f32(4u << (2u * u32(rc.cascade.x))), 1.0);
		let coverage = select(clamp((source - across) / wedge + 0.5, 0.0, 1.0), 1.0, d <= source + DISC_MARGIN);
		if (coverage <= 0.0) {
			return vec3<f32>(0.0);
		}
		var total = light_radiance(light, origin, dir);
		for (var i = 0u; i < MAX_COINCIDENT && light >= 3u; i++) {
			light -= 3u;
			let other = items[light];
			if (any(other.xy != disc.xy) || other.w != disc.w) {
				break;
			}
			total += light_radiance(light, origin, dir);
		}
		return total * coverage;
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

// How far a ray inside a see-through disc may step: to the disc's far side, unless its way passes within the reach of
// the light's own body (body, see fs_combine), where it walks a texel at a time so as not to step over the body.
fn disc_step(kind: f32, body: f32, at: vec2<f32>, dir: vec2<f32>) -> f32 {
	let index = u32(kind - 1.5);
	let disc = items[index * 3u];
	let local = at - (disc.xy - rc.grid.zw) * rc.probes.w;
	let drawn = source_radius(disc.w) + DISC_MARGIN;
	let b = dot(local, dir);
	let leave = max(-b + sqrt(max(b * b - dot(local, local) + drawn * drawn, 0.0)) + 0.5, 0.5);
	if (body <= 0.0) {
		return leave;
	}
	let c = dot(local, local) - body * body;
	if (c <= 0.0) {
		return min(1.0, leave);
	}
	let h = b * b - c;
	if (b >= 0.0 || h <= 0.0) {
		return leave;
	}
	return max(min(-b - sqrt(h) + 0.25, leave), 0.5);
}

// The light along dir from begin for reach texels (rgb), and whether no occluder blocked it (a). Lights' discs do not
// block: a ray takes a disc's light and carries on through it, so lamps do not shadow each other's light (or the sky),
// but a light's own body inside its disc glows with its light and blocks the ray. A ray that begins inside a disc does
// not take its light, which the interval before it took on its way in. Emitters are measured from the probe.
fn trace(probe: vec2<f32>, begin: vec2<f32>, dir: vec2<f32>, reach: f32) -> vec4<f32> {
	let size = rc.grid.xy;
	var t = 0.0;
	var gained = vec3<f32>(0.0);
	// The disc last taken (its scene alpha), so a ray passing along and into one takes it once.
	var taken = -1.0;
	for (var i = 0u; i < MAX_STEPS; i++) {
		let at = begin + dir * t;
		if (any(at < vec2<f32>(0.0)) || any(at >= size)) {
			break;
		}
		let p = vec2<i32>(at);
		let packed = textureLoad(texU, p, 0).r;
		let distance = unpack2x16float(packed).x;
		var step = distance;
		if (distance < 0.0) {
			let here = textureLoad(texB, p, 0);
			let kind = here.a;
			if (abs(kind) < 1.5) {
				return vec4<f32>(gained + hit_radiance(p, at, probe, dir), 0.0);
			}
			// A light's own body glows with it but blocks the ray.
			if (kind < 0.0) {
				if (-kind != taken && i > 0u) {
					gained += hit_radiance(p, at, probe, dir);
				}
				return vec4<f32>(gained, 0.0);
			}
			if (kind != taken && i > 0u) {
				gained += hit_radiance(p, at, probe, dir);
			}
			taken = kind;
			step = disc_step(kind, here.r, at, dir);
		} else if (distance < HIT_DISTANCE) {
			// Grazing rays would step past a light's disc; one passing this close, heading towards it, takes it from the
			// nearest of its texels. Occluders are left to the trace: rays skimming past them towards a light would
			// otherwise be caught too, speckling shadow along their sides. Those skim on a texel at a time, not half.
			let offset = vec2<i32>(i32((packed >> 16u) & 0xFFu), i32(packed >> 24u)) - vec2<i32>(128);
			if (dot(normalize(vec2<f32>(offset) + vec2<f32>(0.0001)), dir) > -0.35) {
				let kind = textureLoad(texB, p + offset, 0).a;
				if (kind > 1.5 && kind != taken && i > 0u) {
					gained += hit_radiance(p + offset, at, probe, dir);
					taken = kind;
				}
				step = 1.0;
			}
		}
		t += step;
		if (t >= reach) {
			break;
		}
	}
	return vec4<f32>(gained, 1.0);
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
	// A probe inside an occluder (or a light's own body) is never used (the resolve and the cascade below leave such
	// probes out): not traced.
	let inside = scene_at(origin).a;
	if ((inside > 0.5 && inside < 1.5) || inside < -1.5) {
		return vec4<f32>(0.0);
	}
	let rays = f32(4u << (2u * c));
	let scale = f32(1u << (2u * c));
	let start = rc.cascade.z * (scale - 1.0) / 3.0;
	let end = rc.cascade.z * (scale * 4.0 - 1.0) / 3.0;
	// The next cascade's four nearest probes and their bilinear weights.
	let up = (vec2<f32>(probe) + 0.5) * 0.5 - vec2<f32>(0.5);
	let base = vec2<i32>(floor(up));
	let f = up - floor(up);
	// The bilinear fix only in the lowest cascades, where its rings and leaks show; above, the light is far and soft, and
	// its four rays per direction (on the longest intervals) would cost the most.
	let bilinearFix = !top && c < u32(rc.previous.w + 0.5);
	// With the bilinear fix, the weights of the four upper probes, those inside an occluder left out: rays aimed at
	// them run into it, which would darken the probes beside every occluder.
	var weights = vec4<f32>(0.0);
	if (bilinearFix) {
		for (var b = 0; b < 4; b++) {
			let offset = vec2<i32>(b & 1, b >> 1);
			let w = select(1.0 - f, f, vec2<bool>(offset.x == 1, offset.y == 1));
			let inSolid = scene_at((vec2<f32>(base + offset) + 0.5) * spacing * 2.0).a;
			weights[b] = select(w.x * w.y, 0.0, abs(inSolid) > 0.5);
		}
	}
	let total = weights.x + weights.y + weights.z + weights.w;
	var sum = vec4<f32>(0.0);
	for (var j = 0u; j < 4u; j++) {
		let index = stored * 4u + j;
		let angle = (f32(index) + 0.5) * TAU / rays;
		let dir = vec2<f32>(cos(angle), sin(angle));
		let begin = origin + dir * start;
		if (bilinearFix && total > 0.0) {
			// The bilinear fix: one ray to where each of the four upper probes' intervals begin, merged with that probe,
			// so the light continues without the parallax between the cascades' probes (which makes rings).
			var ray = vec4<f32>(0.0);
			for (var b = 0; b < 4; b++) {
				if (weights[b] <= 0.0) {
					continue;
				}
				let q = base + vec2<i32>(b & 1, b >> 1);
				let upperOrigin = (vec2<f32>(q) + 0.5) * spacing * 2.0;
				let segment = upperOrigin + dir * end - begin;
				let span = max(length(segment), 0.0001);
				var near = trace(origin, begin, segment / span, span);
				if (near.a > 0.0) {
					near = merge(near, upper_at(q, index, c));
				}
				ray += near * weights[b];
			}
			sum += ray / total;
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
		// Probes inside occluders are dark, and those inside a light's disc see only that light (whose light near its
		// source is drawn plainly anyway), so neither counts.
		let at = scene_at((vec2<f32>(probe) + 0.5) * s0).a;
		if (!anySolid && abs(at) > 0.5) {
			weight *= 0.0001;
		}
		sum += textureLoad(texF, probe, 0) * weight;
		total += weight;
	}
	return sum / max(total, 1e-8);
}

// The signed distance to the nearest surface at g (texels), bilinear between texel centres so it changes smoothly
// from pixel to pixel: about -0.5 on a solid's edge texels and 0.5 on the empty texels beside them.
fn smooth_distance(g: vec2<f32>) -> f32 {
	let size = vec2<i32>(rc.grid.xy) - vec2<i32>(1);
	let q = g - vec2<f32>(0.5);
	let base = vec2<i32>(floor(q));
	let f = q - floor(q);
	let d00 = distance_at(clamp(base, vec2<i32>(0), size));
	let d10 = distance_at(clamp(base + vec2<i32>(1, 0), vec2<i32>(0), size));
	let d01 = distance_at(clamp(base + vec2<i32>(0, 1), vec2<i32>(0), size));
	let d11 = distance_at(clamp(base + vec2<i32>(1, 1), vec2<i32>(0), size));
	return mix(mix(d00, d10, f.x), mix(d01, d11, f.x), f.y);
}

// The share of gather's bilinear weight at g on probes outside solids.
fn open_share(g: vec2<f32>) -> f32 {
	let s0 = rc.probes.z;
	let probes = vec2<i32>(rc.probes.xy);
	let q = g / s0 - vec2<f32>(0.5);
	let base = floor(q);
	let f = q - base;
	var open = 0.0;
	for (var i = 0; i < 4; i++) {
		let offset = vec2<i32>(i & 1, i >> 1);
		let probe = clamp(vec2<i32>(base) + offset, vec2<i32>(0), probes - vec2<i32>(1));
		let w = select(1.0 - f, f, vec2<bool>(offset.x == 1, offset.y == 1));
		let at = scene_at((vec2<f32>(probe) + 0.5) * s0).a;
		if (abs(at) <= 0.5) {
			open += w.x * w.y;
		}
	}
	return open;
}

// An occluder texel takes the light just outside it: in each of a ring of directions, the light just past where the
// occluder ends that way, the nearer exits counting most: seen from above, a building's top catches the light falling
// on its lit side, while the edge on its far side stays dark. Exits move smoothly from texel to texel (unlike the
// distance to the nearest surface, which creases along an occluder's middle), so the light does too.
fn interior(g: vec2<f32>) -> vec4<f32> {
	var sum = vec4<f32>(0.0);
	var count = 0.0;
	for (var i = 0; i < INTERIOR_SAMPLES; i++) {
		let angle = f32(i) * (TAU / f32(INTERIOR_SAMPLES));
		let dir = vec2<f32>(cos(angle), sin(angle));
		// March out of the solid: inside, the distance to the nearest surface is a safe step.
		var t = 0.0;
		for (var step = 0; step < 12; step++) {
			let d = smooth_distance(g + dir * t);
			if (d >= 0.0) {
				break;
			}
			t += max(-d, 0.5);
		}
		// Far enough out that the probes sampled there are clear of the surface.
		let at = g + dir * (t + 1.0 + rc.probes.z);
		if (any(at < vec2<f32>(0.0)) || any(at >= rc.grid.xy)) {
			continue;
		}
		let outside = smoothstep(0.0, 1.5, smooth_distance(at));
		if (outside <= 0.0) {
			continue;
		}
		let weight = outside / ((t + INTERIOR_FALLOFF) * (t + INTERIOR_FALLOFF));
		sum += gather(at, false) * weight;
		count += weight;
	}
	if (count > 0.0) {
		return sum / count;
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
	// Under a light's see-through disc, the probes there too; a light's own body is lit like any occluder's inside.
	if (scene.a > 1.5) {
		light = gather(g, true);
	} else {
		// How far inside an occluder g is, blended across its edge texel so the edge is not a staircase of texels.
		let d = smooth_distance(g);
		let inside = clamp(0.5 - d, 0.0, 1.0);
		// The open probes' light counts only as much as there are open probes round g: just inside an edge they may
		// all be in the solid, and dark.
		let open = (1.0 - inside) * open_share(g);
		light = vec4<f32>(0.0);
		if (open < 1.0) {
			light = interior(g) * (1.0 - open);
		}
		if (open > 0.0) {
			light += gather(g, false) * open;
		}
	}
	if (debugView == 1u) {
		// A light's disc in its colour, its own body (which blocks other light) half red.
		if (scene.a > 1.5) {
			let colour = items[u32(scene.a - 1.5) * 3u + 1u].rgb;
			return vec4<f32>(colour / max(max(colour.r, max(colour.g, colour.b)), 0.0001), 1.0);
		}
		if (scene.a < -1.5) {
			return vec4<f32>(mix(scene.rgb, vec3<f32>(1.0, 0.25, 0.25), 0.5), 1.0);
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
	// No sky occlusion beside a light's own body or disc: a lamp's head lit by its own light is not a dark corner.
	let packed = textureLoad(texU, vec2<i32>(clamp(g, vec2<f32>(0.0), rc.grid.xy - vec2<f32>(1.0))), 0).r;
	let offset = vec2<i32>(i32((packed >> 16u) & 0xFFu), i32(packed >> 24u)) - vec2<i32>(128);
	let nearest = select(vec2<i32>(g) + offset, vec2<i32>(g), unpack2x16float(packed).x < 0.0);
	let byLight = abs(scene_at(vec2<f32>(nearest) + vec2<f32>(0.5)).a) > 1.5;
	return vec4<f32>(rc.ambient.rgb * mix(1.0, light.a, select(rc.params.z, 0.0, byLight)) + light.rgb, 1.0);
}
