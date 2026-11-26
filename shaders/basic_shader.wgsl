// Set per pipeline: screen space sprites are positioned in pixels, others in the world.
override screen_space: bool = false;

struct Uniforms {
	pos: vec2<f32>,
	scale: vec2<f32>,
	region: vec2<u32>, // x | y << 16 and w | h << 16 in texels; a zero size means the whole texture
	tint: vec2<u32>, // half floats r | g << 16 and b | a << 16
	clip: vec2<u32>, // min x | y << 16 and max x | y << 16 in screen pixels
	z_idx: f32,
	rotation: f32,
	wear: vec2<u32>, // SpriteWear: seed | jitter << 8 | grime << 16 | coat << 24, coat colour r | g << 8 | b << 16
};

@group(0) @binding(0) var<storage, read> sprites : array<Uniforms>;
@group(0) @binding(1) var tex : texture_2d<f32>;
@group(0) @binding(2) var samp : sampler;
struct CameraUniforms {
	// xy: the target's size in pixels; zw: for screen space sprites, the z window [z, w) a sprite must lie in to be
	// drawn, when z < w (a pass that draws one layer of an interface).
	viewport: vec4<f32>,
	camera: vec4<f32>,
};

@group(1) @binding(0) var<uniform> cameraData : CameraUniforms;

struct VSOut {
	@builtin(position) pos : vec4<f32>,
	@location(0) uv : vec2<f32>,
	@location(1) tint : vec4<f32>,
	@location(2) @interpolate(flat) texelBounds : vec4<f32>,
	@location(3) @interpolate(flat) clip_rect : vec4<f32>,
	// Where it is in the world, in pixels (in the view's world: the ambient occlusion masks' may be shifted).
	@location(4) world : vec2<f32>,
	// Its turn, for turning its surface's normals (fs_surface).
	@location(5) @interpolate(flat) rotation : f32,
	@location(6) @interpolate(flat) wear : vec2<u32>,
	// Where it is on the sprite, in world pixels from its centre before it turns: wear stays on the sprite as it moves.
	@location(7) wearAt : vec2<f32>,
	// How far down the sprite (0 top, 1 bottom) a point is in the sprite and on the screen: for turning a baked light.
	@location(8) downs : vec2<f32>,
};

// Ambient occlusion cuts (AmbientOcclusionCut2D): areas hiding the casters beneath them. Only the ambient occlusion
// caster pipeline binds group 2.
struct AoCut {
	ab: vec4<f32>,
	cd: vec4<f32>,
	// x: the depth a caster must reach to stand above it.
	depth: vec4<f32>,
};
struct AoCuts {
	// xy: added to a position in the view's world to give where it lies in the world; z: the count.
	shift: vec4<f32>,
	cuts: array<AoCut, 16>,
};
@group(2) @binding(0) var<uniform> aoCuts : AoCuts;

fn ao_cut_side(p: vec2<f32>, a: vec2<f32>, b: vec2<f32>) -> f32 {
	let edge = b - a;
	let to = p - a;
	return edge.x * to.y - edge.y * to.x;
}

// Whether a caster at this depth, at this place in the view's world, lies hidden beneath a cut.
fn ao_cut(viewWorld: vec2<f32>, depth: f32) -> bool {
	let p = viewWorld + aoCuts.shift.xy;
	let count = min(u32(aoCuts.shift.z), 16u);
	for (var i = 0u; i < count; i = i + 1u) {
		let cut = aoCuts.cuts[i];
		if (depth >= cut.depth.x) {
			continue;
		}
		let s0 = ao_cut_side(p, cut.ab.xy, cut.ab.zw);
		let s1 = ao_cut_side(p, cut.ab.zw, cut.cd.xy);
		let s2 = ao_cut_side(p, cut.cd.xy, cut.cd.zw);
		let s3 = ao_cut_side(p, cut.cd.zw, cut.ab.xy);
		if ((s0 >= 0.0 && s1 >= 0.0 && s2 >= 0.0 && s3 >= 0.0) || (s0 <= 0.0 && s1 <= 0.0 && s2 <= 0.0 && s3 <= 0.0)) {
			return true;
		}
	}
	return false;
}

