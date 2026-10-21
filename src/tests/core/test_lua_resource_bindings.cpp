#include <gtest/gtest.h>

#include "axeng/core/application.h"
#include "axeng/core/lua/bindings/lua_user_io_bindings.h"
#include "tests/main/log_capture.h"

#include <minizip-ng/mz.h>
#include <minizip-ng/mz_strm.h>
#include <minizip-ng/mz_zip.h>
#include <minizip-ng/mz_zip_rw.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>
#include <shlobj.h>

namespace
{
	constexpr unsigned char s_test_png_bytes[]{
		#embed "../../../lua_tests/assets/frilly.png"
	};

	std::string lua_string_literal(const std::string& value)
	{
		std::ostringstream result;
		result << '"';
		for (const unsigned char byte : value)
			result << '\\' << std::setw(3) << std::setfill('0') << static_cast<unsigned int>(byte);
		result << '"';
		return result.str();
	}

	std::string make_app_name(const std::string& prefix)
	{
		return prefix + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	}

	std::filesystem::path get_user_directory(const std::string& app_name)
	{
		char app_data[MAX_PATH];
		if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, app_data)))
			return {};
		return std::filesystem::path{ app_data } / "AxEng" / "app_user" / app_name;
	}

	class ScopedTestFiles
	{
	public:
		ScopedTestFiles(std::vector<std::filesystem::path> files, std::filesystem::path user_directory)
			: m_files{ std::move(files) }
			, m_user_directory{ std::move(user_directory) }
		{
		}

		~ScopedTestFiles()
		{
			std::error_code error;
			for (const auto& file : m_files)
			{
				std::filesystem::remove(file, error);
				error.clear();
			}
			std::filesystem::remove(m_user_directory, error);
		}

	private:
		std::vector<std::filesystem::path> m_files;
		std::filesystem::path m_user_directory;
	};
}

