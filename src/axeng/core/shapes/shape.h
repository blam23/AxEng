#pragma once

#include "glm/glm.hpp"

#include <array>
#include <cstdint>
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
        std::uint32_t segments{ 0 };
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
        std::uint32_t segments{ 0 };
    };

    // Position is the top-left corner (like sprites); rotation is around the centre.
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
        std::uint32_t segments{ 0 }; // per rounded corner
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
        std::uint32_t segments{ 0 }; // per round cap
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
    };

    using Shape = std::variant<Circle, Rectangle, Triangle, Line, Polygon, Ellipse>;
    using ShapeList = std::vector<Shape>;
}
