#include "gtest/gtest.h"
#include "axenglib/resource_loader.h"

#include <filesystem>
#include <fstream>

using namespace ax;
namespace fs = std::filesystem;

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
    const auto tmp = fs::temp_directory_path() / "axeng_test_dir_loadfile";
    fs::create_directories(tmp);

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
    fs::remove_all(tmp, ec);
}

TEST(DirectoryResourceLoaderTests, LoadMissingFile_ReturnsNotFound)
{
    const auto tmp = fs::temp_directory_path() / "axeng_test_dir_missing";
    fs::create_directories(tmp);

    DirectoryResourceLoader loader(tmp);
    const auto res = loader.load("no_such_file.bin");
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ResourceLoadError::NotFound);

    std::error_code ec;
    fs::remove_all(tmp, ec);
    EXPECT_EQ(0, ec.value()) << "Failed to cleanup test!";
}

TEST(DirectoryResourceLoaderTests, LoadDirectoryAsFile_ReturnsCantOpen)
{
    const auto tmp = fs::temp_directory_path() / "axeng_test_dir_isdir";
    fs::create_directories(tmp);

    // Create a subdirectory and attempt to load it as if it were a file.
    const auto subdir = tmp / "subdir";
    fs::create_directories(subdir);

    DirectoryResourceLoader loader(tmp);
    const auto res = loader.load("subdir");

    // Exists -> file.open should fail -> CantOpen
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ResourceLoadError::CantOpen);

    std::error_code ec;
    fs::remove_all(tmp, ec);
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
    const auto tmp = fs::temp_directory_path() / "axeng_test_dir_variant";
    fs::create_directories(tmp);

    const auto filename = "hello.txt";
    const auto filepath = tmp / filename;
    const std::string content = "line1\nline2\n";
    {
        std::ofstream out(filepath, std::ios::binary);
        out << content;
    }

    ResourceLoader loader = DirectoryResourceLoader{ tmp };

    const auto bytes = Resource::load(loader, filename);
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(std::string(bytes.value().begin(), bytes.value().end()), content);

    const auto text = Resource::load_as_text(loader, filename);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(text.value(), content);

    std::error_code ec;
    fs::remove_all(tmp, ec);
    EXPECT_EQ(0, ec.value()) << "Failed to cleanup test!";
}


TEST(EmbeddedResourceLoaderTest, LoadReturnsData)
{
    static uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    EmbeddedResourceLayout layout;
    // key is a string literal so the string_view remains valid
    layout.emplace("res.bin", EmbeddedResource{ data, sizeof(data) });

    EmbeddedResourceLoader loader(std::move(layout));
    auto ret = loader.load("res.bin");

    ASSERT_TRUE(ret.has_value());
    std::vector<uint8_t> expected(std::begin(data), std::end(data));
    EXPECT_EQ(ret.value(), expected);
}

TEST(EmbeddedResourceLoaderTest, LoadNotFound)
{
    EmbeddedResourceLayout layout;
    static uint8_t data[] = { 0xFF };
    layout.emplace("present.bin", EmbeddedResource{ data, sizeof(data) });

    EmbeddedResourceLoader loader(std::move(layout));
    auto ret = loader.load("missing.bin");

    EXPECT_FALSE(ret.has_value());
    EXPECT_EQ(ret.error(), ResourceLoadError::NotFound);
}

TEST(ResourceTest, LoadAsTextWithEmbedded)
{
    const std::string text = "embedded text";
    static uint8_t buf[] = { 'e','m','b','e','d','d','e','d',' ','t','e','x','t' };
    EmbeddedResourceLayout layout;
    layout.emplace("foo.txt", EmbeddedResource{ buf, sizeof(buf) });

    ResourceLoader loader = EmbeddedResourceLoader(std::move(layout));
    auto ret = Resource::load_as_text(loader, "foo.txt");

    ASSERT_TRUE(ret.has_value());
    EXPECT_EQ(ret.value(), text);
}

TEST(ResourceTest, LoadNotFoundDirectoryThroughResource)
{
    const fs::path tempDir = fs::temp_directory_path() / "AxEng_ResourceLoaderTests_NotFound";
    fs::create_directories(tempDir);
    ResourceLoader loader = DirectoryResourceLoader{ tempDir };

    auto ret = Resource::load(loader, "does_not_exist.bin");
    EXPECT_FALSE(ret.has_value());
    EXPECT_EQ(ret.error(), ResourceLoadError::NotFound);

    std::error_code ec;
    fs::remove_all(tempDir, ec);
    EXPECT_EQ(0, ec.value()) << "Failed to cleanup test!";
}

TEST(ZipResourceLoaderTest, SetupFailsWhenFileMissing)
{
    const fs::path missingZip = fs::temp_directory_path() / "AxEng_ZipTests_nonexistent.zip";
    // Ensure it does not exist
    std::error_code ec;
    fs::remove(missingZip, ec);

    ZipResourceLoader loader(missingZip);
    auto setupErr = loader.setup();

    ASSERT_TRUE(setupErr.has_value());
    EXPECT_EQ(setupErr.value(), ResourceLoadError::NotFound);
}

TEST(ZipResourceLoaderTest, SetupLoadAndCleanupSucceeds)
{
    const fs::path baseDir = fs::temp_directory_path() / "AxEng_ZipTests";
    const fs::path filePath = baseDir / "data.txt";
    const fs::path zipPath = baseDir / "archive.zip";

    std::error_code ec;
    fs::create_directories(baseDir, ec);
    ASSERT_FALSE(ec) << "Failed to create temp dir: " << ec.message();

    const std::string contents = "Hello from zip!";
    {
        std::ofstream ofs(filePath, std::ios::binary);
        ASSERT_TRUE(ofs.is_open());
        ofs.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    std::string cmd = "powershell -NoProfile -Command \"Compress-Archive -LiteralPath '"
        + filePath.string() + "' -DestinationPath '" + zipPath.string() + "' -Force\"";

    const int rc = std::system(cmd.c_str());
    ASSERT_EQ(rc, 0) << "Compress-Archive command failed with code: " << rc;
    ASSERT_TRUE(fs::exists(zipPath)) << "Zip file was not created";

    ZipResourceLoader loader(zipPath);
    auto setupErr = loader.setup();
    ASSERT_FALSE(setupErr.has_value()) << "setup() returned error";

    auto data = loader.load("data.txt");
    ASSERT_TRUE(data.has_value());
    std::string loaded(data->begin(), data->end());
    EXPECT_EQ(loaded, contents);

    auto cleanupErr = loader.cleanup();
    EXPECT_FALSE(cleanupErr.has_value()) << "cleanup() returned error";

    fs::remove(zipPath, ec);
    EXPECT_EQ(0, ec.value()) << "Failed to cleanup test!";

    fs::remove(filePath, ec);
    EXPECT_EQ(0, ec.value()) << "Failed to cleanup test!";

    fs::remove(baseDir, ec);
    EXPECT_EQ(0, ec.value()) << "Failed to cleanup test!";

}