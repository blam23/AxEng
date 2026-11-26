#pragma once

#include "glm/glm.hpp"

#include <array>
#include <memory>
#include <span>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace ax
{
    struct ShapeOutline
    {
        glm::vec4 colour{ 0.0f, 0.0f, 0.0f, 1.0f };
        float thickness{ 0.0f };
    };

    // How a shape is filled beyond a flat colour, and how it stands up when drawn as height (ShapeRenderer::draw_height).
    // Positions are in surface units: a shape's own coordinates (unrotated, about its centre, or about its position
    // for triangles and polygons) divided by `unit`, so a pattern keeps its size whatever scale the shape is drawn at.
    struct ShapeSurface
    {
        enum class Paint : std::uint8_t
        {
            Fill,    // The shape's fill colour.
            Linear,  // A gradient along from -> to.
            Radial,  // A gradient out from `from` to `radius`.
            Pattern, // colours[0] and colours[1] mixed by pattern `pattern` (see ShapeRenderer::set_pattern_source).
        };
        enum class Profile : std::uint8_t
        {
            Flat,    // The whole height everywhere.
            Pillow,  // Rounds over near the edge.
            Chamfer, // A straight slope near the edge.
            Dome,    // A quarter circle near the edge.
            Recess,  // Sunk by the height, sloping down from the edge.
        };
        enum class Blend : std::uint8_t
        {
            Replace, // Covers the height beneath.
            Add,     // Adds to the height beneath.
        };

        Paint paint{ Paint::Fill };
        // Gradient stops (or the pattern's two colours), straight alpha, and the stops' offsets (0..1) for gradients.
        std::array<glm::vec4, 4> colours{ glm::vec4{ 1.0f }, glm::vec4{ 1.0f }, glm::vec4{ 1.0f }, glm::vec4{ 1.0f } };
        std::array<float, 4> stops{ 0.0f, 1.0f, 1.0f, 1.0f };
        std::uint32_t stopCount{ 2 };
        // Gradients, in fractions of the shape's own bounds (0, 0 one corner, 1, 1 the other): a linear gradient runs
        // from -> to, a radial one from `from` out to `radius` (a fraction of the bounds' larger side).
        glm::vec2 from{ 0.0f, 0.5f };
        glm::vec2 to{ 1.0f, 0.5f };
        float radius{ 0.5f };
        // Patterns: which one, its cell size and turn in surface units and radians, a seed, how far the mix is pushed
        // from the middle (1 as it comes, 0 an even mix), and four values the pattern reads as it likes.
        std::uint32_t pattern{ 0 };
        float scale{ 1.0f };
        float rotation{ 0.0f };
        float seed{ 0.0f };
        float contrast{ 1.0f };
        glm::vec4 values{ 0.0f };
        // Shape coordinates per surface unit.
        float unit{ 1.0f };

        // Height (draw_height, and the relief light): how far it stands up and over how many units in from its edge
        // the profile rises (both in surface units), and how far the pattern also moves the height (surface units from
        // the pattern's middle to either end). Roughness and emissive are written beside it.
        float height{ 0.0f };
        float bevel{ 0.0f };
        Profile profile{ Profile::Flat };
        Blend blend{ Blend::Replace };
        float bump{ 0.0f };
        float roughness{ 0.5f };
        float emissive{ 0.0f };

        // Whether draw_height draws the shape at all.
        bool raised() const noexcept { return height != 0.0f || bump != 0.0f || roughness != 0.5f || emissive != 0.0f; }
    };
    using ShapeSurfaceRef = std::shared_ptr<const ShapeSurface>;

    enum class LineCap : std::uint8_t
    {
        Butt,   // Ends exactly at the end points.
        Square, // Extends past the end points by half the thickness.
        Round,  // Semi-circular end with a radius of half the thickness.
    };

    struct Circle
    {
        glm::vec2 position{ 0.0f }; // centre
        float radius{ 1.0f };
        glm::vec4 fill{ 1.0f };
        ShapeOutline outline{};
        float rotation{ 0.0f };
        float z{ 0.0f };
        bool screenSpace{ false };
        bool unlit{ false };
        std::uint32_t segments{ 0 };
        glm::vec4 clipRect{ 0.0f, 0.0f, -1.0f, -1.0f };
        ShapeSurfaceRef surface{};
    };

    // Rotation is around the centre.
    struct Ellipse
    {
        glm::vec2 position{ 0.0f }; // centre
        glm::vec2 radii{ 1.0f };
        glm::vec4 fill{ 1.0f };
        ShapeOutline outline{};
        float rotation{ 0.0f };
        float z{ 0.0f };
        bool screenSpace{ false };
        bool unlit{ false };
        std::uint32_t segments{ 0 };
        glm::vec4 clipRect{ 0.0f, 0.0f, -1.0f, -1.0f };
        ShapeSurfaceRef surface{};
    };

    // Position is the top-left corner (like sprites); rotation is around the centre. cornerColours (top left, top
    // right, bottom left, bottom right) blends the fill between four colours instead; it is ignored with an outline or
    // rounded corners.
    struct Rectangle
    {
        glm::vec2 position{ 0.0f };
        glm::vec2 size{ 1.0f };
        float cornerRadius{ 0.0f };
        glm::vec4 fill{ 1.0f };
        ShapeOutline outline{};
        float rotation{ 0.0f };
        float z{ 0.0f };
        bool screenSpace{ false };
        bool unlit{ false };
        std::uint32_t segments{ 0 }; // per rounded corner
        glm::vec4 clipRect{ 0.0f, 0.0f, -1.0f, -1.0f };
        ShapeSurfaceRef surface{};
        std::optional<std::array<glm::vec4, 4>> cornerColours{};
    };

    // Points are relative to position, scaled by scale and then rotated around position.
    struct Triangle
    {
        glm::vec2 position{ 0.0f };
        std::array<glm::vec2, 3> points{};
        glm::vec2 scale{ 1.0f };
        glm::vec4 fill{ 1.0f };
        ShapeOutline outline{};
        float rotation{ 0.0f };
        float z{ 0.0f };
        bool screenSpace{ false };
        bool unlit{ false };
        glm::vec4 clipRect{ 0.0f, 0.0f, -1.0f, -1.0f };
        ShapeSurfaceRef surface{};
    };

    // A straight line segment. fill is the line colour and thickness its width.
    // Rotation is around the midpoint of the segment.
    struct Line
    {
        glm::vec2 start{ 0.0f };
        glm::vec2 end{ 0.0f };
        float thickness{ 1.0f };
        LineCap cap{ LineCap::Butt };
        glm::vec4 fill{ 1.0f };
        ShapeOutline outline{};
        float rotation{ 0.0f };
        float z{ 0.0f };
        bool screenSpace{ false };
        bool unlit{ false };
        std::uint32_t segments{ 0 }; // per round cap
        glm::vec4 clipRect{ 0.0f, 0.0f, -1.0f, -1.0f };
        ShapeSurfaceRef surface{};
    };

    // A simple (non self-intersecting) polygon, convex or concave, in either winding order.
    // Points are relative to position, scaled by scale and then rotated around position.
    struct Polygon
    {
        glm::vec2 position{ 0.0f };
        std::vector<glm::vec2> points{};
        glm::vec2 scale{ 1.0f };
        glm::vec4 fill{ 1.0f };
        ShapeOutline outline{};
        float rotation{ 0.0f };
        float z{ 0.0f };
        bool screenSpace{ false };
        bool unlit{ false };
        glm::vec4 clipRect{ 0.0f, 0.0f, -1.0f, -1.0f };
        ShapeSurfaceRef surface{};
    };

    using Shape = std::variant<Circle, Rectangle, Triangle, Line, Polygon, Ellipse>;
    using ShapeList = std::vector<Shape>;

    // Unlit shapes held back by ShapeRenderer::draw, one list per draw call, so replaying the lists in order
    // draws them as they would have been. Storage is kept between frames.
    class UnlitShapeCapture
    {
    public:
        void clear() noexcept { m_used = 0; }
        std::span<const ShapeList> calls() const noexcept { return { m_calls.data(), m_used }; }
        bool empty() const noexcept { return m_used == 0; }
        // A fresh list for the next draw call's shapes.
        ShapeList& next_call();

    private:
        std::vector<ShapeList> m_calls{};
        std::size_t m_used{};
    };

    inline const ShapeSurface* shape_surface(const Shape& shape)
    {
        return std::visit([](const auto& s) { return s.surface.get(); }, shape);
    }

    inline bool shape_screen_space(const Shape& shape)
    {
        return std::visit([](const auto& s) { return s.screenSpace; }, shape);
    }

    // Unlit shapes (and every screen space shape) are drawn after lighting is applied.
    inline bool shape_unlit(const Shape& shape)
    {
        return std::visit([](const auto& s) { return s.screenSpace || s.unlit; }, shape);
    }
}
