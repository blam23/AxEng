#pragma once

#include "axeng/core/lua/lua_bindings.h"
#include "axeng/core/shapes/shape.h"

namespace ax::lua::bindings
{
	// Retained list of shapes exposed to Lua as the "shape_list" usertype. Wrapped rather than
	// exposing ShapeList directly so sol doesn't treat it as a generic container.
	struct LuaShapeList
	{
		ax::ShapeList shapes;
	};

	// Converts a Lua shape description, e.g. { type = "circle", position = { 10, 20 }, radius = 5 },
	// into a shape. Throws sol::error if the description is invalid.
	ax::Shape shape_from_table(const sol::table& description);

	// Accepts either a single shape description or an array of them.
	void shapes_from_table(const sol::table& descriptions, ax::ShapeList& out);

	// Registers the shape_list usertype and returns the table normally exposed as app.shapes.
	sol::table create_shape_table(sol::state&);

	void setup_shape_bindings(ax::Application&, sol::state&);
}