@vertex
fn vs_main(@builtin(vertex_index) in_idx: u32, @builtin(instance_index) instance: u32) -> VSOut {
	var out: VSOut;
	let u = sprites[instance];
	let i = i32(in_idx);
	let textureSize = vec2<f32>(textureDimensions(tex, 0));
	let regionOrigin = vec2<f32>(vec2<u32>(u.region.x & 0xFFFFu, u.region.x >> 16u));
	let regionSize = vec2<f32>(vec2<u32>(u.region.y & 0xFFFFu, u.region.y >> 16u));
	let useRegion = u.region.y != 0u;
	let pixelSize = select(textureSize, regionSize, useRegion);
	let size = pixelSize * u.scale;
	let uv0 = select(vec2<f32>(0.0, 0.0), regionOrigin / textureSize, useRegion);
	let uv1 = select(vec2<f32>(1.0, 1.0), (regionOrigin + regionSize) / textureSize, useRegion);
	var localPos: vec2<f32>;
	var uv: vec2<f32>;

	if (i == 0) {
		localPos = vec2<f32>(0.0, 0.0);
	} else if (i == 1) {
		localPos = vec2<f32>(1.0, 0.0);
	} else if (i == 2) {
		localPos = vec2<f32>(0.0, 1.0);
	} else if (i == 3) {
		localPos = vec2<f32>(1.0, 0.0);
	} else if (i == 4) {
		localPos = vec2<f32>(1.0, 1.0);
	} else {
		localPos = vec2<f32>(0.0, 1.0);
	}
	uv = mix(uv0, uv1, localPos);

	// world-space position in pixels
	let center = u.pos + size * 0.5;
	let offset = (localPos - vec2<f32>(0.5, 0.5)) * size;
	let c = cos(u.rotation);
	let s = sin(u.rotation);
	let rotatedOffset = vec2<f32>(offset.x * c - offset.y * s, offset.x * s + offset.y * c);
	let worldPos = center + rotatedOffset;
	// convert to NDC (-1..1)
	let screenPos = select(
		(worldPos - cameraData.camera.xy) * cameraData.camera.z + cameraData.viewport.xy * 0.5,
		worldPos,
		screen_space
	);
	let ndcX = (screenPos.x / cameraData.viewport.x) * 2.0 - 1.0;
	let ndcY = 1.0 - (screenPos.y / cameraData.viewport.y) * 2.0;

	let depth = 0.5 + atan(u.z_idx) / 3.141592653589793;
	out.pos = vec4<f32>(ndcX, ndcY, depth, 1.0);
	let window = cameraData.viewport.zw;
	if (screen_space && window.x < window.y && (u.z_idx < window.x || u.z_idx >= window.y)) {
		// Outside this pass's z window: every corner on one point, so nothing is drawn.
		out.pos = vec4<f32>(-2.0, -2.0, 0.0, 1.0);
	}
	out.uv = uv;
	out.world = worldPos;
	out.rotation = u.rotation;
	out.wear = u.wear;
	out.wearAt = offset;
	let reach = abs(size.x * s) + abs(size.y * c);
	out.downs = vec2<f32>(localPos.y, rotatedOffset.y / max(reach, 1e-4) + 0.5);
	out.tint = vec4<f32>(unpack2x16float(u.tint.x), unpack2x16float(u.tint.y));
	out.clip_rect = vec4<f32>(
		f32(u.clip.x & 0xFFFFu), f32(u.clip.x >> 16u), f32(u.clip.y & 0xFFFFu), f32(u.clip.y >> 16u));
	let texelMin = select(vec2<f32>(0.0), regionOrigin, useRegion);
	let texelMax = select(textureSize, regionOrigin + regionSize, useRegion) - vec2<f32>(1.0);
	out.texelBounds = clamp(vec4<f32>(texelMin, texelMax),
		vec4<f32>(0.0), vec4<f32>(textureSize - vec2<f32>(1.0), textureSize - vec2<f32>(1.0)));
	return out;
}

fn texel(coord: vec2<i32>, bounds: vec4<f32>) -> vec4<f32> {
	let pixel = textureLoad(tex, clamp(coord, vec2<i32>(bounds.xy), vec2<i32>(bounds.zw)), 0);
	return vec4<f32>(pixel.rgb * pixel.a, pixel.a);
}

fn sample_texture(uv: vec2<f32>, bounds: vec4<f32>) -> vec4<f32> {
	let texturePixelSize = vec2<f32>(1.0) / vec2<f32>(textureDimensions(tex, 0));
	let texelPos = uv / texturePixelSize - vec2<f32>(0.5);
	let base = vec2<i32>(floor(texelPos));
	let fraction = fract(texelPos);
	let top = mix(
		texel(base, bounds),
		texel(base + vec2<i32>(1, 0), bounds), fraction.x);
	let bottom = mix(
		texel(base + vec2<i32>(0, 1), bounds),
		texel(base + vec2<i32>(1, 1), bounds), fraction.x);
	return mix(top, bottom, fraction.y);
}

