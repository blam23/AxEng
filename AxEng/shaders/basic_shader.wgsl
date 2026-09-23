struct Uniforms {
	pos_size: vec4<f32>, // x,y,width,height
	region: vec4<f32>, // u0,v0,u1,v1
	tint: vec4<f32>, // r, g, b, a
	z_idx: f32,

	_unused1: f32,
	_unused2: f32,
	_unused3: f32,
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
	var localPos: vec2<f32>;
	var uv: vec2<f32>;

	if (i == 0) {
		localPos = vec2<f32>(0.0, 0.0);
		uv = vec2<f32>(u.region.x, u.region.y);
	} else if (i == 1) {
		localPos = vec2<f32>(1.0, 0.0);
		uv = vec2<f32>(u.region.z, u.region.y);
	} else if (i == 2) {
		localPos = vec2<f32>(0.0, 1.0);
		uv = vec2<f32>(u.region.x, u.region.w);
	} else if (i == 3) {
		localPos = vec2<f32>(1.0, 0.0);
		uv = vec2<f32>(u.region.z, u.region.y);
	} else if (i == 4) {
		localPos = vec2<f32>(1.0, 1.0);
		uv = vec2<f32>(u.region.z, u.region.w);
	} else {
		localPos = vec2<f32>(0.0, 1.0);
		uv = vec2<f32>(u.region.x, u.region.w);
	}

	// world-space position in pixels
	let worldPos = localPos * u.pos_size.zw + u.pos_size.xy;
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
	let col = textureSample(tex, samp, in_.uv) * in_.tint;
	return col;
}
