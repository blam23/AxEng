#include "axeng/core/lua/bindings/lua_shape_bindings.h"

#include "axeng/core/shapes/shape_renderer.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>

namespace
{
	using namespace std::string_view_literals;

	class ShapeParser
	{
	public:
		ShapeParser(sol::table table, std::string_view type)
			: m_table(std::move(table)), m_type(type)
		{
		}

		[[noreturn]] void fail(std::string_view key, std::string_view message) const
		{
			throw sol::error(std::format("invalid {} shape: '{}' {}", m_type, key, message));
		}

		sol::object field(const char* key) const
		{
			return m_table.get<sol::object>(key);
		}

		static bool is_nil(const sol::object& o)
		{
			return !o.valid() || o.get_type() == sol::type::lua_nil || o.get_type() == sol::type::none;
		}

		std::optional<float> opt_number(const char* key) const
		{
			const auto o{ field(key) };
			if (is_nil(o))
				return std::nullopt;
			if (o.get_type() != sol::type::number)
				fail(key, "must be a number");
			return o.as<float>();
		}

		float number(const char* key) const
		{
			const auto value{ opt_number(key) };
			if (!value)
				fail(key, "is required");
			return *value;
		}

		std::optional<bool> opt_bool(const char* key) const
		{
			const auto o{ field(key) };
			if (is_nil(o))
				return std::nullopt;
			if (o.get_type() != sol::type::boolean)
				fail(key, "must be a boolean");
			return o.as<bool>();
		}

		// vec2 usertype, { x = .., y = .. } or { .., .. }. Numbers are accepted when allowScalar is set.
		glm::vec2 to_vec2(const sol::object& o, std::string_view key, bool allowScalar = false) const
		{
			if (o.is<glm::vec2>())
				return o.as<glm::vec2>();

			if (allowScalar && o.get_type() == sol::type::number)
				return glm::vec2{ o.as<float>() };

			if (o.get_type() == sol::type::table)
			{
				const auto t{ o.as<sol::table>() };
				const auto x{ component(t, "x", 1) };
				const auto y{ component(t, "y", 2) };
				if (x && y)
					return { *x, *y };
			}

			fail(key, allowScalar
				? "must be a number, vec2, { x, y } or { x = .., y = .. }"
				: "must be a vec2, { x, y } or { x = .., y = .. }");
		}

		std::optional<glm::vec2> opt_vec2(const char* key, bool allowScalar = false) const
		{
			const auto o{ field(key) };
			if (is_nil(o))
				return std::nullopt;
			return to_vec2(o, key, allowScalar);
		}

		glm::vec2 vec2(const char* key) const
		{
			const auto value{ opt_vec2(key) };
			if (!value)
				fail(key, "is required");
			return *value;
		}

		// Reads `key` as a vec2, falling back to separate number fields (e.g. x/y). Returns
		// std::nullopt when neither form is present.
		std::optional<glm::vec2> opt_vec2_or(const char* key, const char* xKey, const char* yKey) const
		{
			if (auto value{ opt_vec2(key) })
				return value;

			const auto x{ opt_number(xKey) };
			const auto y{ opt_number(yKey) };
			if (x.has_value() != y.has_value())
				fail(x ? yKey : xKey, std::format("is required when '{}' is set", x ? xKey : yKey));
			if (!x)
				return std::nullopt;
			return glm::vec2{ *x, *y };
		}

		glm::vec2 vec2_or(const char* key, const char* xKey, const char* yKey) const
		{
			const auto value{ opt_vec2_or(key, xKey, yKey) };
			if (!value)
				fail(key, std::format("is required (or '{}' and '{}')", xKey, yKey));
			return *value;
		}

