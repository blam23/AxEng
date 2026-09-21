#include <gtest/gtest.h>

#include "AxEngLib/lua_engine.h"
#include "AxEngLib/resource_loader.h"
#include "AxEngLib/script.h"
#include "AxEngLib/flag_set.hpp"

#include "log_capture.h"

using namespace ax;
using namespace ax::lua;

constexpr const char* s_access_error_message = "Access to library is denied (permission not requested)";

static void register_minimal_init()
{
	static const char init[] = "-- minimal init";
	ax::Resource::register_embedded_resource<ax::lua::Script>("@init", ax::EmbeddedResource{ (const uint8_t*)init, sizeof(init) - 1 });
}

TEST(LuaManagerTests, NoPermissions_AccessIOAndOS_LogsErrors)
{
	testlog::LogCapture::instance().expect_total_error_count(4);

	register_minimal_init();

	flag_set<Permission> perms; // no permissions
	Manager m(perms);
	ASSERT_EQ(ax::Error::Success, m.setup());

	m.state().do_string("local f = io.open; local g = os.exit;", "@test_no_perms");
	ASSERT_LOG_CONTAINS(s_access_error_message);

	testlog::LogCapture::instance().clear_messages();

	m.state().do_string("local f = io.open;", "@test_no_perms_io_only");
	ASSERT_LOG_CONTAINS(s_access_error_message);

	testlog::LogCapture::instance().clear_messages();
	m.state().do_string("local g = os.exit;", "@test_no_perms_os_only");
	ASSERT_LOG_CONTAINS(s_access_error_message);

	m.cleanup();
}

TEST(LuaManagerTests, IOAllowed_OSNot_OnlyOSLogs)
{
	testlog::LogCapture::instance().expect_total_error_count(1);

	register_minimal_init();

	flag_set<Permission> perms;
	perms |= Permission::IO;

	Manager m(perms);
	ASSERT_EQ(ax::Error::Success, m.setup());

	// Access io - should NOT log an error
	m.state().do_string("local f = io.open;", "@test_io_allowed");
	EXPECT_LOG_NOT_CONTAINS(s_access_error_message);

	// clear captured messages and then access os which should log
	testlog::LogCapture::instance().clear_messages();
	m.state().do_string("local g = os.exit;", "@test_os_denied");
	ASSERT_LOG_CONTAINS(s_access_error_message);

	m.cleanup();
}

TEST(LuaManagerTests, OSAllowed_IOAllowed_NoLogs)
{
	register_minimal_init();

	flag_set<Permission> perms;
	perms |= Permission::IO;
	perms |= Permission::OS;

	Manager m(perms);
	ASSERT_EQ(ax::Error::Success, m.setup());

	m.state().do_string("local f = io.open; local g = os.exit;", "@test_both_allowed");

	EXPECT_LOG_NOT_CONTAINS(s_access_error_message);

	m.cleanup();
}
