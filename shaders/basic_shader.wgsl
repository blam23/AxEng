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
};

@group(0) @binding(0) var<storage, read> sprites : array<Uniforms>;
@group(0) @binding(1) var tex : texture_2d<f32>;
@group(0) @binding(2) var samp : sampler;
struct CameraUniforms {
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
};

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
	out.uv = uv;
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
	return vec4<f32>(rgb, pixel.a) * in_.tint;
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
