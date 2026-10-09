#include <gtest/gtest.h>

#include "axeng/core/lua/bindings/lua_shape_bindings.h"
#include "axeng/core/lua/bindings/lua_vector_bindings.h"

#include <string>

namespace
{
	class LuaShapeBindings : public testing::Test
	{
	protected:
		void SetUp() override
		{
			m_state.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
			ax::lua::bindings::setup_vector_bindings(m_state);
			m_state["shapes"] = ax::lua::bindings::create_shape_table(m_state);
		}

		ax::Shape parse(const std::string& description)
		{
			const sol::table table{ m_state.script("return " + description) };
			return ax::lua::bindings::shape_from_table(table);
		}

		template <typename T>
		T parse_as(const std::string& description)
		{
			return std::get<T>(parse(description));
		}

		// Returns the error message raised when parsing the description, or "" if it succeeded.
		std::string parse_error(const std::string& description)
		{
			const auto result{ m_state.safe_script(
				"local ok, err = pcall(shapes.list().add, shapes.list(), " + description + ") return ok and '' or tostring(err)",
				sol::script_pass_on_error) };
			if (!result.valid())
			{
				const sol::error error = result;
				ADD_FAILURE() << error.what();
				return {};
			}
			return result.get<std::string>();
		}

		void run(const std::string& code)
		{
			const auto result{ m_state.safe_script(code, sol::script_pass_on_error) };
			if (!result.valid())
			{
				const sol::error error = result;
				FAIL() << error.what();
			}
		}

		sol::state m_state;
	};

	void expect_vec(const glm::vec2& actual, const glm::vec2& expected)
	{
		EXPECT_FLOAT_EQ(actual.x, expected.x);
		EXPECT_FLOAT_EQ(actual.y, expected.y);
	}

	void expect_vec(const glm::vec4& actual, const glm::vec4& expected)
	{
		EXPECT_FLOAT_EQ(actual.x, expected.x);
		EXPECT_FLOAT_EQ(actual.y, expected.y);
		EXPECT_FLOAT_EQ(actual.z, expected.z);
		EXPECT_FLOAT_EQ(actual.w, expected.w);
	}
}

TEST_F(LuaShapeBindings, CircleWithAllCommonFields)
{
	const auto circle{ parse_as<ax::Circle>(R"({ type = "circle", position = { 1, 2 }, radius = 3, fill = { 0.1, 0.2, 0.3, 0.4 },
		outline = { colour = { r = 1, g = 0, b = 0 }, thickness = 2 }, rotation = 0.5, z = 7, screen_space = true, segments = 24 })") };

	expect_vec(circle.position, { 1, 2 });
	EXPECT_FLOAT_EQ(circle.radius, 3);
	expect_vec(circle.fill, { 0.1f, 0.2f, 0.3f, 0.4f });
	expect_vec(circle.outline.colour, { 1, 0, 0, 1 });
	EXPECT_FLOAT_EQ(circle.outline.thickness, 2);
	EXPECT_FLOAT_EQ(circle.rotation, 0.5f);
	EXPECT_FLOAT_EQ(circle.z, 7);
	EXPECT_TRUE(circle.screenSpace);
	EXPECT_EQ(circle.segments, 24u);
}

TEST_F(LuaShapeBindings, DefaultsMatchNativeShapes)
{
	const auto circle{ parse_as<ax::Circle>(R"({ type = "circle", x = 0, y = 0, radius = 1 })") };
	const ax::Circle native{};
	expect_vec(circle.fill, native.fill);
	EXPECT_FLOAT_EQ(circle.outline.thickness, native.outline.thickness);
	EXPECT_FLOAT_EQ(circle.rotation, native.rotation);
	EXPECT_FLOAT_EQ(circle.z, native.z);
	EXPECT_EQ(circle.screenSpace, native.screenSpace);
	EXPECT_EQ(circle.segments, native.segments);

	const auto line{ parse_as<ax::Line>(R"({ type = "line", start = { 0, 0 }, finish = { 1, 1 } })") };
	EXPECT_FLOAT_EQ(line.thickness, ax::Line{}.thickness);
	EXPECT_EQ(line.cap, ax::LineCap::Butt);
}

