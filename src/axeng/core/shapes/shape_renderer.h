#pragma once

#include "axeng/core/window.h"
#include "axeng/core/shapes/shape.h"

#include <memory>

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

    // How draw() fits a shape's outline (see ShapeRenderer::outline_fit).
    enum class OutlineFit
    {
        None,    // No outline (or nothing drawn at all).
        Ring,    // A ring inside the shape's edge, the fill inside it.
        Clamped, // A convex shape whose outline is thicker than its corners: the ring's inner corners are clamped.
        Covers,  // The outline covers the whole shape, which is drawn in the outline colour.
    };

    // Shapes tessellated once and kept on the GPU (ShapeRenderer::build_static): world space, lit shapes that stay
    // the same from frame to frame.
    struct StaticShapeMesh;

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

        // While set, draw() holds unlit shapes (see shape_unlit) back in capture instead of drawing them, so they
        // can be drawn after lighting. Pass nullptr to draw everything again.
        static void set_unlit_capture(UnlitShapeCapture* capture) noexcept;

        // While set, draw() holds screen space shapes back in capture, ahead of set_unlit_capture, so the interface
        // can be drawn in a pass of its own. Pass nullptr to stop.
        static void set_screen_capture(UnlitShapeCapture* capture) noexcept;

        // Statistics for the most recent draw() call.
        static ShapeDrawStats last_draw_stats();
        // Tessellates the shapes once, as draw() would at pixelScale screen pixels per unit (the zoom they should
        // stay smooth at), into a GPU buffer of their own. Screen space shapes are left out. Null when nothing is
        // drawn or the renderer is not initialised. Window thread only.
        static std::shared_ptr<const StaticShapeMesh> build_static(const ShapeList& shapes, float pixelScale);
        // Draws a static mesh with this frame's view: its opaque parts, then its translucent ones.
        static void draw_static(wgpu::RenderPassEncoder& pass, const StaticShapeMesh& mesh);
        // How draw() fits the shape's outline at pixelScale screen pixels per unit (1 for screen space shapes, whose
        // outlines are in pixels). Needs no device: tools can ask which shapes an outline covers or clamps.
        static OutlineFit outline_fit(const Shape& shape, float pixelScale);
    };
}
