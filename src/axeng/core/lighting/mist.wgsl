// Mist (Mist2D): the stirring (a small fluid grid fixed in the world), the mist's density over the screen and
// the halos of scattered light. lighting.wgsl's fs_mist draws it over the lit scene.

struct Mist {
	view: vec4<f32>, // viewport width, height, camera x, y
	frame: vec4<f32>, // zoom, screen pixels per density texel, time, seconds since the last frame
	grid: vec4<f32>, // stirring grid origin xy, world pixels per texel, 1 when it holds a state
	gridSize: vec4<f32>, // width, height, the last frame's origin xy
	sim: vec4<f32>, // refill per second, damping per second, displacement seconds, 1 when stirring
	noise: vec4<f32>, // scale, warp, seed
	shape: vec4<f32>, // amount, coverage, softness, mask bias
	wind: vec4<f32>, // world pixels a second
	mask: vec4<f32>, // origin xy, world pixels per texel, 1 when there is a mask
	maskSize: vec4<f32>, // width, height
	blur: vec4<f32>, // one step in target texels (xy), sigma and reach in steps
	extent: vec4<f32>, // this pass's target width, height
};

@group(0) @binding(0) var<uniform> mist: Mist;
@group(0) @binding(1) var texA: texture_2d<f32>;
@group(0) @binding(2) var texB: texture_2d<f32>;
@group(0) @binding(3) var texC: texture_2d<f32>;
@group(0) @binding(4) var linearSampler: sampler;
// Movers are two vec4s each: (position, velocity), (radius, push, outward, clear).
@group(0) @binding(5) var<storage, read> movers: array<vec4<f32>>;

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

// ---- Stirring ----

struct MoverOut {
	@builtin(position) pos: vec4<f32>,
	@location(0) local: vec2<f32>,
	@location(1) @interpolate(flat) velocity: vec2<f32>,
	@location(2) @interpolate(flat) shape: vec4<f32>,
};

@vertex
fn vs_mover(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> MoverOut {
	var corners = array<vec2<f32>, 6>(vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, -1.0), vec2<f32>(1.0, 1.0),
		vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, 1.0), vec2<f32>(-1.0, 1.0));
	let a = movers[instance * 2u];
	let b = movers[instance * 2u + 1u];
	let local = corners[vertex];
	// At least a texel and a half across, so small movers still land on the grid.
	let radius = max(b.x, mist.grid.z * 1.5);
	let world = a.xy + local * radius;
	let g = (world - mist.grid.xy) / mist.grid.z / mist.gridSize.xy;
	var out: MoverOut;
	out.pos = vec4<f32>(g.x * 2.0 - 1.0, 1.0 - g.y * 2.0, 0.0, 1.0);
	out.local = local;
	out.velocity = a.zw;
	out.shape = b;
	return out;
}

// Forces, summed: the velocity the mist is pulled to times the weight, the clearing, and the weight.
@fragment
fn fs_mover(in_: MoverOut) -> @location(0) vec4<f32> {
	let d = length(in_.local);
	if (d >= 1.0) {
		discard;
	}
	let f = (1.0 - d * d) * (1.0 - d * d);
	let away = select(in_.local / max(d, 0.0001), vec2<f32>(0.0), d < 0.0001);
	let pulled = in_.velocity * in_.shape.y + away * in_.shape.z;
	return vec4<f32>(pulled * f, in_.shape.w * f, f);
}

fn previous_state(world: vec2<f32>) -> vec4<f32> {
	if (mist.grid.w < 0.5) {
		return vec4<f32>(0.0);
	}
	let uv = (world - mist.gridSize.zw) / mist.grid.z / mist.gridSize.xy;
	if (any(uv < vec2<f32>(0.0)) || any(uv > vec2<f32>(1.0))) {
		return vec4<f32>(0.0);
	}
	return textureSampleLevel(texA, linearSampler, uv, 0.0);
}

