#pragma once

#include "axeng/core/window.h"
#include "axeng/core/shapes/shape.h"

namespace ax
{
    struct ShapeRendererConfig
    {
        wgpu::TextureFormat colourFormat{ wgpu::TextureFormat::Undefined };
        // Undefined disables depth testing (z then only orders shapes within a draw call).
        wgpu::TextureFormat depthFormat{ wgpu::TextureFormat::Depth24Plus };
        std::uint32_t sampleCount{ 4 };

        bool operator==(const ShapeRendererConfig&) const = default;
    };

    struct ShapeView
    {
        glm::vec2 viewport{ 0.0f };
        glm::vec2 cameraPosition{ 0.0f };
        float zoom{ 1.0f };
    };

    struct ShapeDrawStats
    {
        std::uint32_t drawCalls{ 0 };
        std::uint32_t vertices{ 0 };
        std::uint32_t indices{ 0 };
    };

    class ShapeRenderer
    {
    public:
        // Creates the pipelines. Calling again with the same device and config is a no-op.
        static bool init(const wgpu::Device& device, const ShapeRendererConfig& config);
        static void shutdown();
        static bool is_initialised();

        // Sets the view for this frame and recycles the upload buffer. Must only be called once the
        // command buffer containing the previous frame's shape draws has been submitted.
        static void begin_frame(const ShapeView& view);

        static void draw(wgpu::RenderPassEncoder& pass, const ShapeList& shapes);

        // Statistics for the most recent draw() call.
        static ShapeDrawStats last_draw_stats();
    };
}