		// vec4 usertype, { r = .., g = .., b = .., a = .. } or { r, g, b, a }. Alpha defaults to 1.
		glm::vec4 to_colour(const sol::object& o, std::string_view key) const
		{
			if (o.is<glm::vec4>())
				return o.as<glm::vec4>();

			if (o.get_type() == sol::type::table)
			{
				const auto t{ o.as<sol::table>() };
				const auto r{ component(t, "r", 1) };
				const auto g{ component(t, "g", 2) };
				const auto b{ component(t, "b", 3) };
				const auto a{ component(t, "a", 4) };
				if (r && g && b)
					return { *r, *g, *b, a.value_or(1.0f) };
			}

			fail(key, "must be a vec4, { r, g, b, a } or { r = .., g = .., b = .., a = .. }");
		}

		std::optional<glm::vec4> opt_colour(const char* key) const
		{
			const auto o{ field(key) };
			if (is_nil(o))
				return std::nullopt;
			return to_colour(o, key);
		}

		std::vector<glm::vec2> points(const char* key) const
		{
			const auto o{ field(key) };
			if (o.get_type() != sol::type::table)
				fail(key, "must be an array of points");

			const auto t{ o.as<sol::table>() };
			std::vector<glm::vec2> result;
			result.reserve(t.size());
			for (std::size_t i = 1; i <= t.size(); ++i)
				result.push_back(to_vec2(t.get<sol::object>(i), std::format("{}[{}]", key, i)));
			return result;
		}

		template <typename S>
		void common(S& shape) const
		{
			if (const auto fill{ opt_colour("fill") })
				shape.fill = *fill;

			const auto outline{ field("outline") };
			if (!is_nil(outline))
			{
				if (outline.get_type() != sol::type::table)
					fail("outline", "must be a table { colour = .., thickness = .. }");

				const ShapeParser outlineParser{ outline.as<sol::table>(), m_type };
				if (const auto colour{ outlineParser.opt_colour("colour") })
					shape.outline.colour = *colour;
				else if (const auto color{ outlineParser.opt_colour("color") })
					shape.outline.colour = *color;
				shape.outline.thickness = outlineParser.opt_number("thickness").value_or(1.0f);
			}

			if (const auto rotation{ opt_number("rotation") })
				shape.rotation = *rotation;
			if (const auto z{ opt_number("z") })
				shape.z = *z;
			if (const auto screenSpace{ opt_bool("screen_space") })
				shape.screenSpace = *screenSpace;

			if constexpr (requires { shape.segments; })
			{
				if (const auto segments{ opt_number("segments") })
				{
					if (*segments < 0.0f)
						fail("segments", "must not be negative");
					shape.segments = static_cast<std::uint32_t>(*segments);
				}
			}
		}

		ax::LineCap cap() const
		{
			const auto o{ field("cap") };
			if (is_nil(o))
				return ax::LineCap::Butt;
			if (o.get_type() == sol::type::string)
			{
				const auto name{ o.as<std::string_view>() };
				if (name == "butt"sv)
					return ax::LineCap::Butt;
				if (name == "square"sv)
					return ax::LineCap::Square;
				if (name == "round"sv)
					return ax::LineCap::Round;
			}
			fail("cap", "must be one of \"butt\", \"square\" or \"round\"");
		}

	private:
		static std::optional<float> component(const sol::table& t, const char* name, int index)
		{
			auto o{ t.get<sol::object>(name) };
			if (is_nil(o))
				o = t.get<sol::object>(index);
			if (o.get_type() != sol::type::number)
				return std::nullopt;
			return o.as<float>();
		}

		sol::table m_table;
		std::string_view m_type;
	};

	using ax::lua::bindings::LuaShapeList;

	ax::Shape& shape_at(LuaShapeList& list, std::size_t index)
	{
		if (index < 1 || index > list.shapes.size())
			throw sol::error(std::format("shape index {} is out of range (list has {} shapes)", index, list.shapes.size()));
		return list.shapes[index - 1];
	}

	glm::vec4 rgba(float r, float g, float b, sol::optional<float> a)
	{
		return { r, g, b, a.value_or(1.0f) };
	}

	sol::table stats_table(sol::this_state ts)
	{
		const auto stats{ ax::ShapeRenderer::last_draw_stats() };
		sol::state_view lua{ ts };
		auto table{ lua.create_table() };
		table["draw_calls"] = stats.drawCalls;
		table["vertices"] = stats.vertices;
		table["indices"] = stats.indices;
		return table;
	}
}

