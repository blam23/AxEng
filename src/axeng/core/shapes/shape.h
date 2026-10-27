#pragma once

#include "glm/glm.hpp"

#include <array>
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

    // Unlit shapes (and every screen space shape) are drawn after lighting is applied.
    inline bool shape_unlit(const Shape& shape)
    {
        return std::visit([](const auto& s) { return s.screenSpace || s.unlit; }, shape);
    }
}