TEST_F(LuaShapeBindings, VectorAndColourFormsAreEquivalent)
{
	for (const auto* position : { "vec2:new(4, 5)", "{ 4, 5 }", "{ x = 4, y = 5 }" })
	{
		SCOPED_TRACE(position);
		const auto ellipse{ parse_as<ax::Ellipse>(std::string("{ type = 'ellipse', radii = { 1, 2 }, position = ") + position + " }") };
		expect_vec(ellipse.position, { 4, 5 });
		expect_vec(ellipse.radii, { 1, 2 });
	}

	for (const auto* fill : { "vec4:new(0.5, 0.25, 1, 1)", "{ 0.5, 0.25, 1 }", "{ r = 0.5, g = 0.25, b = 1, a = 1 }" })
	{
		SCOPED_TRACE(fill);
		const auto rect{ parse_as<ax::Rectangle>(std::string("{ type = 'rect', x = 0, y = 0, width = 1, height = 1, fill = ") + fill + " }") };
		expect_vec(rect.fill, { 0.5f, 0.25f, 1, 1 });
	}
}

TEST_F(LuaShapeBindings, RectangleAliasesAndCornerRadius)
{
	const auto rect{ parse_as<ax::Rectangle>(R"({ type = "rectangle", position = { 1, 2 }, size = { 3, 4 }, corner_radius = 1.5 })") };
	expect_vec(rect.position, { 1, 2 });
	expect_vec(rect.size, { 3, 4 });
	EXPECT_FLOAT_EQ(rect.cornerRadius, 1.5f);
}

TEST_F(LuaShapeBindings, TriangleAndPolygonPointsAndScale)
{
	const auto triangle{ parse_as<ax::Triangle>(R"({ type = "triangle", points = { { 0, 0 }, vec2:new(1, 0), { x = 0, y = 1 } }, scale = 2 })") };
	expect_vec(triangle.position, { 0, 0 });
	expect_vec(triangle.points[1], { 1, 0 });
	expect_vec(triangle.points[2], { 0, 1 });
	expect_vec(triangle.scale, { 2, 2 });

	const auto polygon{ parse_as<ax::Polygon>(R"({ type = "polygon", x = 3, y = 4, points = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }, scale = { 2, 3 } })") };
	expect_vec(polygon.position, { 3, 4 });
	ASSERT_EQ(polygon.points.size(), 4u);
	expect_vec(polygon.points[2], { 1, 1 });
	expect_vec(polygon.scale, { 2, 3 });
}

TEST_F(LuaShapeBindings, LineEndPointFormsAndCaps)
{
	const auto a{ parse_as<ax::Line>(R"({ type = "line", start = { 1, 2 }, ["end"] = { 3, 4 }, cap = "square", thickness = 5 })") };
	expect_vec(a.start, { 1, 2 });
	expect_vec(a.end, { 3, 4 });
	EXPECT_EQ(a.cap, ax::LineCap::Square);
	EXPECT_FLOAT_EQ(a.thickness, 5);

	const auto b{ parse_as<ax::Line>(R"({ type = "line", x1 = 1, y1 = 2, x2 = 3, y2 = 4, cap = shapes.cap.round })") };
	expect_vec(b.start, { 1, 2 });
	expect_vec(b.end, { 3, 4 });
	EXPECT_EQ(b.cap, ax::LineCap::Round);
}

TEST_F(LuaShapeBindings, ShapesFromTableAcceptsSingleShapeOrArray)
{
	ax::ShapeList list;
	ax::lua::bindings::shapes_from_table(m_state.script(R"(return { type = "circle", x = 0, y = 0, radius = 1 })"), list);
	ax::lua::bindings::shapes_from_table(m_state.script(R"(return {
		{ type = "circle", x = 0, y = 0, radius = 1 },
		{ type = "line", x1 = 0, y1 = 0, x2 = 1, y2 = 1 },
	})"), list);
	ASSERT_EQ(list.size(), 3u);
	EXPECT_TRUE(std::holds_alternative<ax::Circle>(list[0]));
	EXPECT_TRUE(std::holds_alternative<ax::Circle>(list[1]));
	EXPECT_TRUE(std::holds_alternative<ax::Line>(list[2]));
}

