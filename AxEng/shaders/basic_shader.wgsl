struct Uniforms {
	pos: vec2<f32>,
	scale: vec2<f32>,
	region: vec4<f32>, // x,y,width,height in pixels
	tint: vec4<f32>, // r, g, b, a
	z_idx: f32,
	rotation: f32,
	use_region: u32,
	_padding: u32,
};

@group(0) @binding(0) var<storage, read> sprites : array<Uniforms>;
@group(0) @binding(1) var tex : texture_2d<f32>;
@group(0) @binding(2) var samp : sampler;
@group(1) @binding(0) var<uniform> viewport : vec2<f32>; // x=width, y=height

struct VSOut {
	@builtin(position) pos : vec4<f32>,
	@location(0) uv : vec2<f32>,
	@location(1) tint : vec4<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) in_idx: u32, @builtin(instance_index) instance: u32) -> VSOut {
	var out: VSOut;
	let u = sprites[instance];
	let i = i32(in_idx);
	let textureSize = vec2<f32>(textureDimensions(tex, 0));
	let useRegion = u.use_region != 0u;
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
	let ndcX = (worldPos.x / viewport.x) * 2.0 - 1.0;
	let ndcY = 1.0 - (worldPos.y / viewport.y) * 2.0;

	out.pos = vec4<f32>(ndcX, ndcY, u.z_idx, 1.0);
	out.uv = uv;
	out.tint = u.tint;
	return out;
}

@fragment
fn fs_main(in_: VSOut) -> @location(0) vec4<f32> {
	let texturePixelSize = vec2<f32>(1.0) / vec2<f32>(textureDimensions(tex, 0));
	let spriteScreenResolution = vec2<f32>(1.0) / fwidth(in_.uv);
	let uvPixelSrc = floor(in_.uv / texturePixelSize + vec2<f32>(0.499));
	let edge = uvPixelSrc * texturePixelSize * spriteScreenResolution;
	let uvPixel = in_.uv * spriteScreenResolution;
	let uvFactor = clamp(uvPixel - edge + vec2<f32>(0.5), vec2<f32>(0.0), vec2<f32>(1.0));
	let uv = (mix(uvPixelSrc - vec2<f32>(1.0), uvPixelSrc, uvFactor) + vec2<f32>(0.5)) * texturePixelSize;
	let col = textureSample(tex, samp, uv) * in_.tint;
	return col;
}
