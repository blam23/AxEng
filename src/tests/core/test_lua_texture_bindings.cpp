#include <gtest/gtest.h>

#include "axeng/core/lua/bindings/lua_texture_bindings.h"

namespace
{
	sol::table make_texture(sol::state& state, bool atlas)
	{
		auto texture{ state.create_table() };
		texture["width"] = 64.0f;
		texture["height"] = 48.0f;
		if (atlas)
			texture["atlas_region"] = state.create_table_with(1, 128.0f, 2, 96.0f, 3, 64.0f, 4, 48.0f);
		return texture;
	}

	void expect_region(ax::rectf region, ax::rectf expected)
	{
		EXPECT_FLOAT_EQ(region.x, expected.x);
		EXPECT_FLOAT_EQ(region.y, expected.y);
		EXPECT_FLOAT_EQ(region.z, expected.z);
		EXPECT_FLOAT_EQ(region.w, expected.w);
	}
}

TEST(LuaTextureBindings, WholeAtlasSubTextureUsesPixelRegion)
{
	sol::state state;
	const auto texture{ make_texture(state, true) };
	expect_region(ax::lua::bindings::get_texture_region(texture), { 128.0f, 96.0f, 64.0f, 48.0f });
}

TEST(LuaTextureBindings, AtlasCropOffsetsOriginWithoutScalingDimensions)
{
	sol::state state;
	const auto texture{ make_texture(state, true) };
	expect_region(ax::lua::bindings::get_texture_region(texture, { 8.0f, 12.0f, 16.0f, 24.0f }),
		{ 136.0f, 108.0f, 16.0f, 24.0f });
}

TEST(LuaTextureBindings, AtlasCropPreservesFractionalPixelsAndSignedDimensions)
{
	sol::state state;
	const auto texture{ make_texture(state, true) };
	expect_region(ax::lua::bindings::get_texture_region(texture, { 0.5f, 1.25f, -16.0f, 0.0f }),
		{ 128.5f, 97.25f, -16.0f, 0.0f });
}

TEST(LuaTextureBindings, StandaloneTextureUsesFullDimensions)
{
	sol::state state;
	const auto texture{ make_texture(state, false) };
	expect_region(ax::lua::bindings::get_texture_region(texture), { 0.0f, 0.0f, 64.0f, 48.0f });
}

TEST(LuaTextureBindings, StandaloneCropIsUnchanged)
{
	sol::state state;
	const auto texture{ make_texture(state, false) };
	expect_region(ax::lua::bindings::get_texture_region(texture, { 8.0f, 12.0f, 16.0f, 24.0f }),
		{ 8.0f, 12.0f, 16.0f, 24.0f });
}
