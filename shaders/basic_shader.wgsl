struct Uniforms {
	pos: vec2<f32>,
	scale: vec2<f32>,
	region: vec4<f32>, // x,y,width,height in pixels
	tint: vec4<f32>, // r, g, b, a
	z_idx: f32,
	rotation: f32,
	use_region: u32, // flags: 1 = use region, 2 = SDF, 4 = MSDF (distance edge at 0.5)
	screen_space: u32,
	clip_rect: vec4<f32>, // min x/y and max x/y in screen pixels; max < min disables clipping
	effect_params: vec4<f32>, // kind, thickness, softness, reserved
	effect_offset: vec4<f32>, // screen pixels in xy
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
	@location(3) @interpolate(flat) sdf : u32,
	@location(4) @interpolate(flat) clip_rect : vec4<f32>,
	@location(5) @interpolate(flat) effectParams : vec4<f32>,
	@location(6) @interpolate(flat) effectOffset : vec4<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) in_idx: u32, @builtin(instance_index) instance: u32) -> VSOut {
	var out: VSOut;
	let u = sprites[instance];
	let i = i32(in_idx);
	let textureSize = vec2<f32>(textureDimensions(tex, 0));
	let useRegion = (u.use_region & 1u) != 0u;
	let pixelSize = select(textureSize, u.region.zw, useRegion);
	let size = pixelSize * u.scale;
	let uv0 = select(vec2<f32>(0.0, 0.0), u.region.xy / textureSize, useRegion);
	let uv1 = select(vec2<f32>(1.0, 1.0), (u.region.xy + u.region.zw) / textureSize, useRegion);
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
		u.screen_space != 0u
	);
	let ndcX = (screenPos.x / cameraData.viewport.x) * 2.0 - 1.0;
	let ndcY = 1.0 - (screenPos.y / cameraData.viewport.y) * 2.0;

	let depth = 0.5 + atan(u.z_idx) / 3.141592653589793;
	out.pos = vec4<f32>(ndcX, ndcY, depth, 1.0);
	out.uv = uv;
	out.tint = u.tint;
	out.sdf = (u.use_region >> 1u) & 3u;
	out.clip_rect = u.clip_rect;
	out.effectParams = u.effect_params;
	out.effectOffset = u.effect_offset;
	let texelMin = select(vec2<f32>(0.0), u.region.xy, useRegion);
	let texelMax = select(textureSize, u.region.xy + u.region.zw, useRegion) - vec2<f32>(1.0);
	out.texelBounds = clamp(vec4<f32>(texelMin, texelMax),
		vec4<f32>(0.0), vec4<f32>(textureSize - vec2<f32>(1.0), textureSize - vec2<f32>(1.0)));
	return out;
}

fn texel(coord: vec2<i32>, bounds: vec4<f32>, premultiply: bool) -> vec4<f32> {
	let pixel = textureLoad(tex, clamp(coord, vec2<i32>(bounds.xy), vec2<i32>(bounds.zw)), 0);
	return select(pixel, vec4<f32>(pixel.rgb * pixel.a, pixel.a), premultiply);
}

fn sample_texture(uv: vec2<f32>, bounds: vec4<f32>, premultiply: bool) -> vec4<f32> {
	let texturePixelSize = vec2<f32>(1.0) / vec2<f32>(textureDimensions(tex, 0));
	let texelPos = uv / texturePixelSize - vec2<f32>(0.5);
	let base = vec2<i32>(floor(texelPos));
	let fraction = fract(texelPos);
	let top = mix(
		texel(base, bounds, premultiply),
		texel(base + vec2<i32>(1, 0), bounds, premultiply), fraction.x);
	let bottom = mix(
		texel(base + vec2<i32>(0, 1), bounds, premultiply),
		texel(base + vec2<i32>(1, 1), bounds, premultiply), fraction.x);
	return mix(top, bottom, fraction.y);
}

fn median3(values: vec3<f32>) -> f32 {
	return max(min(values.r, values.g), min(max(values.r, values.g), values.b));
}

