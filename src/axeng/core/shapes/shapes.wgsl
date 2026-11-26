struct View {
	viewport: vec4<f32>,
	camera: vec4<f32>,
	// A light that shades raised shapes as they are drawn (ShapeRenderer::set_relief_light): its direction (towards
	// the light, z up out of the screen) and strength, 0 for none.
	light: vec4<f32>,
};

@group(0) @binding(0) var<uniform> view : View;

// A shape's surface (ShapeSurface), as ShapeRenderer packs it.
struct Surface {
	kinds: vec4<u32>, // paint, pattern, gradient stop count, geometry kind
	colours: array<vec4<f32>, 4>,
	stops: vec4<f32>,
	gradient: vec4<f32>, // from xy, to xy
	pattern: vec4<f32>, // scale, rotation, seed, contrast
	values: vec4<f32>,
	relief: vec4<f32>, // height, bevel, profile, blend
	relief2: vec4<f32>, // bump, roughness, emissive, radial gradient radius
	frame: vec4<f32>, // the shape's own bounds: min xy, size xy (shape coordinates)
	geometry: vec4<f32>, // per geometry kind (see edge_distance)
	extra: vec4<f32>, // unit, first point, point count, half thickness (segments)
};

@group(1) @binding(0) var<storage, read> surfaces : array<Surface>;
@group(1) @binding(1) var<storage, read> points : array<vec2<f32>>;

const NO_SURFACE : u32 = 0xFFFFFFFFu;
// Set on an outline's vertices: it keeps its colour, but stands up with the shape.
const OUTLINE_FLAG : u32 = 0x40000000u;
const INDEX_MASK : u32 = 0x3FFFFFFFu;

const PAINT_LINEAR : u32 = 1u;
const PAINT_RADIAL : u32 = 2u;
const PAINT_PATTERN : u32 = 3u;

struct VSIn {
	@location(0) pos: vec2<f32>,
	@location(1) z: f32,
	@location(2) screen_space: u32,
	@location(3) colour: vec4<f32>,
	@location(4) clip_rect: vec4<f32>,
	@location(5) local: vec2<f32>,
	@location(6) surface: u32,
};

struct VSOut {
	@builtin(position) pos: vec4<f32>,
	@location(0) colour: vec4<f32>,
	@location(1) clip_rect: vec4<f32>,
	@location(2) local: vec2<f32>,
	@location(3) @interpolate(flat) surface: u32,
};

@vertex
fn vs_main(in_: VSIn) -> VSOut {
	// Same transform and depth mapping as the sprite shader so shapes and sprites interleave by z.
	let screenPos = select(
		(in_.pos - view.camera.xy) * view.camera.z + view.viewport.xy * 0.5,
		in_.pos,
		in_.screen_space != 0u
	);
	let ndcX = (screenPos.x / view.viewport.x) * 2.0 - 1.0;
	let ndcY = 1.0 - (screenPos.y / view.viewport.y) * 2.0;
	let depth = 0.5 + atan(in_.z) / 3.141592653589793;

	var out: VSOut;
	out.pos = vec4<f32>(ndcX, ndcY, depth, 1.0);
	out.colour = in_.colour;
	out.clip_rect = in_.clip_rect;
	out.local = in_.local;
	out.surface = in_.surface;
	return out;
}

fn clipped(in_: VSOut) -> bool {
	return in_.clip_rect.z >= in_.clip_rect.x && in_.clip_rect.w >= in_.clip_rect.y
		&& (in_.pos.x < in_.clip_rect.x || in_.pos.y < in_.clip_rect.y
			|| in_.pos.x >= in_.clip_rect.z || in_.pos.y >= in_.clip_rect.w);
}

//------------------------------------------------------------------------------
// Patterns: functions of a position in pattern cells (surface units turned and divided by the pattern's scale), its
// four values and seed, giving how far to mix from the first colour to the second (0..1).
//------------------------------------------------------------------------------
fn pattern_hash(p: vec2<f32>, seed: f32) -> f32 {
	let x = bitcast<u32>(i32(floor(p.x)));
	let y = bitcast<u32>(i32(floor(p.y)));
	var h = x ^ (y * 0x9e3779b9u + 0x7f4a7c15u + bitcast<u32>(i32(seed * 977.0)));
	h = (h ^ (h >> 16u)) * 0x7feb352du;
	h = (h ^ (h >> 15u)) * 0x846ca68bu;
	h = h ^ (h >> 16u);
	return f32(h >> 8u) * (1.0 / 16777216.0);
}

