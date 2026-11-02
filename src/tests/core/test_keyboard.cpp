#include <gtest/gtest.h>

#include "axeng/core/input/keyboard.h"

TEST(Keyboard, InvalidKeyQueriesReturnFalse)
{
	EXPECT_FALSE(ax::input::KeyEventHandler::is_key_pressed(GLFW_KEY_UNKNOWN));
	EXPECT_FALSE(ax::input::KeyEventHandler::is_key_pressed(GLFW_KEY_LAST + 1));
}

TEST(Keyboard, ExternalMediaKeysReachGlobalSubscribers)
{
	bool received{};
	const auto subscription{ ax::input::KeyEventHandler::globalEventHandler.subscribe(
		[&received](const ax::input::AnyKeyEvent& event)
		{
			received = event.key == ax::input::media_key_next && event.pressed;
		}) };

	ax::input::KeyEventHandler::dispatch_external_key(ax::input::media_key_next, true);
	ax::input::KeyEventHandler::globalEventHandler.unsubscribe(subscription);

	EXPECT_TRUE(received);
}