TEST_F(LuaShapeBindings, InvalidDescriptionsRaiseDescriptiveLuaErrors)
{
	EXPECT_NE(parse_error(R"({ radius = 1 })").find("'type' must be one of"), std::string::npos);
	EXPECT_NE(parse_error(R"({ type = "hexagon" })").find("unknown type \"hexagon\""), std::string::npos);
	EXPECT_NE(parse_error(R"({ type = "circle", x = 0, y = 0 })").find("'radius' is required"), std::string::npos);
	EXPECT_NE(parse_error(R"({ type = "circle", x = 0, radius = 1 })").find("'y' is required"), std::string::npos);
	EXPECT_NE(parse_error(R"({ type = "circle", x = 0, y = 0, radius = "big" })").find("'radius' must be a number"), std::string::npos);
	EXPECT_NE(parse_error(R"({ type = "circle", position = { 1 }, radius = 1 })").find("'position' must be a vec2"), std::string::npos);
	EXPECT_NE(parse_error(R"({ type = "circle", x = 0, y = 0, radius = 1, fill = { 1, 0 } })").find("'fill' must be a vec4"), std::string::npos);
	EXPECT_NE(parse_error(R"({ type = "circle", x = 0, y = 0, radius = 1, outline = 2 })").find("'outline' must be a table"), std::string::npos);
	EXPECT_NE(parse_error(R"({ type = "circle", x = 0, y = 0, radius = 1, screen_space = 1 })").find("'screen_space' must be a boolean"), std::string::npos);
	EXPECT_NE(parse_error(R"({ type = "circle", x = 0, y = 0, radius = 1, segments = -1 })").find("'segments' must not be negative"), std::string::npos);
	EXPECT_NE(parse_error(R"({ type = "triangle", points = { { 0, 0 }, { 1, 1 } } })").find("exactly 3 points"), std::string::npos);
	EXPECT_NE(parse_error(R"({ type = "polygon", points = { { 0, 0 }, "nope" } })").find("'points[2]'"), std::string::npos);
	EXPECT_NE(parse_error(R"({ type = "line", start = { 0, 0 }, finish = { 1, 1 }, cap = "pointy" })").find("'cap' must be one of"), std::string::npos);
	EXPECT_EQ(parse_error(R"({ type = "line", start = { 0, 0 }, finish = { 1, 1 }, cap = "round" })"), "");
}

TEST_F(LuaShapeBindings, ShapeListManagement)
{
	run(R"(
		local list = shapes.list()
		assert(#list == 0)
		list:reserve(10)
		assert(list:add({ type = "circle", x = 1, y = 2, radius = 3 }) == 1)
		assert(list:add({ type = "line", x1 = 0, y1 = 0, x2 = 4, y2 = 0 }) == 2)
		assert(list:add({ type = "rect", x = 0, y = 0, width = 1, height = 1 }) == 3)
		assert(#list == 3 and list:size() == 3)

		list:set_position(2, 10, 10)
		local x, y = list:get_position(2)
		assert(x == 10 and y == 10)

		list:set(1, { type = "circle", x = 5, y = 6, radius = 1 })
		x, y = list:get_position(1)
		assert(x == 5 and y == 6)

		list:remove(1)
		assert(#list == 2)
		x, y = list:get_position(1)
		assert(x == 10 and y == 10)

		assert(not pcall(list.set_z, list, 3, 1))
		assert(not pcall(list.get_position, list, 0))
		local ok, err = pcall(list.remove, list, 5)
		assert(not ok and tostring(err):find("out of range"))

		list:clear()
		assert(#list == 0)
	)");
}

TEST_F(LuaShapeBindings, SettersUpdateNativeShapes)
{
	run(R"(
		list = shapes.list()
		list:add({ type = "line", start = { 1, 1 }, finish = { 4, 5 } })
		list:add({ type = "polygon", points = { { 0, 0 }, { 1, 0 }, { 0, 1 } } })
		list:set_position(1, 11, 21)
		list:set_rotation(1, 0.25)
		list:set_z(2, 3)
		list:set_fill(2, 0.1, 0.2, 0.3)
		list:set_outline(2, 4, 1, 0, 0, 0.5)
	)");

	const auto& list{ m_state["list"].get<ax::lua::bindings::LuaShapeList&>() };
	ASSERT_EQ(list.shapes.size(), 2u);

	const auto& line{ std::get<ax::Line>(list.shapes[0]) };
	expect_vec(line.start, { 11, 21 });
	expect_vec(line.end, { 14, 25 });
	EXPECT_FLOAT_EQ(line.rotation, 0.25f);

	const auto& polygon{ std::get<ax::Polygon>(list.shapes[1]) };
	EXPECT_FLOAT_EQ(polygon.z, 3);
	expect_vec(polygon.fill, { 0.1f, 0.2f, 0.3f, 1 });
	expect_vec(polygon.outline.colour, { 1, 0, 0, 0.5f });
	EXPECT_FLOAT_EQ(polygon.outline.thickness, 4);
}
