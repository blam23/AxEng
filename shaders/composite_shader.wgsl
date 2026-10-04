@group(0) @binding(0) var accumulationTexture: texture_multisampled_2d<f32>;
@group(0) @binding(1) var revealageTexture: texture_multisampled_2d<f32>;

struct VSOut {
	@builtin(position) pos: vec4<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VSOut {
	var positions = array<vec2<f32>, 3>(
		vec2<f32>(-1.0, -1.0),
		vec2<f32>(3.0, -1.0),
		vec2<f32>(-1.0, 3.0)
	);
	var out: VSOut;
	out.pos = vec4<f32>(positions[vertexIndex], 0.0, 1.0);
	return out;
}

@fragment
fn fs_main(
	@builtin(position) position: vec4<f32>,
	@builtin(sample_index) sampleIndex: u32
) -> @location(0) vec4<f32> {
	let pixel = vec2<i32>(position.xy);
	let sampleIdx = i32(sampleIndex);
	let accumulation = textureLoad(accumulationTexture, pixel, sampleIdx);
	let revealage = textureLoad(revealageTexture, pixel, sampleIdx).r;
	let alpha = 1.0 - revealage;
	let color = accumulation.rgb / max(accumulation.a, 0.00001);
	return vec4<f32>(color, alpha);
}
