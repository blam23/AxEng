// Round ambient occlusion stand-ins (Window::submit_ao_discs), drawn into a shadow mask as coverage. Each instance is a
// disc (centre x, y and radius, in world pixels) swept back along `sweep.xy` into a capsule, its radius scaled by
// `sweep.z`: the mask camera already shifts the world by the shadows' offset, so sweeping back joins the shadow to
// the thing that casts it. With no sweep it is the disc itself (the stand-in's silhouette).
struct View {
	viewport: vec4<f32>,
	camera: vec4<f32>,
	sweep: vec4<f32>,
};
@group(0) @binding(0) var<uniform> view: View;

// The ambient occlusion cuts, as basic_shader.wgsl reads them. The discs stand on the ground, at depth 0.5 (z 0).
struct AoCut {
	ab: vec4<f32>,
	cd: vec4<f32>,
	depth: vec4<f32>,
};
struct AoCuts {
	shift: vec4<f32>,
	cuts: array<AoCut, 16>,
};
@group(0) @binding(1) var<uniform> aoCuts: AoCuts;

fn ao_cut_side(p: vec2<f32>, a: vec2<f32>, b: vec2<f32>) -> f32 {
	let edge = b - a;
	let to = p - a;
	return edge.x * to.y - edge.y * to.x;
}

fn ao_cut(viewWorld: vec2<f32>) -> bool {
	let p = viewWorld + aoCuts.shift.xy;
	let count = min(u32(aoCuts.shift.z), 16u);
	for (var i = 0u; i < count; i = i + 1u) {
		let cut = aoCuts.cuts[i];
		if (0.5 >= cut.depth.x) {
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

struct VSOut {
	@builtin(position) pos: vec4<f32>,
	@location(0) world: vec2<f32>,
	@location(1) @interpolate(flat) disc: vec3<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) index: u32, @location(0) disc: vec3<f32>) -> VSOut {
	let radius = disc.z * view.sweep.z;
	let a = disc.xy;
	let b = disc.xy - view.sweep.xy;
	// A pixel more all round, so the edge is not lost to rasterisation.
	let low = min(a, b) - vec2<f32>(radius + 1.0);
	let high = max(a, b) + vec2<f32>(radius + 1.0);
	var corners = array<vec2<f32>, 6>(vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0), vec2<f32>(0.0, 1.0),
		vec2<f32>(0.0, 1.0), vec2<f32>(1.0, 0.0), vec2<f32>(1.0, 1.0));
	let world = mix(low, high, corners[index]);
	// As the sprites' world positions (basic_shader.wgsl).
	let screen = (world - view.camera.xy) * view.camera.z + view.viewport.xy * 0.5;
	var out: VSOut;
	out.pos = vec4<f32>((screen.x / view.viewport.x) * 2.0 - 1.0, 1.0 - (screen.y / view.viewport.y) * 2.0, 0.5, 1.0);
	out.world = world;
	out.disc = vec3<f32>(a, radius);
	return out;
}

@fragment
fn fs_main(in_: VSOut) -> @location(0) vec4<f32> {
	let a = in_.disc.xy;
	let along = -view.sweep.xy;
	let t = clamp(dot(in_.world - a, along) / max(dot(along, along), 1e-6), 0.0, 1.0);
	if (distance(in_.world, a + along * t) > in_.disc.z || ao_cut(in_.world)) {
		discard;
	}
	return vec4<f32>(1.0);
}
