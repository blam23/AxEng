#include "gtest/gtest.h"
#include "axeng/core/application.h"
#include "axeng/core/user_files.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include <vector>
#include <windows.h>
#include <shlobj.h>

static_assert(!std::is_move_constructible_v<ax::UserFileHandle>);
static_assert(!std::is_copy_constructible_v<ax::UserFileHandle>);

class UserFileManagerTests : public testing::Test
{
protected:
    void SetUp() override
    {
        const auto name = "axeng_test_user_files_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const std::string manifest = "manifest = { name = '" + name +
            "', headless = true, permissions = {}, scripts = { main = 'main.lua' }, entry_point = 'main' }";
        static constexpr char main_script[] = "return 0";
        ax::EmbeddedResourceLayout layout;
        layout.emplace("manifest.luac", ax::EmbeddedResource{
            reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size() });
        layout.emplace("main.lua", ax::EmbeddedResource{
            reinterpret_cast<const uint8_t*>(main_script), sizeof(main_script) - 1 });
        auto app = ax::Application::from_embedded({}, std::move(layout));
        ASSERT_TRUE(app.try_load({}));
        ASSERT_EQ(manager.set_application(app), ax::Error::Success);
        char app_data[MAX_PATH];
        ASSERT_TRUE(SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, app_data)));
        directory = std::filesystem::path(app_data) / "AxEng" / "app_user" / app.safe_directory_name();
        filepath = directory / filename;
        std::ofstream file(filepath, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        file << "user file contents";
        ASSERT_TRUE(file.good());
    }

    void TearDown() override
    {
        std::error_code error;
        if (!filepath.empty())
            std::filesystem::remove(filepath, error);
        EXPECT_FALSE(error) << error.message();
        if (!directory.empty())
            std::filesystem::remove(directory, error);
        EXPECT_FALSE(error) << error.message();
    }

    std::string filename = "contents.txt";
    std::filesystem::path filepath;
    std::filesystem::path directory;
    ax::UserFileManager manager;
};

TEST_F(UserFileManagerTests, OpeningMoreFilesPreservesExistingHandles)
{
    std::vector<ax::UserFileHandle*> handles;
    for (int i = 0; i < 64; ++i)
    {
        auto* handle = manager.open_file(filename, "r");
        ASSERT_NE(handle, nullptr);
        ASSERT_TRUE(handle->file.is_open());
        handles.push_back(handle);
    }

    for (auto* handle : handles)
    {
        EXPECT_EQ(handle->manager, &manager);
        EXPECT_EQ(handle->read_all(), "user file contents");
        EXPECT_EQ(manager.close_file(handle), ax::Error::Success);
    }
}

TEST_F(UserFileManagerTests, ClosingMiddleFilePreservesOtherHandles)
{
    auto* first = manager.open_file(filename, "r");
    auto* middle = manager.open_file(filename, "r");
    auto* last = manager.open_file(filename, "r");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(middle, nullptr);
    ASSERT_NE(last, nullptr);

    EXPECT_EQ(manager.close_file(middle), ax::Error::Success);
    EXPECT_EQ(first->read_all(), "user file contents");
    EXPECT_EQ(last->read_all(), "user file contents");
    first->close();
    last->close();
}

TEST_F(UserFileManagerTests, ClosingNullHandleSucceeds)
{
    EXPECT_EQ(manager.close_file(nullptr), ax::Error::Success);
}