TEST(LuaResourceBindings, SaveTextureMethodsWritePngFiles)
{
	const auto app_name{ make_app_name("axeng_test_save_texture_") };
	const auto io_file{ std::filesystem::temp_directory_path() / (app_name + "_io.png") };
	const auto user_directory{ get_user_directory(app_name) };
	ASSERT_FALSE(user_directory.empty());
	const auto user_file{ user_directory / "saved.png" };
	ScopedTestFiles cleanup{ { io_file, user_file }, user_directory };

	const std::string png{ reinterpret_cast<const char*>(s_test_png_bytes), sizeof(s_test_png_bytes) };
	const auto entry_point{
		"assert(type(ax) == 'table')\n"
		"local texture = app.res.create_texture('test_texture', " + lua_string_literal(png) + ")\n"
		"assert(texture.valid and texture.width > 0 and texture.height > 0)\n"
		"local found_texture = app.res.get_texture('test_texture')\n"
		"assert(found_texture.valid and found_texture.width == texture.width and found_texture.height == texture.height)\n"
		"assert(not app.res.get_texture('missing_texture').valid)\n"
		"local texture_regions = app.res.get_texture_regions('test_texture')\n"
		"assert(texture_regions.valid and next(texture_regions.regions) == nil)\n"
		"assert(not app.res.get_texture_regions('missing_texture').valid)\n"
		"assert(app.res.get_script('main').valid)\n"
		"assert(not app.res.get_script('missing_script').valid)\n"
		"assert(type(app.res.save_texture_io) == 'function')\n"
		"assert(type(app.res.save_texture_user) == 'function')\n"
		"assert(type(app.res.create_mega_texture) == 'function')\n"
		"assert(app.res.save_texture_io('test_texture', " + lua_string_literal(io_file.generic_string()) + "))\n"
		"assert(app.res.save_texture_user('test_texture', 'saved.png'))\n"
		"assert(not app.res.save_texture_io('missing_texture', " + lua_string_literal(io_file.generic_string()) + "))\n"
		"assert(not app.res.save_texture_user('missing_texture', 'missing.png'))\n"
		"local builtin_font = app.res.create_texture('__builtin_font', " + lua_string_literal(png) + ")\n"
		"assert(builtin_font.valid and builtin_font.width > 0 and builtin_font.height > 0)\n"
		"assert(app.res.create_mega_texture('test_atlas', { 'test.png', '__builtin_font' }, 0) == 0)\n"
		"local atlas_regions = app.res.get_texture_regions('test_atlas')\n"
		"assert(atlas_regions.valid and atlas_regions.regions['test.png'] ~= nil)\n"
		"assert(atlas_regions.regions['__builtin_font'] ~= nil)\n"
		"assert(atlas_regions.regions['test.png'][3] > 0 and atlas_regions.regions['test.png'][4] > 0)\n"
		"assert(atlas_regions.regions['__builtin_font'][3] == builtin_font.width)\n"
		"assert(atlas_regions.regions['__builtin_font'][4] == builtin_font.height)\n"
		"assert(app.res.get_texture('test_atlas').valid)\n"
		"return 0"
	};
	const std::string manifest{
		"manifest = { name = '" + app_name +
		"', headless = true, permissions = { 'io' }, scripts = { main = 'main.lua' }, entry_point = 'main' }"
	};

	ax::EmbeddedResourceLayout layout;
	layout.emplace("manifest.luac", ax::EmbeddedResource{
		reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size() });
	layout.emplace("main.lua", ax::EmbeddedResource{
		reinterpret_cast<const uint8_t*>(entry_point.data()), entry_point.size() });
	layout.emplace("test.png", ax::EmbeddedResource{ s_test_png_bytes, sizeof(s_test_png_bytes) });

	flag_set<ax::lua::Permission> permissions;
	permissions |= ax::lua::Permission::IO;
	auto app{ ax::Application::from_embedded(permissions, std::move(layout)) };
	ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));

	const auto expect_png{ [](const std::filesystem::path& path)
	{
		std::ifstream file{ path, std::ios::binary };
		ASSERT_TRUE(file.is_open());
		unsigned char signature[8]{};
		ASSERT_TRUE(file.read(reinterpret_cast<char*>(signature), sizeof(signature)));
		const unsigned char expected_signature[]{ 137, 80, 78, 71, 13, 10, 26, 10 };
		EXPECT_TRUE(std::equal(signature, signature + sizeof(signature), expected_signature));
		EXPECT_GT(std::filesystem::file_size(path), sizeof(signature));
	} };
	expect_png(io_file);
	expect_png(user_file);
}

