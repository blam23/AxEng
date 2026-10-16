#include "axeng/core/lua/bindings/lua_vector_bindings.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

namespace
{
	template <typename Vector, typename Operation>
	Vector apply_vector_operation(const sol::object& left, const sol::object& right, Operation operation)
	{
		if (left.is<Vector>())
		{
			const auto lhs{ left.as<Vector>() };
			if (right.is<Vector>())
				return operation(lhs, right.as<Vector>());
			if (right.get_type() == sol::type::number)
				return operation(lhs, Vector{ right.as<float>() });
		}
		else if (left.get_type() == sol::type::number && right.is<Vector>())
		{
			return operation(Vector{ left.as<float>() }, right.as<Vector>());
		}

		throw sol::error("Vector arithmetic requires a vector or number on each side.");
	}
}

void ax::lua::bindings::setup_vector_bindings(sol::state& state)
{
	state.new_usertype<glm::vec2>
	(
		"vec2",

		"new", sol::constructors<void(float, float)>(),
		"x", & glm::vec2::x,
		"y", & glm::vec2::y,
		"normalize", [](const glm::vec2& in) { return glm::normalize(in); },
		"length", [](const glm::vec2& in) { return glm::length(in); },
		"dot", [](const glm::vec2& a, const glm::vec2& b) { return glm::dot(a, b); },
		"copy", [](const glm::vec2& in) { return glm::vec2(in); },
		sol::meta_function::multiplication,
		[](const sol::object& left, const sol::object& right)
		{
			return apply_vector_operation<glm::vec2>(left, right, [](const auto& a, const auto& b) { return a * b; });
		},
		sol::meta_function::addition,
		[](const sol::object& left, const sol::object& right)
		{
			return apply_vector_operation<glm::vec2>(left, right, [](const auto& a, const auto& b) { return a + b; });
		},
		sol::meta_function::subtraction,
		[](const sol::object& left, const sol::object& right)
		{
			return apply_vector_operation<glm::vec2>(left, right, [](const auto& a, const auto& b) { return a - b; });
		},
		sol::meta_function::division,
		[](const sol::object& left, const sol::object& right)
		{
			return apply_vector_operation<glm::vec2>(left, right, [](const auto& a, const auto& b) { return a / b; });
		},
		sol::meta_function::to_string, [](const glm::vec2& in) { return glm::to_string(in); }
	);

	state.new_usertype<glm::vec4>
	(
		"vec4",

		"new", sol::constructors<void(float, float, float, float)>(),
		"x", & glm::vec4::x,
		"y", & glm::vec4::y,
		"z", & glm::vec4::z,
		"w", & glm::vec4::w,
		"r", & glm::vec4::r,
		"g", & glm::vec4::g,
		"b", & glm::vec4::b,
		"a", & glm::vec4::a,
		"normalize", [](const glm::vec4& in) { return glm::normalize(in); },
		"copy", [](const glm::vec4& in) { return glm::vec4(in); },
		sol::meta_function::multiplication,
		[](const sol::object& left, const sol::object& right)
		{
			return apply_vector_operation<glm::vec4>(left, right, [](const auto& a, const auto& b) { return a * b; });
		},
		sol::meta_function::addition,
		[](const sol::object& left, const sol::object& right)
		{
			return apply_vector_operation<glm::vec4>(left, right, [](const auto& a, const auto& b) { return a + b; });
		},
		sol::meta_function::subtraction,
		[](const sol::object& left, const sol::object& right)
		{
			return apply_vector_operation<glm::vec4>(left, right, [](const auto& a, const auto& b) { return a - b; });
		},
		sol::meta_function::division,
		[](const sol::object& left, const sol::object& right)
		{
			return apply_vector_operation<glm::vec4>(left, right, [](const auto& a, const auto& b) { return a / b; });
		},
		sol::meta_function::to_string, [](const glm::vec4& in) { return glm::to_string(in); }
	);

}