fn pattern_hash2(p: vec2<f32>, seed: f32) -> vec2<f32> {
	return vec2<f32>(pattern_hash(p, seed), pattern_hash(p + vec2<f32>(37.0, 91.0), seed + 13.0));
}

fn pattern_noise(p: vec2<f32>, seed: f32) -> f32 {
	let i = floor(p);
	let f = fract(p);
	let u = f * f * (3.0 - 2.0 * f);
	return mix(
		mix(pattern_hash(i, seed), pattern_hash(i + vec2<f32>(1.0, 0.0), seed), u.x),
		mix(pattern_hash(i + vec2<f32>(0.0, 1.0), seed), pattern_hash(i + vec2<f32>(1.0, 1.0), seed), u.x),
		u.y);
}

// Value noise repeating every `period` cells (a whole number), so a tile of it meets its neighbours seamlessly.
fn pattern_noise_wrapped(p: vec2<f32>, seed: f32, period: f32) -> f32 {
	let i = floor(p);
	let f = fract(p);
	let u = f * f * (3.0 - 2.0 * f);
	let a = i - period * floor(i / period);
	let b = (i + vec2<f32>(1.0)) - period * floor((i + vec2<f32>(1.0)) / period);
	return mix(
		mix(pattern_hash(a, seed), pattern_hash(vec2<f32>(b.x, a.y), seed), u.x),
		mix(pattern_hash(vec2<f32>(a.x, b.y), seed), pattern_hash(b, seed), u.x),
		u.y);
}

// fbm repeating every `period` cells: each octave doubles the frequency and the period with it.
fn pattern_fbm_wrapped(p: vec2<f32>, seed: f32, octaves: i32, period: f32) -> f32 {
	var sum = 0.0;
	var amplitude = 0.5;
	var total = 0.0;
	var q = p;
	var repeat = period;
	for (var i = 0; i < octaves; i = i + 1) {
		sum = sum + pattern_noise_wrapped(q, seed + f32(i) * 7.0, repeat) * amplitude;
		total = total + amplitude;
		q = q * 2.0;
		repeat = repeat * 2.0;
		amplitude = amplitude * 0.5;
	}
	return sum / total;
}

fn pattern_fbm(p: vec2<f32>, seed: f32, octaves: i32) -> f32 {
	var sum = 0.0;
	var amplitude = 0.5;
	var total = 0.0;
	var q = p;
	for (var i = 0; i < octaves; i = i + 1) {
		sum = sum + pattern_noise(q, seed + f32(i) * 7.0) * amplitude;
		total = total + amplitude;
		q = q * 2.03 + vec2<f32>(17.1, 9.7);
		amplitude = amplitude * 0.5;
	}
	return sum / total;
}

// Distance to the nearest of one jittered point per cell.
fn pattern_cells(p: vec2<f32>, seed: f32, jitter: f32) -> f32 {
	let i = floor(p);
	let f = fract(p);
	var best = 8.0;
	for (var y = -1; y <= 1; y = y + 1) {
		for (var x = -1; x <= 1; x = x + 1) {
			let cell = vec2<f32>(f32(x), f32(y));
			let point = cell + vec2<f32>(0.5) + (pattern_hash2(i + cell, seed) - vec2<f32>(0.5)) * jitter;
			best = min(best, length(point - f));
		}
	}
	return best;
}

// Smooth step over about a pixel (aa: pattern cells per pixel) either side of an edge at 0.
fn pattern_edge(distance: f32, aa: f32) -> f32 {
	return clamp(0.5 - distance / max(aa, 1e-5), 0.0, 1.0);
}

