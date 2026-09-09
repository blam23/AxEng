#include "gtest/gtest.h"
#include "axenglib/resource_loader.h"

#include <filesystem>
#include <fstream>

using namespace ax;

TEST(EmbeddedResourceLoaderTests, LoadExistingResource_ReturnsExpectedData)
{
    // Source buffer that stays alive for the loader's lifetime
    static uint8_t src[] = { 0x10, 0x20, 0x30, 0x40 };

    EmbeddedResourceLayout layout;
    layout.emplace("myres", EmbeddedResource{ src, sizeof(src) });

    EmbeddedResourceLoader loader(std::move(layout));

    const auto res = loader.load("myres");
    ASSERT_TRUE(res.has_value());

    const auto& vec = res.value();
    EXPECT_EQ(vec.size(), sizeof(src));
    for (size_t i = 0; i < sizeof(src); i++)
        EXPECT_EQ(vec[i], src[i]);
}

TEST(EmbeddedResourceLoaderTests, LoadMissingResource_ReturnsNotFoundError)
{
    EmbeddedResourceLayout layout; // empty
    EmbeddedResourceLoader loader(std::move(layout));

    const auto res = loader.load("does_not_exist");
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ResourceLoadError::NotFound);
}

TEST(EmbeddedResourceLoaderTests, LoadEmptyResource_ReturnsEmptyVector)
{
    static uint8_t dummy = 0xFF;
    EmbeddedResourceLayout layout;

    // Provide a valid pointer but zero size
    layout.emplace("empty", EmbeddedResource{ &dummy, 0 });

    EmbeddedResourceLoader loader(std::move(layout));

    const auto res = loader.load("empty");
    ASSERT_TRUE(res.has_value());
    const auto& vec = res.value();
    EXPECT_TRUE(vec.empty());
}

TEST(DirectoryResourceLoaderTests, LoadFile_ReturnsContents)
{
    const auto tmp = std::filesystem::temp_directory_path() / "axeng_test_dir_loadfile";
    std::filesystem::create_directories(tmp);

    const auto filename = "test.bin";
    const auto filepath = tmp / filename;

    std::vector<uint8_t> expected{ 1, 2, 3, 4, 5 };
    {
        std::ofstream out(filepath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(expected.data()), expected.size());
    }

    DirectoryResourceLoader loader(tmp);
    const auto res = loader.load(filename);
    ASSERT_TRUE(res.has_value());
    const auto& vec = res.value();
    EXPECT_EQ(vec.size(), expected.size());
    EXPECT_EQ(vec, expected);

    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

TEST(DirectoryResourceLoaderTests, LoadMissingFile_ReturnsNotFound)
{
    const auto tmp = std::filesystem::temp_directory_path() / "axeng_test_dir_missing";
    std::filesystem::create_directories(tmp);

    DirectoryResourceLoader loader(tmp);
    const auto res = loader.load("no_such_file.bin");
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ResourceLoadError::NotFound);

    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    EXPECT_EQ(0, ec.value()) << "Failed to cleanup test!";
}

TEST(DirectoryResourceLoaderTests, LoadDirectoryAsFile_ReturnsCantOpen)
{
    const auto tmp = std::filesystem::temp_directory_path() / "axeng_test_dir_isdir";
    std::filesystem::create_directories(tmp);

    // Create a subdirectory and attempt to load it as if it were a file.
    const auto subdir = tmp / "subdir";
    std::filesystem::create_directories(subdir);

    DirectoryResourceLoader loader(tmp);
    const auto res = loader.load("subdir");

    // Exists -> file.open should fail -> CantOpen
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ResourceLoadError::CantOpen);

    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    EXPECT_EQ(0, ec.value()) << "Failed to cleanup test!";
}

TEST(ResourceTests, LoadAndLoadAsText_WithEmbeddedLoader)
{
    static uint8_t data[] = { 'H','e','l','l','o',' ', 'W','o','r','l','d','\n' };
    EmbeddedResourceLayout layout;
    layout.emplace("greet.txt", EmbeddedResource{ data, sizeof(data) });

    ResourceLoader loader = EmbeddedResourceLoader(std::move(layout));

    const auto bytes = Resource::load(loader, "greet.txt");
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(bytes.value().size(), sizeof(data));

    const auto text = Resource::load_as_text(loader, "greet.txt");
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(text.value(), std::string(reinterpret_cast<char*>(data), sizeof(data)));
}

TEST(ResourceTests, LoadWithDirectoryLoader_ViaResourceVariant)
{
    const auto tmp = std::filesystem::temp_directory_path() / "axeng_test_dir_variant";
    std::filesystem::create_directories(tmp);

    const auto filename = "hello.txt";
    const auto filepath = tmp / filename;
    const std::string content = "line1\nline2\n";
    {
        std::ofstream out(filepath, std::ios::binary);
        out << content;
    }

    DirectoryResourceLoader dirLoader(tmp);
    ResourceLoader loader = dirLoader;

    const auto bytes = Resource::load(loader, filename);
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(std::string(bytes.value().begin(), bytes.value().end()), content);

    const auto text = Resource::load_as_text(loader, filename);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(text.value(), content);

    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    EXPECT_EQ(0, ec.value()) << "Failed to cleanup test!";
}