ax::Shape ax::lua::bindings::shape_from_table(const sol::table& description)
{
	const auto typeObject{ description.get<sol::object>("type") };
	if (typeObject.get_type() != sol::type::string)
		throw sol::error("invalid shape: 'type' must be one of \"circle\", \"ellipse\", \"rectangle\", \"triangle\", \"line\" or \"polygon\"");

	const auto type{ typeObject.as<std::string>() };
	const ShapeParser p{ description, type };

	if (type == "circle")
	{
		ax::Circle circle{};
		circle.position = p.vec2_or("position", "x", "y");
		circle.radius = p.number("radius");
		p.common(circle);
		return circle;
	}

	if (type == "ellipse")
	{
		ax::Ellipse ellipse{};
		ellipse.position = p.vec2_or("position", "x", "y");
		ellipse.radii = p.vec2_or("radii", "rx", "ry");
		p.common(ellipse);
		return ellipse;
	}

	if (type == "rectangle" || type == "rect")
	{
		ax::Rectangle rect{};
		rect.position = p.vec2_or("position", "x", "y");
		rect.size = p.vec2_or("size", "width", "height");
		rect.cornerRadius = p.opt_number("corner_radius").value_or(0.0f);
		p.common(rect);
		return rect;
	}

	if (type == "triangle")
	{
		ax::Triangle triangle{};
		triangle.position = p.opt_vec2_or("position", "x", "y").value_or(glm::vec2{ 0.0f });
		const auto points{ p.points("points") };
		if (points.size() != 3)
			p.fail("points", std::format("must contain exactly 3 points, got {}", points.size()));
		std::copy(points.begin(), points.end(), triangle.points.begin());
		triangle.scale = p.opt_vec2("scale", true).value_or(glm::vec2{ 1.0f });
		p.common(triangle);
		return triangle;
	}

	if (type == "polygon")
	{
		ax::Polygon polygon{};
		polygon.position = p.opt_vec2_or("position", "x", "y").value_or(glm::vec2{ 0.0f });
		polygon.points = p.points("points");
		polygon.scale = p.opt_vec2("scale", true).value_or(glm::vec2{ 1.0f });
		p.common(polygon);
		return polygon;
	}

	if (type == "line")
	{
		ax::Line line{};
		line.start = p.vec2_or("start", "x1", "y1");
		// "end" is a Lua keyword, so "finish" is accepted as a friendlier alternative to ["end"].
		if (const auto finish{ p.opt_vec2("finish") })
			line.end = *finish;
		else
			line.end = p.vec2_or("end", "x2", "y2");
		line.thickness = p.opt_number("thickness").value_or(1.0f);
		line.cap = p.cap();
		p.common(line);
		return line;
	}

	throw sol::error(std::format("invalid shape: unknown type \"{}\"", type));
}

void ax::lua::bindings::shapes_from_table(const sol::table& descriptions, ax::ShapeList& out)
{
	if (!ShapeParser::is_nil(descriptions.get<sol::object>("type")))
	{
		out.push_back(shape_from_table(descriptions));
		return;
	}

	const auto count{ descriptions.size() };
	out.reserve(out.size() + count);
	for (std::size_t i = 1; i <= count; ++i)
	{
		const auto entry{ descriptions.get<sol::object>(i) };
		if (entry.get_type() != sol::type::table)
			throw sol::error(std::format("invalid shape at index {}: expected a table", i));
		out.push_back(shape_from_table(entry.as<sol::table>()));
	}
}