// Built-in patterns, by id:
//   1 value noise      2 fbm (values.x octaves, 4 if 0; values.y repeats it every that many cells, for seamless tiles)
//   3 cells (values.x jitter, 1 if 0)
//   4 stripes (values.x the second colour's share)          5 checker
//   6 grid lines (values.x line width in cells)             7 dots (values.x radius in cells)
//   8 brushed (values.x stretch, 12 if 0)                   9 bricks (values.x mortar width in cells)
//   10 tread plate (values.x ridge length)                  11 corrugation (a sine across x)
//   12 blotches: patches of the second colour over the first, with specks (values.x coverage, 0.35 if 0)
//   13 panels: staggered plates with seams (values.x seam width, values.y rivet radius, 0 for none)
//   14 hexagons (values.x line width)                       15 rivets: rows of dots, every other row shifted
// Ids from 64 go to user_pattern (ShapeRenderer::set_pattern_source), with the same arguments.
fn builtin_pattern(id: u32, p: vec2<f32>, values: vec4<f32>, seed: f32, aa: f32) -> f32 {
	switch id {
		case 1u: { return pattern_noise(p, seed); }
		case 2u: {
			let octaves = select(i32(values.x), 4, values.x <= 0.0);
			if (values.y >= 1.0) {
				return pattern_fbm_wrapped(p, seed, clamp(octaves, 1, 8), floor(values.y));
			}
			return pattern_fbm(p, seed, clamp(octaves, 1, 8));
		}
		case 3u: { return clamp(pattern_cells(p, seed, select(values.x, 1.0, values.x <= 0.0)), 0.0, 1.0); }
		case 4u: {
			let share = select(values.x, 0.5, values.x <= 0.0);
			let f = fract(p.x);
			return pattern_edge(abs(f - share * 0.5) - share * 0.5, aa);
		}
		case 5u: {
			let c = floor(p);
			return select(0.0, 1.0, (i32(c.x) + i32(c.y)) % 2 != 0);
		}
		case 6u: {
			let width = select(values.x, 0.06, values.x <= 0.0) * 0.5;
			let f = abs(fract(p + vec2<f32>(0.5)) - vec2<f32>(0.5));
			return pattern_edge(min(f.x, f.y) - width, aa);
		}
		case 7u: {
			let radius = select(values.x, 0.25, values.x <= 0.0);
			return pattern_edge(length(fract(p) - vec2<f32>(0.5)) - radius, aa);
		}
		case 8u: {
			let stretch = select(values.x, 12.0, values.x <= 0.0);
			return pattern_fbm(vec2<f32>(p.x / stretch, p.y * 4.0), seed, 3);
		}
		case 9u: {
			let mortar = select(values.x, 0.06, values.x <= 0.0) * 0.5;
			let row = floor(p.y);
			let q = vec2<f32>(p.x + select(0.0, 0.5, i32(row) % 2 != 0), p.y * 2.0);
			let f = abs(fract(q + vec2<f32>(0.5)) - vec2<f32>(0.5));
			return pattern_edge(min(f.x, f.y * 0.5) - mortar, aa);
		}
		case 10u: {
			let length_ = select(values.x, 0.35, values.x <= 0.0);
			let c = floor(p);
			let f = fract(p) - vec2<f32>(0.5);
			// Ridges alternate between the two diagonals, as on tread plate.
			let turn = select(vec2<f32>(0.7071, 0.7071), vec2<f32>(0.7071, -0.7071), (i32(c.x) + i32(c.y)) % 2 != 0);
			let along = dot(f, turn);
			let across = dot(f, vec2<f32>(-turn.y, turn.x));
			let d = length(vec2<f32>(max(abs(along) - length_ * 0.5, 0.0), across)) - 0.07;
			return pattern_edge(d, aa);
		}
		case 11u: { return 0.5 + 0.5 * sin(p.x * 6.2831853); }
		case 12u: {
			let coverage = select(values.x, 0.35, values.x <= 0.0);
			let blotch = pattern_fbm(p, seed, 4) + (pattern_noise(p * 9.0, seed + 5.0) - 0.5) * 0.18;
			return pattern_edge((1.0 - coverage) - blotch, aa * 0.5);
		}
		case 13u: {
			let seam = select(values.x, 0.04, values.x <= 0.0) * 0.5;
			let row = floor(p.y);
			let q = vec2<f32>(p.x * 0.5 + select(0.0, 0.5, i32(row) % 2 != 0), p.y);
			let f = abs(fract(q + vec2<f32>(0.5)) - vec2<f32>(0.5));
			var mark = pattern_edge(min(f.x * 2.0, f.y) - seam, aa);
			if (values.y > 0.0) {
				// A rivet in from each plate's corners.
				let c = fract(q) - vec2<f32>(0.5);
				let corner = abs(c) - vec2<f32>(0.5 - 0.09, 0.5 - 0.15);
				mark = max(mark, pattern_edge(length(vec2<f32>(corner.x * 2.0, corner.y)) - values.y, aa));
			}
			return mark;
		}
		case 14u: {
			let width = select(values.x, 0.06, values.x <= 0.0) * 0.5;
			let r = vec2<f32>(1.0, 1.7320508);
			let h = r * 0.5;
			let a = (p - r * floor(p / r)) - h;
			let b = ((p - h) - r * floor((p - h) / r)) - h;
			let g = select(b, a, dot(a, a) < dot(b, b));
			let edge = 0.5 - max(abs(g.x), dot(abs(g), normalize(vec2<f32>(1.0, 1.7320508))));
			return pattern_edge(edge - width, aa);
		}
		case 15u: {
			let radius = select(values.x, 0.12, values.x <= 0.0);
			let row = floor(p.y);
			let q = vec2<f32>(p.x + select(0.0, 0.5, i32(row) % 2 != 0), p.y);
			return pattern_edge(length(fract(q) - vec2<f32>(0.5)) - radius, aa);
		}
		default: { return user_pattern(id, p, values, seed, aa); }
	}
}

