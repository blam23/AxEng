#include "axeng/core/lua/bindings/lua_vector_bindings.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

void ax::lua::bindings::setup_vector_bindings(sol::state& state)
{
	auto vec_mult_overloads = 
		sol::overload
		(
			[](const glm::vec2& a, const glm::vec2& b) -> glm::vec2 { return a * b; },
			[](const glm::vec2& a, float b) -> glm::vec2 { return a * b; },
			[](float a, const glm::vec2& b) -> glm::vec2 { return a * b; }
		);

	auto vec_add_overloads = 
		sol::overload
		(
			[](const glm::vec2& a, const glm::vec2& b) -> glm::vec2 { return a + b; },
			[](const glm::vec2& a, float b) -> glm::vec2 { return a + b; },
			[](float a, const glm::vec2& b) -> glm::vec2 { return a + b; }
		);

	auto vec_sub_overloads =
		sol::overload
		(
			[](const glm::vec2& a, const glm::vec2& b) -> glm::vec2 { return a - b; },
			[](const glm::vec2& a, float b) -> glm::vec2 { return a - b; },
			[](float a, const glm::vec2& b) -> glm::vec2 { return a - b; }
		);

	auto vec_div_overloads = 
		sol::overload
		(
			[](const glm::vec2& a, const glm::vec2& b) -> glm::vec2 { return a / b; },
			[](const glm::vec2& a, float b) -> glm::vec2 { return a / b; },
			[](float a, const glm::vec2& b) -> glm::vec2 { return a / b; }
		);

	state.new_usertype<glm::vec2>
	(
		"vec2",

		"new", sol::constructors<void(float, float)>(),
		"x", & glm::vec2::x,
		"y", & glm::vec2::y,
		"normalize", [](const glm::vec2& in) { return glm::normalize(in); },
		"length", [](const glm::vec2& in) { return glm::length(in); },
		sol::meta_function::multiplication, vec_mult_overloads,
		sol::meta_function::addition, vec_add_overloads,
		sol::meta_function::subtraction, vec_sub_overloads,
		sol::meta_function::division, vec_div_overloads,
		sol::meta_function::to_string, [](const glm::vec2& in) { return glm::to_string(in); }
	);

	auto vec4_mult_overloads = 
		sol::overload
		(
			[](const glm::vec4& a, const glm::vec4& b) -> glm::vec4 { return a * b; },
			[](const glm::vec4& a, float b) -> glm::vec4 { return a * b; },
			[](float a, const glm::vec4& b) -> glm::vec4 { return a * b; }
		);

	auto vec4_add_overloads = 
		sol::overload
		(
			[](const glm::vec4& a, const glm::vec4& b) -> glm::vec4 { return a + b; },
			[](const glm::vec4& a, float b) -> glm::vec4 { return a + b; },
			[](float a, const glm::vec4& b) -> glm::vec4 { return a + b; }
		);

	auto vec4_sub_overloads = 
		sol::overload
		(
			[](const glm::vec4& a, const glm::vec4& b) -> glm::vec4 { return a - b; },
			[](const glm::vec4& a, float b) -> glm::vec4 { return a - b; },
			[](float a, const glm::vec4& b) -> glm::vec4 { return a - b; }
		);

	auto vec4_div_overloads = 
		sol::overload
		(
			[](const glm::vec4& a, const glm::vec4& b) -> glm::vec4 { return a / b; },
			[](const glm::vec4& a, float b) -> glm::vec4 { return a / b; },
			[](float a, const glm::vec4& b) -> glm::vec4 { return a / b; }
		);

	state.new_usertype<glm::vec4>
	(
		"vec4",

		"new", sol::constructors<void(float, float, float, float)>(),
		"x", & glm::vec4::x,
		"y", & glm::vec4::y,
		"z", & glm::vec4::z,
		"w", & glm::vec4::w,
		"normalize", [](const glm::vec4& in) { return glm::normalize(in); },
		sol::meta_function::multiplication, vec4_mult_overloads,
		sol::meta_function::addition, vec4_add_overloads,
		sol::meta_function::subtraction, vec4_sub_overloads,
		sol::meta_function::division, vec4_div_overloads,
		sol::meta_function::to_string, [](const glm::vec4& in) { return glm::to_string(in); }
	);

}
