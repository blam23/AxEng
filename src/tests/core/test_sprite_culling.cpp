#include <gtest/gtest.h>

#include "axeng/core/sprite_culling.h"

#include <limits>

TEST(SpriteCulling, KeepsEdgesRotationsAndNegativeScalesButRejectsDistantQuads)
{
	const ax::SpriteCuller culler{ { 100, 100 }, { 0, 0 }, 1 };
	ax::SpriteData data{};
	data.pos = { 100, 100 };
	EXPECT_FALSE(culler.visible(data, 16, 16));
	data.pos = { 51, 0 };
	data.rotation = 0.7853982f;
	EXPECT_TRUE(culler.visible(data, 16, 16));
	data.pos = { 100, 0 };
	data.scale = { -8, 1 };
	EXPECT_TRUE(culler.visible(data, 16, 16));
	data.pos = { -50, -50 };
	EXPECT_TRUE(culler.visible(data, 16, 16));
}

TEST(SpriteCulling, UsesPackedRegionDimensionsAndZeroPairFallback)
{
	const ax::SpriteCuller culler{ { 100, 100 }, { 0, 0 }, 1 };
	ax::SpriteData data{};
	data.pos = { 100, 0 };
	data.useRegion = ax::sprite_flag_region;
	data.region = { 0, 0, 16, 16 };
	EXPECT_FALSE(culler.visible(data, 1000, 1000));
	data.region = { 0, 0, .1f, .1f };
	EXPECT_TRUE(culler.visible(data, 1000, 1000));
	data.region.w = 16;
	EXPECT_FALSE(culler.visible(data, 1000, 1000));
}

TEST(SpriteCulling, ScreenBoundsIgnoreCameraAndClipsUsePackedPixelRules)
{
	const ax::SpriteCuller culler{ { 100, 100 }, { 1000, 1000 }, 2 };
	ax::SpriteData data{};
	data.screenSpace = 1;
	data.pos = { 20, 20 };
	EXPECT_TRUE(culler.visible(data, 16, 16));
	data.pos = { 200, 200 };
	EXPECT_FALSE(culler.visible(data, 16, 16));
	data.pos = { 20, 20 };
	data.clipRect = { 100, 0, 120, 100 };
	EXPECT_FALSE(culler.visible(data, 16, 16));
	data.clipRect = { .1f, .1f, .4f, .4f };
	EXPECT_FALSE(culler.visible(data, 16, 16));
	data.clipRect = { 0, 0, -1, -1 };
	EXPECT_TRUE(culler.visible(data, 16, 16));
}

TEST(SpriteCulling, InvalidViewsAndNonfiniteBoundsStayConservative)
{
	ax::SpriteData data{};
	data.pos = { 1e8f, 1e8f };
	EXPECT_TRUE((ax::SpriteCuller{ { 100, 100 }, { 0, 0 }, 0 }.visible(data, 16, 16)));
	const ax::SpriteCuller culler{ { 100, 100 }, { 0, 0 }, 1 };
	data.rotation = std::numeric_limits<float>::quiet_NaN();
	EXPECT_TRUE(culler.visible(data, 16, 16));
	data.rotation = 0;
	data.scale.x = std::numeric_limits<float>::infinity();
	EXPECT_TRUE(culler.visible(data, 16, 16));
	data.scale = { 1, 1 };
	data.useRegion = ax::sprite_flag_region;
	data.region = { 0, 0, std::numeric_limits<float>::max(), 16 };
	EXPECT_TRUE(culler.visible(data, 16, 16));
	data.useRegion = 0;
	data.clipRect = { 0, 0, std::numeric_limits<float>::max(), 100 };
	EXPECT_TRUE(culler.visible(data, 16, 16));
}

TEST(SpriteCulling, ContainsShaderTransformedSamplesAcrossCameraZoomAndRotation)
{
	for (const float zoom : { .25f, 1.0f, 4.0f })
	for (const bool screen : { false, true })
	for (const float rotation : { 0.0f, .7853982f, 1.5707964f, 4.712389f, -2.2f })
	for (const glm::vec2 scale : { glm::vec2{ 1, 1 }, glm::vec2{ -3, .5f }, glm::vec2{ .25f, -4 } })
	for (const float x : { -200.0f, -50.0f, 0.0f, 50.0f, 200.0f })
	for (const float y : { -100.0f, 0.0f, 100.0f })
	{
		const glm::vec2 camera{ 10, -20 };
		const ax::SpriteCuller culler{ { 100, 100 }, camera, zoom };
		ax::SpriteData data{};
		data.pos = { x, y };
		data.scale = scale;
		data.rotation = rotation;
		data.screenSpace = screen ? 1u : 0u;
		const auto size{ glm::vec2{ 32, 16 } * scale };
		bool touches{};
		for (const float u : { 0.0f, .5f, 1.0f })
		for (const float v : { 0.0f, .5f, 1.0f })
		{
			const auto offset{ (glm::vec2{ u, v } - .5f) * size };
			const glm::vec2 rotated{ offset.x * std::cos(rotation) - offset.y * std::sin(rotation),
				offset.x * std::sin(rotation) + offset.y * std::cos(rotation) };
			const auto world{ data.pos + size * .5f + rotated };
			const auto pixel{ screen ? world : (world - camera) * zoom + glm::vec2{ 50, 50 } };
			touches = touches || (pixel.x >= 0 && pixel.x <= 100 && pixel.y >= 0 && pixel.y <= 100);
		}
		if (touches) EXPECT_TRUE(culler.visible(data, 32, 16));
	}
}