fn wear_hash(p: vec2<f32>) -> f32 {
	let q = vec2<u32>(bitcast<u32>(i32(floor(p.x))), bitcast<u32>(i32(floor(p.y))));
	var h = (q.x * 0x8da6b343u) ^ (q.y * 0xd8163841u);
	h = (h ^ (h >> 15u)) * 0x2c1b3c6du;
	h = h ^ (h >> 12u);
	return f32(h & 0xFFFFu) / 65535.0;
}

fn wear_noise(p: vec2<f32>) -> f32 {
	let i = floor(p);
	let f = fract(p);
	let u = f * f * (3.0 - 2.0 * f);
	return mix(mix(wear_hash(i), wear_hash(i + vec2<f32>(1.0, 0.0)), u.x),
		mix(wear_hash(i + vec2<f32>(0.0, 1.0)), wear_hash(i + vec2<f32>(1.0, 1.0)), u.x), u.y);
}

// Wear (SpriteWear) over the sprite (at: world pixels from its centre, unturned): a drift of colour of its own, dark
// patches and a settled coat.
fn worn(col: vec4<f32>, wear: vec2<u32>, world: vec2<f32>, downs: vec2<f32>) -> vec4<f32> {
	// (`world` is the place on the sprite, not in the world: see wearAt.)
	if ((wear.x == 0u && wear.y == 0u) || col.a <= 0.0) {
		return col;
	}
	// A light baked in from the sprite's top, turned back to fall from the screen's top.
	let baked = f32(wear.y >> 24u) / 255.0;
	var lit = col.rgb;
	if (baked > 0.0) {
		lit = lit * (1.0 + baked * (0.5 - downs.y)) / max(1.0 + baked * (0.5 - downs.x), 0.05);
	}
	let seed = f32(wear.x & 0xFFu) / 255.0;
	let jitter = f32((wear.x >> 8u) & 0xFFu) / 255.0;
	let grime = f32((wear.x >> 16u) & 0xFFu) / 255.0;
	let coat = f32(wear.x >> 24u) / 255.0;
	let coatColour = vec3<f32>(f32(wear.y & 0xFFu), f32((wear.y >> 8u) & 0xFFu), f32((wear.y >> 16u) & 0xFFu)) / 255.0;
	var rgb = lit;
	let drift = vec3<f32>(fract(seed * 7.13 + 0.1), fract(seed * 3.71 + 0.5), fract(seed * 5.29 + 0.3)) - vec3<f32>(0.5);
	rgb = rgb * (vec3<f32>(1.0) + jitter * (drift * 0.08 + vec3<f32>(drift.x * 0.1)));
	let offset = vec2<f32>(seed * 97.0, seed * 61.0);
	let patches = wear_noise(world / 48.0 + offset) * 0.65 + wear_noise(world / 11.0 + offset) * 0.35;
	rgb = rgb * (1.0 - grime * 0.4 * smoothstep(0.5, 0.85, patches));
	let settled = wear_noise(world / 21.0 - offset) * 0.7 + wear_noise(world / 5.0 + offset) * 0.3;
	rgb = mix(rgb, coatColour, coat * smoothstep(0.42, 0.78, settled));
	return vec4<f32>(clamp(rgb, vec3<f32>(0.0), vec3<f32>(1.0)), col.a);
}

fn sprite_color(in_: VSOut) -> vec4<f32> {
	let texturePixelSize = vec2<f32>(1.0) / vec2<f32>(textureDimensions(tex, 0));
	let spriteScreenResolution = vec2<f32>(1.0) / fwidth(in_.uv);
	let uvPixelSrc = floor(in_.uv / texturePixelSize + vec2<f32>(0.499));
	let edge = uvPixelSrc * texturePixelSize * spriteScreenResolution;
	let uvPixel = in_.uv * spriteScreenResolution;
	let uvFactor = clamp(uvPixel - edge + vec2<f32>(0.5), vec2<f32>(0.0), vec2<f32>(1.0));
	// Pixel art keeps texels sharp. Clamp each tap to the sprite region, not just the full atlas.
	let uv = (mix(uvPixelSrc - vec2<f32>(1.0), uvPixelSrc, uvFactor) + vec2<f32>(0.5)) * texturePixelSize;
	let pixel = sample_texture(uv, in_.texelBounds);
	// The render passes expect straight alpha; unpremultiply only after filtering.
	var rgb = vec3<f32>(0.0);
	if (pixel.a > 0.0) {
		rgb = pixel.rgb / pixel.a;
	}
	return worn(vec4<f32>(rgb, pixel.a) * in_.tint, in_.wear, in_.wearAt, in_.downs);
}

