#pragma once

#include "axeng/core/window.h"
#include "axeng/core/shapes/shape.h"

#include <memory>
#include <string>

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
        // Draws only the shapes whose z lies in [zMin, zMax), such as one layer of a pass drawn in z ranges.
        static void draw(wgpu::RenderPassEncoder& pass, const ShapeList& shapes, float zMin, float zMax);

        // Draws the shapes' height instead of their colour, for a height map: only shapes whose surface is raised
        // (ShapeSurface::raised) draw, each writing height (in shape coordinates, so slopes are true) to red, roughness to green and emissive to blue,
        // premultiplied by coverage in alpha (a ShapeSurface::Blend::Add shape leaves alpha at 0 and adds its height).
        // The target should be a float format; nothing is depth tested.
        static void draw_height(wgpu::RenderPassEncoder& pass, const ShapeList& shapes);

        // WGSL defining the patterns from id 64 up (ShapeSurface::Paint::Pattern), as
        //     fn user_pattern(id: u32, p: vec2<f32>, values: vec4<f32>, seed: f32, aa: f32) -> f32
        // p is in pattern cells and aa the cells a pixel covers (to soften edges over); it returns the mix from the
        // first colour to the second (0..1). It may call the helpers in shapes.wgsl (pattern_hash, pattern_noise,
        // pattern_fbm, pattern_cells, pattern_edge, builtin_pattern). The pipelines are built again with it; an empty
        // source restores the default (every user pattern 0). Returns false if they could not be.
        static bool set_pattern_source(std::string source);
        static const std::string& pattern_source();

        // Shades shapes with a surface by their own height as draw() draws them, as if lit from `direction` (towards
        // the light, z out of the screen), for previewing height without a height map. Strength 0 (the default) turns
        // it off; 1 is full shading. It applies from the next begin_frame.
        static void set_relief_light(glm::vec3 direction, float strength);

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
