struct View {
	viewport: vec4<f32>,
	camera: vec4<f32>,
};

@group(0) @binding(0) var<uniform> view : View;

struct VSIn {
	@location(0) pos: vec2<f32>,
	@location(1) z: f32,
	@location(2) screen_space: u32,
	@location(3) colour: vec4<f32>,
};

struct VSOut {
	@builtin(position) pos: vec4<f32>,
	@location(0) colour: vec4<f32>,
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
	return out;
}

@fragment
fn fs_main(in_: VSOut) -> @location(0) vec4<f32> {
	return in_.colour;
}