@fragment
fn fs_main(in_: VSOut) -> @location(0) vec4<f32> {
	if (in_.pos.x < in_.clip_rect.x || in_.pos.y < in_.clip_rect.y
		|| in_.pos.x >= in_.clip_rect.z || in_.pos.y >= in_.clip_rect.w) {
		discard;
	}
	let col = sprite_color(in_);
	if (col.a <= 0.0) {
		discard;
	}
	return col;
}

// The silhouette of an occluder sprite for the radiance cascades' scene: its colour as albedo where it is at least
// half opaque, and not hidden beneath a cut (something standing over it).
@fragment
fn fs_radiance_occluder(in_: VSOut) -> @location(0) vec4<f32> {
	let col = sprite_color(in_);
	if (col.a < 0.5 || ao_cut(in_.world, in_.pos.z)) {
		discard;
	}
	return vec4<f32>(col.rgb, 1.0);
}

// The silhouette of an ambient occlusion caster: full coverage where it is at least half opaque (red, for a single
// channel mask), and none where a cut hides it.
@fragment
fn fs_ao_silhouette(in_: VSOut) -> @location(0) vec4<f32> {
	if (sprite_color(in_).a < 0.5 || ao_cut(in_.world, in_.pos.z)) {
		discard;
	}
	return vec4<f32>(1.0);
}

// The texture's surface texture (Texture::surface), laid out like it: only the surface pipeline binds group 2's
// binding 1.
@group(2) @binding(1) var surfaceTex : texture_2d<f32>;

struct SurfaceOut {
	@location(0) normal: vec4<f32>,
	@location(1) emissive: vec4<f32>,
};

// A lit sprite's surface for the lighting's surface pass: its normal (turned with it) and roughness, and its colour
// where it glows. Where its colour is under half opaque, its surface empty or it lies beneath a cut, it leaves what
// is beneath.
@fragment
fn fs_surface(in_: VSOut) -> SurfaceOut {
	let col = sprite_color(in_);
	let size = vec2<f32>(textureDimensions(surfaceTex, 0));
	let texel = clamp(vec2<i32>(floor(in_.uv * size)), vec2<i32>(in_.texelBounds.xy), vec2<i32>(in_.texelBounds.zw));
	let surface = textureLoad(surfaceTex, texel, 0);
	if (col.a < 0.5 || surface.a < 0.25 || ao_cut(in_.world, in_.pos.z)) {
		discard;
	}
	let n = surface.xy * 2.0 - vec2<f32>(1.0);
	let c = cos(in_.rotation);
	let s = sin(in_.rotation);
	let turned = vec2<f32>(n.x * c - n.y * s, n.x * s + n.y * c);
	let emissive = clamp((surface.a - 0.5) * 2.0, 0.0, 1.0);
	var out: SurfaceOut;
	out.normal = vec4<f32>(turned * 0.5 + vec2<f32>(0.5), surface.b, 1.0);
	out.emissive = vec4<f32>(col.rgb * emissive, emissive);
	return out;
}

@fragment
fn fs_opaque(in_: VSOut) -> @location(0) vec4<f32> {
	if (in_.pos.x < in_.clip_rect.x || in_.pos.y < in_.clip_rect.y
		|| in_.pos.x >= in_.clip_rect.z || in_.pos.y >= in_.clip_rect.w) {
		discard;
	}
	let col = sprite_color(in_);
	if (col.a < 1.0) {
		discard;
	}
	return col;
}

struct OitOutput {
	@location(0) accumulation: vec4<f32>,
	@location(1) revealage: f32,
};

@fragment
fn fs_oit(in_: VSOut) -> OitOutput {
	if (in_.pos.x < in_.clip_rect.x || in_.pos.y < in_.clip_rect.y
		|| in_.pos.x >= in_.clip_rect.z || in_.pos.y >= in_.clip_rect.w) {
		discard;
	}
	let col = sprite_color(in_);
	let alpha = clamp(col.a, 0.0, 1.0);
	if (alpha <= 0.0 || alpha >= 1.0) {
		discard;
	}

	let depthWeight = 0.01 + 8.0 * in_.pos.z * in_.pos.z * in_.pos.z;
	let weight = clamp(depthWeight, 0.01, 8.0);
	var out: OitOutput;
	out.accumulation = vec4<f32>(col.rgb * alpha * weight, alpha * weight);
	out.revealage = alpha;
	return out;
}