// Moves the last state along its own velocity (from where the grid stood last frame), adds the movers' forces, calms
// the air and refills what was cleared. texA: the last state, texB: the forces.
@fragment
fn fs_advect(in_: FullscreenOut) -> @location(0) vec4<f32> {
	let world = mist.grid.xy + in_.pos.xy * mist.grid.z;
	let dt = mist.frame.w;
	let here = previous_state(world);
	var s = previous_state(world - here.xy * dt);
	let force = textureLoad(texB, vec2<i32>(in_.pos.xy), 0);
	if (force.a > 0.0) {
		s = vec4<f32>(mix(s.xy, force.xy / force.a, min(force.a, 1.0)), s.z, s.w);
	}
	let calmed = s.xy * exp(-mist.sim.y * dt);
	let cleared = clamp(s.z + force.z * dt - mist.sim.x * dt, 0.0, 1.0);
	return vec4<f32>(calmed, cleared, 1.0);
}

fn load_clamped(t: texture_2d<f32>, p: vec2<i32>) -> vec4<f32> {
	let size = vec2<i32>(mist.gridSize.xy);
	return textureLoad(t, clamp(p, vec2<i32>(0), size - vec2<i32>(1)), 0);
}

fn load_or_zero(t: texture_2d<f32>, p: vec2<i32>) -> f32 {
	let size = vec2<i32>(mist.gridSize.xy);
	if (any(p < vec2<i32>(0)) || any(p >= size)) {
		return 0.0;
	}
	return textureLoad(t, p, 0).r;
}

// texA: the advected state.
@fragment
fn fs_divergence(in_: FullscreenOut) -> @location(0) vec4<f32> {
	let p = vec2<i32>(in_.pos.xy);
	let l = load_clamped(texA, p - vec2<i32>(1, 0)).x;
	let r = load_clamped(texA, p + vec2<i32>(1, 0)).x;
	let t = load_clamped(texA, p - vec2<i32>(0, 1)).y;
	let b = load_clamped(texA, p + vec2<i32>(0, 1)).y;
	return vec4<f32>((r - l + b - t) / (2.0 * mist.grid.z), 0.0, 0.0, 1.0);
}

// texA: the last pressure, texB: the divergence.
@fragment
fn fs_jacobi(in_: FullscreenOut) -> @location(0) vec4<f32> {
	let p = vec2<i32>(in_.pos.xy);
	let sum = load_or_zero(texA, p - vec2<i32>(1, 0)) + load_or_zero(texA, p + vec2<i32>(1, 0))
		+ load_or_zero(texA, p - vec2<i32>(0, 1)) + load_or_zero(texA, p + vec2<i32>(0, 1));
	let divergence = textureLoad(texB, p, 0).r;
	return vec4<f32>((sum - divergence * mist.grid.z * mist.grid.z) * 0.25, 0.0, 0.0, 1.0);
}

// Takes the pressure's gradient away, so the stirred air flows round rather than piling up. texA: the advected state,
// texB: the pressure.
@fragment
fn fs_project(in_: FullscreenOut) -> @location(0) vec4<f32> {
	let p = vec2<i32>(in_.pos.xy);
	let s = textureLoad(texA, p, 0);
	let gradient = vec2<f32>(load_or_zero(texB, p + vec2<i32>(1, 0)) - load_or_zero(texB, p - vec2<i32>(1, 0)),
		load_or_zero(texB, p + vec2<i32>(0, 1)) - load_or_zero(texB, p - vec2<i32>(0, 1))) / (2.0 * mist.grid.z);
	return vec4<f32>(s.xy - gradient, s.z, 1.0);
}

// ---- Density ----

fn hash21(p: vec2<f32>) -> f32 {
	let x = bitcast<u32>(i32(p.x));
	let y = bitcast<u32>(i32(p.y));
	var h = x ^ (y + 0x9e3779b9u + (x << 6u) + (x >> 2u));
	h = (h ^ (h >> 16u)) * 0x7feb352du;
	h = (h ^ (h >> 15u)) * 0x846ca68bu;
	h = h ^ (h >> 16u);
	return f32(h >> 8u) * (1.0 / 16777216.0);
}

fn value_noise(p: vec2<f32>) -> f32 {
	let i = floor(p);
	let f = fract(p);
	let u = f * f * (3.0 - 2.0 * f);
	return mix(
		mix(hash21(i), hash21(i + vec2<f32>(1.0, 0.0)), u.x),
		mix(hash21(i + vec2<f32>(0.0, 1.0)), hash21(i + vec2<f32>(1.0, 1.0)), u.x),
		u.y);
}