sol::table ax::lua::bindings::create_shape_table(sol::state& state)
{
	state.new_usertype<LuaShapeList>
	(
		"shape_list",
		sol::constructors<LuaShapeList()>(),

		"add",
		[](LuaShapeList& list, const sol::table& description) -> std::size_t
		{
			list.shapes.push_back(shape_from_table(description));
			return list.shapes.size();
		},
		"set",
		[](LuaShapeList& list, std::size_t index, const sol::table& description)
		{
			shape_at(list, index) = shape_from_table(description);
		},
		"remove",
		[](LuaShapeList& list, std::size_t index)
		{
			shape_at(list, index);
			list.shapes.erase(list.shapes.begin() + static_cast<std::ptrdiff_t>(index - 1));
		},
		"clear", [](LuaShapeList& list) { list.shapes.clear(); },
		"reserve", [](LuaShapeList& list, std::size_t count) { list.shapes.reserve(count); },
		"size", [](const LuaShapeList& list) { return list.shapes.size(); },
		sol::meta_function::length, [](const LuaShapeList& list) { return list.shapes.size(); },

		// Fast per-shape updates that avoid re-parsing a whole description each frame.
		// For lines the position is the start point; the end point moves with it.
		"set_position",
		[](LuaShapeList& list, std::size_t index, float x, float y)
		{
			std::visit([x, y](auto& shape)
			{
				if constexpr (std::is_same_v<std::decay_t<decltype(shape)>, ax::Line>)
				{
					const glm::vec2 offset{ glm::vec2{ x, y } - shape.start };
					shape.start += offset;
					shape.end += offset;
				}
				else
				{
					shape.position = { x, y };
				}
			}, shape_at(list, index));
		},
		"get_position",
		[](LuaShapeList& list, std::size_t index) -> std::tuple<float, float>
		{
			return std::visit([](const auto& shape) -> std::tuple<float, float>
			{
				if constexpr (std::is_same_v<std::decay_t<decltype(shape)>, ax::Line>)
					return { shape.start.x, shape.start.y };
				else
					return { shape.position.x, shape.position.y };
			}, shape_at(list, index));
		},
		"set_rotation",
		[](LuaShapeList& list, std::size_t index, float rotation)
		{
			std::visit([rotation](auto& shape) { shape.rotation = rotation; }, shape_at(list, index));
		},
		"set_z",
		[](LuaShapeList& list, std::size_t index, float z)
		{
			std::visit([z](auto& shape) { shape.z = z; }, shape_at(list, index));
		},
		"set_fill",
		[](LuaShapeList& list, std::size_t index, float r, float g, float b, sol::optional<float> a)
		{
			const auto colour{ rgba(r, g, b, a) };
			std::visit([&colour](auto& shape) { shape.fill = colour; }, shape_at(list, index));
		},
		"set_outline",
		[](LuaShapeList& list, std::size_t index, float thickness, float r, float g, float b, sol::optional<float> a)
		{
			const ax::ShapeOutline outline{ .colour = rgba(r, g, b, a), .thickness = thickness };
			std::visit([&outline](auto& shape) { shape.outline = outline; }, shape_at(list, index));
		}
	);

	auto shapes{ state.create_table() };

	shapes["list"] = []() { return LuaShapeList{}; };

	shapes["cap"] = state.create_table_with
	(
		"butt", "butt",
		"square", "square",
		"round", "round"
	);

	// Draws immediately into the pass; everything passed in a single call is batched together.
	// Accepts a shape_list, a single shape description or an array of shape descriptions.
	shapes["draw"] = sol::overload
	(
		[](wgpu::RenderPassEncoder& pass, const LuaShapeList& list)
		{
			ax::ShapeRenderer::draw(pass, list.shapes);
		},
		[](wgpu::RenderPassEncoder& pass, const sol::table& descriptions)
		{
			// Reused between calls to avoid reallocating every frame.
			static ax::ShapeList scratch;
			scratch.clear();
			shapes_from_table(descriptions, scratch);
			ax::ShapeRenderer::draw(pass, scratch);
		}
	);

	shapes["stats"] = &stats_table;

	return shapes;
}

void ax::lua::bindings::setup_shape_bindings(ax::Application&, sol::state& state)
{
	auto app_table{ state["app"].get<sol::table>() };
	app_table["shapes"] = create_shape_table(state);
}
