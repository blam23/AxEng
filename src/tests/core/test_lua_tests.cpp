#include "gtest/gtest.h"

#include "axeng/comp/compiler.h"
#include "axeng/core/axeng.h"

#include "tests/main/log_capture.h"

TEST(LuaTests, RunLuaTestSuite)
{
    // This kinda sucks but this value needs to be updated as we add and remove tests
    testlog::LogCapture::instance().expect_total_error_count(3);

    // Clean & compile lua tests
    ASSERT_EQ(ax::Error::Success, ax::comp::clean("compiled_lua_tests"));
    ASSERT_EQ
    (
        ax::Error::Success,
        ax::comp::compile("lua_tests", "compiled_lua_tests", false)
    );

    // Run ze tests
    ASSERT_EQ
    (
        ax::Error::Success,
        ax::run_from_directory({}, {}, "compiled_lua_tests")
    );
}