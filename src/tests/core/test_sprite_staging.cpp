#include <gtest/gtest.h>

#include "axeng/core/application.h"
#include "axeng/core/lua/bindings/lua_sprite_bindings.h"
#include "axeng/core/retained_sprites.h"

#include <array>
#include <future>
#include <limits>

TEST(SpriteStagingTests, RequiresExplicitRequestIndependentOfVisibility)
{
	auto batch{ std::make_shared<ax::StaticSpriteBatch>() };
	const std::array batches{ batch };
	EXPECT_FALSE(batch->ready());
	EXPECT_FALSE(batch->visible());
	EXPECT_FALSE(batch->staging_eligible());
	EXPECT_EQ(ax::next_static_sprite_batch(batches), nullptr);

	batch->set_visible(true);
	EXPECT_EQ(ax::next_static_sprite_batch(batches), nullptr);
	batch->set_visible(false);
	batch->set_staging(true, 10);
	EXPECT_FALSE(batch->visible());
	EXPECT_FALSE(batch->ready());
	EXPECT_EQ(ax::next_static_sprite_batch(batches), batch);

	batch->set_staging(false, 10);
	EXPECT_EQ(ax::next_static_sprite_batch(batches), nullptr);
}

TEST(SpriteStagingTests, PrioritizesRequestsWithoutReorderingDrawBatches)
{
	auto hidden{ std::make_shared<ax::StaticSpriteBatch>() };
	auto distant_batch{ std::make_shared<ax::StaticSpriteBatch>() };
	auto nearby_batch{ std::make_shared<ax::StaticSpriteBatch>() };
	const std::array batches{ hidden, distant_batch, nearby_batch };
	distant_batch->set_staging(true, 20);
	nearby_batch->set_staging(true, 5);
	EXPECT_EQ(ax::next_static_sprite_batch(batches), nearby_batch);
	EXPECT_EQ(batches.front(), hidden);
	EXPECT_EQ(batches.back(), nearby_batch);

	distant_batch->set_staging(true, 5);
	EXPECT_EQ(ax::next_static_sprite_batch(batches), distant_batch);
	nearby_batch->set_staging(true, std::numeric_limits<std::int64_t>::min());
	distant_batch->set_staging(true, std::numeric_limits<std::int64_t>::max());
	EXPECT_EQ(ax::next_static_sprite_batch(batches), nearby_batch);
	nearby_batch->set_staging(false, 0);
	EXPECT_EQ(ax::next_static_sprite_batch(batches), distant_batch);
	distant_batch->release();
	EXPECT_EQ(ax::next_static_sprite_batch(batches), nullptr);
}

TEST(SpriteStagingTests, EnforcesOwnershipAndRelease)
{
	auto batch{ std::make_shared<ax::StaticSpriteBatch>() };
	auto ownership{ std::async(std::launch::async, [batch]()
	{
		EXPECT_THROW(batch->set_staging(true, 0), sol::error);
	}) };
	ownership.get();
	EXPECT_FALSE(batch->staging_eligible());
	batch->set_staging(true, -1);
	EXPECT_EQ(batch->staging_priority(), -1);
	batch->release();
	batch->release();
	EXPECT_FALSE(batch->staging_eligible());
	EXPECT_FALSE(batch->ready());
	EXPECT_THROW(batch->set_staging(true, 0), sol::error);
	EXPECT_THROW(batch->set_visible(true), sol::error);
}

TEST(SpriteStagingTests, ExposesPolicyThroughLuaBindings)
{
	auto app{ ax::Application::from_directory({}, ".") };
	sol::state state;
	state.open_libraries();
	state["app"] = state.create_table();
	ax::lua::bindings::setup_sprite_bindings(app, state);
	auto batch{ std::make_shared<ax::StaticSpriteBatch>() };
	state["batch"] = batch;
	const auto result{ state.safe_script(R"(
		assert(not batch:ready() and not batch:visible() and not batch:staging_eligible())
		batch:set_staging(true, -10)
		assert(batch:staging_eligible() and batch:staging_priority() == -10)
		assert(not batch:ready() and not batch:visible())
		batch:set_staging(false, 20)
		assert(not batch:staging_eligible() and batch:staging_priority() == 20)
		batch:release()
		batch:release()
		assert(not batch:staging_eligible())
		assert(not pcall(function() batch:set_staging(true, 0) end))
	)", sol::script_pass_on_error) };
	ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
}