// footprint: shape coordinates per pixel. Patterns too fine to see fade to an even mix rather than shimmer.
fn shape_pattern(index: u32, local: vec2<f32>, footprint: f32) -> f32 {
	let unit = max(surfaces[index].extra.x, 1e-6);
	let settings = surfaces[index].pattern;
	let c = cos(settings.y);
	let s = sin(settings.y);
	let q = local / unit;
	let cell = max(settings.x, 1e-6);
	let turned = vec2<f32>(q.x * c + q.y * s, -q.x * s + q.y * c) / cell;
	let aa = footprint / unit / cell;
	let value = builtin_pattern(surfaces[index].kinds.y, turned, surfaces[index].values, settings.z, aa);
	let even = mix(value, 0.5, smoothstep(0.35, 1.0, aa));
	return clamp(0.5 + (even - 0.5) * settings.w, 0.0, 1.0);
}

fn gradient_colour(index: u32, t: f32) -> vec4<f32> {
	let count = clamp(surfaces[index].kinds.z, 1u, 4u);
	let stops = surfaces[index].stops;
	var colour = surfaces[index].colours[0];
	if (t <= stops[0]) {
		return colour;
	}
	for (var i = 1u; i < count; i = i + 1u) {
		let a = stops[i - 1u];
		let b = stops[i];
		if (t <= b) {
			let k = select((t - a) / (b - a), 1.0, b <= a);
			return mix(surfaces[index].colours[i - 1u], surfaces[index].colours[i], k);
		}
		colour = surfaces[index].colours[i];
	}
	return colour;
}

fn surface_colour(index: u32, local: vec2<f32>, fill: vec4<f32>, footprint: f32) -> vec4<f32> {
	let paint = surfaces[index].kinds.x;
	let frame = surfaces[index].frame;
	let gradient = surfaces[index].gradient;
	if (paint == PAINT_LINEAR) {
		let uv = (local - frame.xy) / max(frame.zw, vec2<f32>(1e-6));
		let d = gradient.zw - gradient.xy;
		let t = dot(uv - gradient.xy, d) / max(dot(d, d), 1e-8);
		return gradient_colour(index, t);
	}
	if (paint == PAINT_RADIAL) {
		let centre = frame.xy + gradient.xy * frame.zw;
		let radius = max(surfaces[index].relief2.w * max(frame.z, frame.w), 1e-6);
		return gradient_colour(index, length(local - centre) / radius);
	}
	if (paint == PAINT_PATTERN) {
		let colour = mix(surfaces[index].colours[0], surfaces[index].colours[1], shape_pattern(index, local, footprint));
		// Its ramp (stops.xy): brightness from `from` to `to` across the shape's bounds.
		let uv = (local - frame.xy) / max(frame.zw, vec2<f32>(1e-6));
		let d = gradient.zw - gradient.xy;
		let t = clamp(dot(uv - gradient.xy, d) / max(dot(d, d), 1e-8), 0.0, 1.0);
		let ramp = mix(surfaces[index].stops.x, surfaces[index].stops.y, t);
		return vec4<f32>(clamp(colour.rgb * ramp, vec3<f32>(0.0), vec3<f32>(1.0)), colour.a);
	}
	return fill;
}

@fragment
fn fs_main(in_: VSOut) -> @location(0) vec4<f32> {
	// Before any branch, so the derivatives are taken in uniform control flow.
	let footprint = max(fwidth(in_.local.x), fwidth(in_.local.y));
	let stepX = dpdx(in_.local);
	let stepY = dpdy(in_.local);
	let hasSurface = in_.surface != NO_SURFACE;
	let index = in_.surface & INDEX_MASK;
	// With a relief light, the shape's own height shades it: its slope across a pixel gives the normal.
	var shade = 1.0;
	if (view.light.w > 0.0 && hasSurface) {
		let height = surface_height(index, in_.local, footprint);
		let slope = vec2<f32>(surface_height(index, in_.local + stepX, footprint) - height,
			surface_height(index, in_.local + stepY, footprint) - height);
		shade = relief_shade(slope, view.light);
	}
	if (clipped(in_)) {
		discard;
	}
	if (!hasSurface || (in_.surface & OUTLINE_FLAG) != 0u) {
		return vec4<f32>(in_.colour.rgb * shade, in_.colour.a);
	}
	let colour = surface_colour(index, in_.local, in_.colour, footprint);
	return vec4<f32>(colour.rgb * shade, colour.a);
}