TEST(LuaResourceBindings, UserFilesCanBeWrittenToZip)
{
	const auto app_name{ make_app_name("axeng_test_user_zip_") };
	const auto user_directory{ get_user_directory(app_name) };
	ASSERT_FALSE(user_directory.empty());
	const auto archive_path{ user_directory / "factory_save.zip" };
	ScopedTestFiles cleanup{ { archive_path, user_directory / "replacement.txt" }, user_directory };
	const std::string manifest{
		"manifest = { name = '" + app_name +
		"', headless = true, permissions = { 'io' }, scripts = { main = 'main.lua' }, entry_point = 'main' }"
	};
	static constexpr char entry_point[] = "return 0";

	ax::EmbeddedResourceLayout layout;
	layout.emplace("manifest.lua", ax::EmbeddedResource{
		reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size() });
	layout.emplace("main.lua", ax::EmbeddedResource{
		reinterpret_cast<const uint8_t*>(entry_point), sizeof(entry_point) - 1 });

	flag_set<ax::lua::Permission> permissions;
	permissions |= ax::lua::Permission::IO;
	auto app{ ax::Application::from_embedded(permissions, std::move(layout)) };
	ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));

	auto* save_file{ app.user_files().open_file("factory_save.lua", "w") };
	ASSERT_NE(save_file, nullptr);
	ASSERT_TRUE(save_file->write("return { seed = 123 }"));
	save_file->close();
	auto* screenshot{ app.user_files().open_file("factory_save.png", "w") };
	ASSERT_NE(screenshot, nullptr);
	ASSERT_TRUE(screenshot->write("png image bytes"));
	screenshot->close();

	const auto zip_result{ app.user_files().create_zip(
		{ "factory_save.lua", "factory_save.png" },
		"factory_save.zip",
		true) };
	ASSERT_TRUE(zip_result.has_value()) << zip_result.error();
	EXPECT_FALSE(std::filesystem::exists(user_directory / "factory_save.lua"));
	EXPECT_FALSE(std::filesystem::exists(user_directory / "factory_save.png"));
	EXPECT_EQ(app.user_files().read_zip_entry("factory_save.zip", "factory_save.lua"),
		std::optional<std::string>{ "return { seed = 123 }" });
	EXPECT_EQ(app.user_files().read_zip_entry("factory_save.zip", "factory_save.png"),
		std::optional<std::string>{ "png image bytes" });

	sol::state lua;
	lua.open_libraries();
	lua["app"] = lua.create_table();
	ax::lua::bindings::setup_user_io_bindings(app, lua, true);
	const auto read_result{ lua.safe_script(
		"assert(app.user_io.read_zip_entry('factory_save.zip', 'factory_save.lua') == 'return { seed = 123 }')",
		sol::script_pass_on_error) };
	ASSERT_TRUE(read_result.valid());

	void* reader{ mz_zip_reader_create() };
	ASSERT_NE(reader, nullptr);
	ASSERT_EQ(mz_zip_reader_open_file(reader, archive_path.string().c_str()), MZ_OK);
	ASSERT_EQ(mz_zip_reader_goto_first_entry(reader), MZ_OK);

	std::vector<std::string> entry_names;
	for (;;)
	{
		mz_zip_file* entry{ nullptr };
		ASSERT_EQ(mz_zip_reader_entry_get_info(reader, &entry), MZ_OK);
		ASSERT_NE(entry, nullptr);
		entry_names.emplace_back(entry->filename);
		const auto result{ mz_zip_reader_goto_next_entry(reader) };
		if (result == MZ_END_OF_LIST)
			break;
		ASSERT_EQ(result, MZ_OK);
	}
	EXPECT_EQ(entry_names, (std::vector<std::string>{ "factory_save.lua", "factory_save.png" }));
	EXPECT_EQ(mz_zip_reader_close(reader), MZ_OK);
	mz_zip_reader_delete(&reader);

	auto* replacement{ app.user_files().open_file("replacement.txt", "w") };
	ASSERT_NE(replacement, nullptr);
	ASSERT_TRUE(replacement->write("replacement save"));
	replacement->close();
	const auto replacement_result{ app.user_files().create_zip({ "replacement.txt" }, "factory_save.zip", true) };
	ASSERT_TRUE(replacement_result.has_value()) << replacement_result.error();
	EXPECT_EQ(app.user_files().read_zip_entry("factory_save.zip", "replacement.txt"),
		std::optional<std::string>{ "replacement save" });
	EXPECT_FALSE(std::filesystem::exists(user_directory / "replacement.txt"));
}

TEST(LuaResourceBindings, SourceManifestOverridesIncompatibleBytecode)
{
	const std::string manifest{
		"manifest = { name = 'source_manifest_test', headless = true, permissions = {}, scripts = { main = 'main.lua' }, entry_point = 'main' }"
	};
	const std::string incompatible_bytecode{ "\x1bLua" };
	const std::string entry_point{ "assert(type(args) == 'table' and args[1] == 'bootstrap-test'); return 0" };

	ax::EmbeddedResourceLayout layout;
	layout.emplace("manifest.lua", ax::EmbeddedResource{
		reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size() });
	layout.emplace("manifest.luac", ax::EmbeddedResource{
		reinterpret_cast<const uint8_t*>(incompatible_bytecode.data()), incompatible_bytecode.size() });
	layout.emplace("main.lua", ax::EmbeddedResource{
		reinterpret_cast<const uint8_t*>(entry_point.data()), entry_point.size() });

	auto app{ ax::Application::from_embedded({}, std::move(layout)) };
	EXPECT_TRUE(app.try_load({ "bootstrap-test" }, wgpu::BackendType::Undefined));
}