fn sprite_color(in_: VSOut) -> vec4<f32> {
	let sdf = in_.sdf != 0u;
	let msdf = in_.sdf == 2u;
	let texturePixelSize = vec2<f32>(1.0) / vec2<f32>(textureDimensions(tex, 0));
	let spriteScreenResolution = vec2<f32>(1.0) / fwidth(in_.uv);
	let uvPixelSrc = floor(in_.uv / texturePixelSize + vec2<f32>(0.499));
	let edge = uvPixelSrc * texturePixelSize * spriteScreenResolution;
	let uvPixel = in_.uv * spriteScreenResolution;
	let uvFactor = clamp(uvPixel - edge + vec2<f32>(0.5), vec2<f32>(0.0), vec2<f32>(1.0));
	// Pixel art keeps texels sharp; distance fields are sampled smoothly.
	let snappedUv = (mix(uvPixelSrc - vec2<f32>(1.0), uvPixelSrc, uvFactor) + vec2<f32>(0.5)) * texturePixelSize;
	let uv = select(snappedUv, in_.uv, sdf);
	// Clamp each tap to the sprite region, not just the full atlas. Distance fields aren't premultiplied:
	// their alpha is a distance, not coverage.
	let premultiply = !sdf;
	let effectUv = uv - dpdx(uv) * in_.effectOffset.x - dpdy(uv) * in_.effectOffset.y;
	let pixel = sample_texture(effectUv, in_.texelBounds, premultiply);
	let distance = select(pixel.a, median3(pixel.rgb), msdf);
	// Four quarter-pixel samples approximate 2x render-and-downsample for distance-field sprites.
	let distanceWidth = max(fwidth(distance) * 0.25, 0.0001);
	let effect = in_.effectParams.x > 0.0;
	let effectDistanceWidth = fwidth(distance);
	let softness = max(in_.effectParams.z, 0.0) * effectDistanceWidth;
	let expansion = max(in_.effectParams.y, 0.0) * effectDistanceWidth;
	let lowerEdge = select(0.5 - distanceWidth, 0.5 - expansion - distanceWidth - softness, effect);
	let upperEdge = select(0.5 + distanceWidth, 0.5 - expansion + distanceWidth + softness, effect);
	var sdfAlpha = smoothstep(lowerEdge, upperEdge, distance);
	let sampleOffsetX = dpdx(effectUv) * 0.25;
	let sampleOffsetY = dpdy(effectUv) * 0.25;
	if (sdf) {
		let sample0 = sample_texture(effectUv - sampleOffsetX - sampleOffsetY, in_.texelBounds, false);
		let sample1 = sample_texture(effectUv + sampleOffsetX - sampleOffsetY, in_.texelBounds, false);
		let sample2 = sample_texture(effectUv - sampleOffsetX + sampleOffsetY, in_.texelBounds, false);
		let sample3 = sample_texture(effectUv + sampleOffsetX + sampleOffsetY, in_.texelBounds, false);
		sdfAlpha = (smoothstep(lowerEdge, upperEdge, select(sample0.a, median3(sample0.rgb), msdf))
			+ smoothstep(lowerEdge, upperEdge, select(sample1.a, median3(sample1.rgb), msdf))
			+ smoothstep(lowerEdge, upperEdge, select(sample2.a, median3(sample2.rgb), msdf))
			+ smoothstep(lowerEdge, upperEdge, select(sample3.a, median3(sample3.rgb), msdf))) * 0.25;
	}
	// The render passes expect straight alpha; unpremultiply only after filtering.
	var rgb = pixel.rgb;
	if (msdf) {
		rgb = vec3<f32>(1.0);
	} else if (!sdf && pixel.a > 0.0) {
		rgb = pixel.rgb / pixel.a;
	} else if (!sdf) {
		rgb = vec3<f32>(0.0);
	}
	return vec4<f32>(rgb, select(pixel.a, sdfAlpha, sdf)) * in_.tint;
}

@fragment
fn fs_main(in_: VSOut) -> @location(0) vec4<f32> {
	if (in_.clip_rect.z >= in_.clip_rect.x && in_.clip_rect.w >= in_.clip_rect.y
		&& (in_.pos.x < in_.clip_rect.x || in_.pos.y < in_.clip_rect.y
			|| in_.pos.x >= in_.clip_rect.z || in_.pos.y >= in_.clip_rect.w)) {
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
	if (in_.clip_rect.z >= in_.clip_rect.x && in_.clip_rect.w >= in_.clip_rect.y
		&& (in_.pos.x < in_.clip_rect.x || in_.pos.y < in_.clip_rect.y
			|| in_.pos.x >= in_.clip_rect.z || in_.pos.y >= in_.clip_rect.w)) {
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
	if (in_.clip_rect.z >= in_.clip_rect.x && in_.clip_rect.w >= in_.clip_rect.y
		&& (in_.pos.x < in_.clip_rect.x || in_.pos.y < in_.clip_rect.y
			|| in_.pos.x >= in_.clip_rect.z || in_.pos.y >= in_.clip_rect.w)) {
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
