#include <gtest/gtest.h>

#include "axeng/core/application.h"
#include "axeng/core/build_info.h"
#include "axeng/core/lua/bindings/lua_application_bindings.h"

#include <string>

TEST(LuaApplicationBindings, CurrentPlatformNamesTheBuildPlatform)
{
	auto app{ ax::Application::from_embedded({}, ax::EmbeddedResourceLayout{}) };
	sol::state state;
	state.open_libraries(sol::lib::base);
	ax::lua::bindings::setup_application_bindings(app, state, false);
	const auto platform{ state.do_string("return app.current_platform()") };
	ASSERT_TRUE(platform.valid());
	EXPECT_EQ(platform.get<std::string>(), std::string{ ax::build::platform });
#if defined(_WIN32)
	EXPECT_EQ(platform.get<std::string>(), "windows");
#endif
}
