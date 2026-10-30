# Compute programs

Project native modules can use `ax::ComputeProgram` with the window's WebGPU device and queue. The program owns a
WGSL shader's compute pipelines and their shared bind-group layout; storage buffers and uniforms remain owned by the
calling system.

```cpp
ax::ComputeProgram compute;
compute.initialise(window.device(), wgslSource, bindings, entryPoints, "FlowField");
auto group = compute.create_bind_group(window.device(), entries, "FlowFieldBindings");

wgpu::CommandEncoderDescriptor descriptor{};
auto encoder = window.device().CreateCommandEncoder(&descriptor);
const std::array<ax::ComputeDispatch, 2> dispatches{
    ax::ComputeDispatch{ "prepare", group, 16 },
    ax::ComputeDispatch{ "solve", group, 16 },
};
compute.encode_many(encoder, dispatches);
const auto commands = encoder.Finish();
window.queue().Submit(1, &commands);
```

`encode_many` records several entry points and workgroup sizes in one compute pass. `Window::surface_format()` and
`Window::sample_count()` expose the render target configuration to project-owned render pipelines that consume
compute output. Use `Window::get_post_scene_render_event_handler()` to draw that output after queued scene materials
have rendered while the scene pass is still open. This is useful for compute-driven overlays that should remain
visible over the world; the ordinary render event fires before those materials.