// How bright a surface of this slope (height per pixel) is under the light, 1 for a flat one.
fn relief_shade(slope: vec2<f32>, light: vec4<f32>) -> f32 {
	let normal = normalize(vec3<f32>(-slope * 2.0, 1.0));
	let towards = normalize(light.xyz);
	let lit = max(dot(normal, towards), 0.0) / max(towards.z, 0.05);
	return mix(1.0, lit, clamp(light.w, 0.0, 1.0));
}

//------------------------------------------------------------------------------
// Height: how far inside its edge a point lies, and the profile that turns that into height.
//------------------------------------------------------------------------------
fn segment_distance(p: vec2<f32>, a: vec2<f32>, b: vec2<f32>) -> f32 {
	let ab = b - a;
	let t = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-12), 0.0, 1.0);
	return length(p - (a + ab * t));
}

// Distance from p (shape coordinates) in to the shape's edge. Geometry kinds: 0 none (far inside), 1 rounded box
// (geometry: half size xy, corner radius), 2 ellipse (geometry: radii xy), 3 segment (geometry: ends, extra.w half
// thickness), 4 polygon (extra.y first point, extra.z count).
fn edge_distance(index: u32, p: vec2<f32>) -> f32 {
	let kind = surfaces[index].kinds.w;
	let g = surfaces[index].geometry;
	if (kind == 1u) {
		let r = g.z;
		let q = abs(p) - g.xy + vec2<f32>(r);
		let outside = length(max(q, vec2<f32>(0.0))) + min(max(q.x, q.y), 0.0) - r;
		return max(-outside, 0.0);
	}
	if (kind == 2u) {
		let radii = max(g.xy, vec2<f32>(1e-6));
		let k0 = length(p / radii);
		let k1 = length(p / (radii * radii));
		return max(-(k0 * (k0 - 1.0) / max(k1, 1e-6)), 0.0);
	}
	if (kind == 3u) {
		return max(surfaces[index].extra.w - segment_distance(p, g.xy, g.zw), 0.0);
	}
	if (kind == 4u) {
		let first = u32(surfaces[index].extra.y);
		let count = u32(surfaces[index].extra.z);
		var best = 1e9;
		for (var i = 0u; i < count; i = i + 1u) {
			let a = points[first + i];
			let b = points[first + (i + 1u) % count];
			best = min(best, segment_distance(p, a, b));
		}
		return best;
	}
	return 1e9;
}

fn profile_height(t: f32, profile: u32) -> f32 {
	switch profile {
		case 1u: { return 1.0 - (1.0 - t) * (1.0 - t); }
		case 2u: { return t; }
		case 3u: { return sqrt(max(1.0 - (1.0 - t) * (1.0 - t), 0.0)); }
		case 4u: { return -t; }
		case 5u: { return t * t * (3.0 - 2.0 * t); }
		default: { return 1.0; }
	}
}

// The shape's height at a point (shape coordinates), in shape coordinates: its profile over its edge distance, and its
// pattern's bump (both given in surface units).
fn surface_height(index: u32, local: vec2<f32>, footprint: f32) -> f32 {
	let relief = surfaces[index].relief;
	let unit = max(surfaces[index].extra.x, 1e-6);
	var shape = 1.0;
	let profile = u32(relief.z);
	if (relief.y > 0.0) {
		shape = profile_height(clamp(edge_distance(index, local) / unit / relief.y, 0.0, 1.0), profile);
	} else if (profile == 4u) {
		shape = -1.0;
	}
	var height = relief.x * shape;
	let bump = surfaces[index].relief2.x;
	if (surfaces[index].kinds.x == PAINT_PATTERN && bump != 0.0) {
		height = height + bump * (shape_pattern(index, local, footprint) - 0.5);
	}
	return height * unit;
}

// Height (in shape coordinates) in red, roughness in green and emissive in blue, premultiplied by coverage in alpha so a covering shape
// replaces what is beneath; an adding shape leaves alpha at 0, adding its height.
@fragment
fn fs_height(in_: VSOut) -> @location(0) vec4<f32> {
	let footprint = max(fwidth(in_.local.x), fwidth(in_.local.y));
	if (clipped(in_) || in_.surface == NO_SURFACE) {
		discard;
	}
	let index = in_.surface & INDEX_MASK;
	let height = surface_height(index, in_.local, footprint);
	let relief2 = surfaces[index].relief2;
	if (surfaces[index].relief.w > 0.5) {
		return vec4<f32>(height, 0.0, 0.0, 0.0);
	}
	return vec4<f32>(height, relief2.y, relief2.z, 1.0);
}
