@group(0) @binding(0) var<uniform> colour : vec4f;
@group(0) @binding(1) var t : texture_2d<f32>;
@group(0) @binding(2) var s : sampler;

@vertex
fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> @builtin(position) vec4f {
    var p = vec2f(1.0, 1.0);
    if (in_vertex_index == 0u) {
        p = vec2f(-1.0, -1.0);
    } else if (in_vertex_index == 1u) {
        p = vec2f(-1.0, 1.0);
    } else if (in_vertex_index == 2u) {
        p = vec2f(1.0, -1.0);
    }
    return vec4f(p, 0.0, 1.0);
}
 
@fragment
fn fs_main(@builtin(position) pos : vec4f) -> @location(0) vec4<f32> {
    var ret : vec4f = textureSample(t, s, (pos.xy) * 0.001);
    return ret * colour;
}