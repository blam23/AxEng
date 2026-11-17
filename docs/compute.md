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

## Indirect dispatch

A compute pass can size the next one on the GPU. Set `ComputeDispatch::indirect` (and `indirectOffset`) to a buffer
holding three `u32` workgroup counts, or call `encode_indirect`:

```cpp
compute.encode(encoder, "count_alive", group, 1);            // writes args[0..2]
encoder.CopyBufferToBuffer(args, 0, indirectArgs, 0, 12);    // see below
compute.encode_indirect(encoder, "update_alive", group, indirectArgs);
```

WebGPU does not allow a buffer to be bound as read-write storage and used as the indirect source of the same dispatch.
Have the shader write the counts into a storage buffer, copy them into a buffer created with
`BufferUsage::Indirect | BufferUsage::CopyDst`, and dispatch from that copy.

## Reading results back without waiting

`ax::GpuReadbackRing` (`gpu_readback_ring.h`) brings small results (counters, event records) back to the CPU without
stalling. Each copy lands in its own slot of a ring of mappable buffers. Once the GPU has finished, the slot is mapped,
and `poll()` hands it over, usually one or two frames later.

```cpp
ax::GpuReadbackRing ring;
ring.initialise(window.device(), sizeof(TickSummary), 8, "Tick summaries");

// Each tick: record the copy with the tick's other GPU work, submit, then start mapping.
ring.encode_copy(encoder, summaryBuffer, 0, tick);   // false when all 8 slots are still in flight
window.queue().Submit(1, &commands);
ring.submitted();

// Every frame: take whatever has arrived, oldest tick first.
ring.poll([](std::uint64_t tick, std::span<const std::byte> data) { /* ... */ });
```

The source buffer needs `BufferUsage::CopySrc`. Data is always delivered in tag order. A slot that is still mapping
holds back later ones, so consumers can apply results at a fixed delay (for example, tick T's results at tick T + 8)
and every machine reacts on the same tick. The ring never calls a blocking wait. Mapping completes through the window's
per-frame device tick on native, and through the browser's event loop on the web.

## Device limits

The window asks for the adapter's own storage limits: storage buffer binding size and buffer size (both capped at
1 GiB), up to 16 storage buffers per stage, and the adapter's workgroup storage size. Code that must run everywhere,
the browser included, should stay within the WebGPU defaults: 8 storage buffers per shader stage, 128 MiB per storage
binding, 16 KiB of workgroup storage and 256 invocations per workgroup.