fn fbm(p: vec2<f32>) -> f32 {
	var sum = 0.0;
	var amplitude = 0.5;
	var q = p;
	for (var i = 0; i < 5; i = i + 1) {
		sum = sum + value_noise(q) * amplitude;
		q = q * 2.03 + vec2<f32>(17.1, 9.7);
		amplitude = amplitude * 0.5;
	}
	return sum / 0.96875;
}

fn state_at(world: vec2<f32>) -> vec4<f32> {
	if (mist.sim.w < 0.5) {
		return vec4<f32>(0.0);
	}
	let uv = (world - mist.grid.xy) / mist.grid.z / mist.gridSize.xy;
	if (any(uv < vec2<f32>(0.0)) || any(uv > vec2<f32>(1.0))) {
		return vec4<f32>(0.0);
	}
	return textureSampleLevel(texA, linearSampler, uv, 0.0);
}

fn mask_at(world: vec2<f32>) -> f32 {
	if (mist.mask.w < 0.5) {
		return 0.0;
	}
	let uv = (world - mist.mask.xy) / mist.mask.z / mist.maskSize.xy;
	return textureSampleLevel(texC, linearSampler, uv, 0.0).r;
}

// Red: density (0 to 1), green: cleared, blue: mask. texA: the stirring's state, texC: the mask mask.
@fragment
fn fs_density(in_: FullscreenOut) -> @location(0) vec4<f32> {
	let screen = in_.pos.xy * mist.frame.y;
	let world = (screen - mist.view.xy * 0.5) / mist.frame.x + mist.view.zw;
	let state = state_at(world);
	let mask = mask_at(world);
	let time = mist.frame.z;
	// The stirred air drags the pattern along with it; the pattern drifts with the wind and slowly changes shape.
	let p = (world - state.xy * mist.sim.z - mist.wind.xy * time) / max(mist.noise.x, 1.0)
		+ vec2<f32>(mist.noise.z * 0.71, mist.noise.z * 1.37);
	let warp = vec2<f32>(fbm(p * 0.6 + vec2<f32>(5.2, 1.3) + vec2<f32>(time * 0.011, 0.0)),
		fbm(p * 0.6 + vec2<f32>(1.7, 9.2) - vec2<f32>(0.0, time * 0.013)));
	let n = fbm(p + (warp - vec2<f32>(0.5)) * mist.noise.y);
	let amount = mist.shape.x;
	let threshold = mix(1.1, mist.shape.y, amount) - mist.shape.w * mask;
	let soft = max(mist.shape.z, 0.001);
	let cover = smoothstep(threshold - soft, threshold + soft, n);
	let density = cover * min(amount * 3.0, 1.0) * (1.0 - state.z);
	return vec4<f32>(density, state.z, mask, 1.0);
}

// ---- Halos ----

// Four bilinear taps over the source texels this target texel covers. texA: the light map.
@fragment
fn fs_downsample(in_: FullscreenOut) -> @location(0) vec4<f32> {
	let uv = in_.pos.xy / mist.extent.xy;
	let d = 0.25 / mist.extent.xy;
	let sum = textureSampleLevel(texA, linearSampler, uv + vec2<f32>(-d.x, -d.y), 0.0)
		+ textureSampleLevel(texA, linearSampler, uv + vec2<f32>(d.x, -d.y), 0.0)
		+ textureSampleLevel(texA, linearSampler, uv + vec2<f32>(-d.x, d.y), 0.0)
		+ textureSampleLevel(texA, linearSampler, uv + vec2<f32>(d.x, d.y), 0.0);
	return vec4<f32>(sum.rgb * 0.25, 1.0);
}

// One direction of a separable gaussian. texA: the source, the target's size.
@fragment
fn fs_blur(in_: FullscreenOut) -> @location(0) vec4<f32> {
	let uv = in_.pos.xy / mist.extent.xy;
	let sigma = max(mist.blur.z, 0.001);
	let steps = i32(mist.blur.w);
	var sum = vec3<f32>(0.0);
	var total = 0.0;
	for (var i = -steps; i <= steps; i = i + 1) {
		let t = f32(i);
		let weight = exp(-0.5 * t * t / (sigma * sigma));
		sum = sum + textureSampleLevel(texA, linearSampler, uv + mist.blur.xy * t / mist.extent.xy, 0.0).rgb * weight;
		total = total + weight;
	}
	return vec4<f32>(sum / max(total, 0.0001), 1.0);
}