TEST(LuaResourceBindings, IOBindingsAreUnavailableWithoutPermission)
{
	testlog::LogCapture::instance().expect_total_error_count(1);

	const auto app_name{ make_app_name("axeng_test_no_io_") };
	const auto user_directory{ get_user_directory(app_name) };
	ASSERT_FALSE(user_directory.empty());
	const auto user_file{ user_directory / "saved.png" };
	ScopedTestFiles cleanup{ { user_file }, user_directory };

	const std::string png{ reinterpret_cast<const char*>(s_test_png_bytes), sizeof(s_test_png_bytes) };
	const auto entry_point{
		"assert(type(ax) == 'table')\n"
		"assert(app.res.save_texture_io == nil)\n"
		"assert(app.res.save_texture == nil)\n"
		"assert(app.res.create_mega_texture == nil)\n"
		"assert(type(app.res.save_texture_user) == 'function')\n"
		"local texture = app.res.create_texture('test_texture', " + lua_string_literal(png) + ")\n"
		"assert(texture.valid)\n"
		"assert(app.res.save_texture_user('test_texture', 'saved.png'))\n"
		"assert(not app.res.save_texture_user('test_texture', '../outside.png'))\n"
		"return 0"
	};
	const std::string manifest{
		"manifest = { name = '" + app_name +
		"', headless = true, permissions = {}, scripts = { main = 'main.lua' }, entry_point = 'main' }"
	};

	ax::EmbeddedResourceLayout layout;
	layout.emplace("manifest.luac", ax::EmbeddedResource{
		reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size() });
	layout.emplace("main.lua", ax::EmbeddedResource{
		reinterpret_cast<const uint8_t*>(entry_point.data()), entry_point.size() });

	auto app{ ax::Application::from_embedded({}, std::move(layout)) };
	ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));

	std::ifstream file{ user_file, std::ios::binary };
	ASSERT_TRUE(file.is_open());
	unsigned char signature[8]{};
	ASSERT_TRUE(file.read(reinterpret_cast<char*>(signature), sizeof(signature)));
	const unsigned char expected_signature[]{ 137, 80, 78, 71, 13, 10, 26, 10 };
	EXPECT_TRUE(std::equal(signature, signature + sizeof(signature), expected_signature));
}

TEST(LuaResourceBindings, InvalidTextureAndAtlasInputsFailGracefully)
{
	testlog::LogCapture::instance().expect_total_error_count(2);

	const auto app_name{ make_app_name("axeng_test_invalid_resources_") };
	const std::string entry_point{
		"local texture = app.res.create_texture('invalid_texture', 'not a png')\n"
		"assert(not texture.valid)\n"
		"assert(not app.res.get_texture('invalid_texture').valid)\n"
		"assert(not app.res.get_texture_regions('invalid_texture').valid)\n"
		"assert(app.res.create_mega_texture('invalid_atlas', { 'missing.png' }, 0) ~= 0)\n"
		"assert(not app.res.get_texture_regions('invalid_atlas').valid)\n"
		"return 0"
	};
	const std::string manifest{
		"manifest = { name = '" + app_name +
		"', headless = true, permissions = { 'io' }, scripts = { main = 'main.lua' }, entry_point = 'main' }"
	};

	ax::EmbeddedResourceLayout layout;
	layout.emplace("manifest.luac", ax::EmbeddedResource{
		reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size() });
	layout.emplace("main.lua", ax::EmbeddedResource{
		reinterpret_cast<const uint8_t*>(entry_point.data()), entry_point.size() });

	flag_set<ax::lua::Permission> permissions;
	permissions |= ax::lua::Permission::IO;
	auto app{ ax::Application::from_embedded(permissions, std::move(layout)) };
	ASSERT_TRUE(app.try_load({}, wgpu::BackendType::Undefined));
}
