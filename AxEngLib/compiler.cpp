#include "compiler.h"

#include "lua_engine.h"
#include "bind_all.h"
#include "lua_bindings.h"
#include "log_timer.h"

#include <filesystem>
#include <fstream>
#include <ranges>

static ax::Error setup_lua_compiler(sol::state& compiler, sol::environment& env)
{
	ax::LogTimer tmr{ "setup lua compiler" };

	sol::load_result load_res{ compiler.load_file("lua_comp/main.lua", sol::load_mode::text)};
	if (!load_res.valid())
	{
		sol::error err = load_res;
		spdlog::error("Failed to load compilation file, err: {}", err.what());
		return ax::Error::Lua;
	}
	
	sol::function func{ load_res };
	sol::set_environment(env, func);
	const auto& res{ func() };

	if (!res.valid())
	{
		sol::error err = res;
		spdlog::error("Failed to run compilation file, err: {}", err.what());
		return ax::Error::Lua;
	}

	return ax::Error::Success;
}

static ax::Error validate_project(std::string_view inDir, sol::state& compiler, sol::environment& env)
{
	ax::LogTimer tmr{ "validate project file" };

	sol::function validate{ env["validate_project_file"] };
	sol::set_environment(env, validate);

	std::string projectFile{ inDir };
	projectFile += "/project.lua";
	compiler.load_file(projectFile);

	sol::function func{ compiler.load_file(projectFile, sol::load_mode::text) };
	sol::set_environment(env, func);
	const auto& res{ func() };

	if (!res.valid())
	{
		sol::error err = res;
		spdlog::error("Failed to run file {}, err: {}", projectFile, err.what());
		return ax::Error::Lua;
	}

	bool success{ validate() };
	if (!success)
	{
		spdlog::error("Failed to validate project!");
		return ax::Error::InvalidConfiguration;
	}

	spdlog::info("<Build> Validated project.");
	return ax::Error::Success;
}

static ax::Error setup_directory(std::string_view inDir, std::string_view outDir)
{
	ax::LogTimer tmr{ "setup output directory" };

	std::filesystem::path in{ inDir };
	std::filesystem::path out{ outDir };

	for (const auto& dir : std::filesystem::recursive_directory_iterator(in))
	{
		if (dir.is_directory())
		{
			const auto& substr{ dir.path().string().substr(in.string().length() + 1) };
			const auto& newDir{ out / substr };

			std::error_code err;
			if (!std::filesystem::create_directory(newDir, err))
			{
				spdlog::error("Failed to create directory '{}': {}", newDir.string(), err.message());
				return ax::Error::IO;
			}
		}
	}

	spdlog::info("<Build> Setup output directory.");
	return ax::Error::Success;
}

static ax::Error validate_lua_files(std::string_view inDir, std::string_view outDir, sol::environment& env)
{
	ax::LogTimer tmr{ "validate scripts" };

	std::filesystem::path in{ inDir };
	std::filesystem::path out{ outDir };
	std::filesystem::path newExt{ ".luac" };

	sol::function checkFunc{ env["check_and_copy_script"] };
	sol::set_environment(env, checkFunc);

	const auto& project{ env["project"] };
	const sol::table& files{ project["scripts"].get<sol::table>() };
	auto built_script_table{ env.create() };

	for (const auto& kvp : files)
	{
		const std::filesystem::path path{ in / kvp.second.as<std::string>() };

		if (path.extension() != ".lua")
		{
			spdlog::error("All script files must have '.lua' extension, '{}' does not.", path.string());
			return ax::Error::InvalidConfiguration;
		}

		const auto& substr{ path.string().substr(in.string().length() + 1) };
		auto newFile{ out / substr };

		newFile.replace_extension(newExt);
		const auto res{ checkFunc(path.string(), newFile.string(), true) };
		if (!res.valid())
		{
			sol::error err = res;
			spdlog::error("Failed to validate lua file '{}':\n{}", path.string(), err.what());
			return ax::Error::Lua;
		}

		spdlog::debug("<Build> Validated & Copied: '{}' -> '{}'", path.string(), newFile.string());

		built_script_table[kvp.first] = substr + "c";
	}

	env["built_scripts"] = built_script_table;
	spdlog::info("<Build> Validated scripts.");
	return ax::Error::Success;
}

static ax::Error validate_texture_files(std::string_view inDir, std::string_view outDir, sol::environment& env)
{
	ax::LogTimer tmr{ "validate textures" };

	std::filesystem::path in{ inDir };
	std::filesystem::path out{ outDir };

	sol::function checkFunc{ env["check_and_copy_texture"] };
	sol::set_environment(env, checkFunc);

	const auto& project{ env["project"] };
	const sol::table& files{ project["textures"].get<sol::table>() };
	auto out_texture_table{ env.create() };

	for (const auto& kvp : files)
	{
		const std::filesystem::path path{ in / kvp.second.as<std::string>() };

		if (path.extension() != ".png")
		{
			spdlog::error("All textures files must have '.png' extension, '{}' does not.", path.string());
			return ax::Error::InvalidConfiguration;
		}

		const auto& substr{ path.string().substr(in.string().length() + 1) };
		auto newFile{ out / substr };

		//newFile.replace_extension(newExt);
		const auto res{ checkFunc(path.string(), newFile.string(), true) };
		if (!res.valid())
		{
			sol::error err = res;
			spdlog::error("Failed to validate texture file '{}':\n{}", path.string(), err.what());
			return ax::Error::InvalidTexture;
		}

		spdlog::debug("<Build> Validated & Copied: '{}' -> '{}'", path.string(), newFile.string());

		out_texture_table[kvp.first] = substr;
	}

	env["out_textures"] = out_texture_table;
	spdlog::info("<Build> Validated textures.");
	return ax::Error::Success;
}

static ax::Error create_manifest(std::string_view outDir, sol::environment& env)
{
	ax::LogTimer tmr{ "create manifest" };

	sol::function createManifest{ env["create_manifest"] };
	sol::set_environment(env, createManifest);

	const auto res{ createManifest(outDir, true) };
	if (!res.valid())
	{
		sol::error err = res;
		spdlog::error("Failed to create manifest: {}", err.what());
		return ax::Error::Lua;
	}

	spdlog::info("<Build> Generated Manifest.");
	return ax::Error::Success;
}

ax::Error ax::comp::clean(std::string_view outDir)
{
	LogTimer tmr{ "clean output dir" };

	ax::Error ret{ ax::Error::Success };
	std::error_code ioErr;

	if (std::filesystem::exists(outDir))
	{
		std::filesystem::remove_all(outDir, ioErr);

		if (ioErr)
		{
			spdlog::error("Failed to clean output directory: {}", ioErr.message());
			ret = ax::Error::IO;
			return ret;
		}

		spdlog::info("<Build> Cleaned old output directory.");
	}

	std::filesystem::create_directory(outDir, ioErr);
	if (ioErr)
	{
		spdlog::error("Failed to create output directory: {}", ioErr.message());
		ret = ax::Error::IO;
		return ret;
	}

	spdlog::info("<Build> Created output directory.");
	return ret;
}

ax::Error ax::comp::compile(std::string_view inDir, std::string_view outDir, bool zipItUp)
{
	LogTimer tmr{ "compilation" };

	ax::Error ret{ ax::Error::Success };

	sol::state compiler;
	compiler.open_libraries
	(
		sol::lib::base,
		sol::lib::os,
		sol::lib::io,
		sol::lib::string
	);
	sol::environment env{ compiler, sol::create, compiler.globals() };

	ax::lua::bindings::setup();
	ax::lua::bindings::bind_to_state(compiler);

	AX_RETURN_ERROR_IF_FAIL(ret, setup_directory(inDir, outDir));
	AX_RETURN_ERROR_IF_FAIL(ret, setup_lua_compiler(compiler, env));

	AX_RETURN_ERROR_IF_FAIL(ret, validate_project(inDir, compiler, env));
	AX_RETURN_ERROR_IF_FAIL(ret, validate_lua_files(inDir, outDir, env));
	AX_RETURN_ERROR_IF_FAIL(ret, validate_texture_files(inDir, outDir, env));

	AX_RETURN_ERROR_IF_FAIL(ret, create_manifest(outDir, env));

	ax::lua::bindings::cleanup_state(compiler);

	spdlog::info("<Build> Built project '{}' to '{}'.", env["project"]["name"].get<std::string>(), outDir);

	if (zipItUp)
	{
		try
		{
			std::filesystem::path outPath{ outDir };
			std::filesystem::path zipPath = outPath;
			zipPath += ".zip";

			std::string pathPattern = outPath.string();
			if (pathPattern.back() == '/' || pathPattern.back() == '\\')
				pathPattern += "*";
			else
				pathPattern += "\\*";

			std::string cmd = "powershell -NoProfile -Command \"Compress-Archive -Path '"
				+ pathPattern + "' -DestinationPath '" + zipPath.string() + "' -Force\"";

			int rc = std::system(cmd.c_str());
			if (rc != 0)
			{
				spdlog::error("Failed to create zip '{}', exit code: {}", zipPath.string(), rc);
				ret = ax::Error::IO;
			}
			else
			{
				spdlog::info("<Build> Packaged output to '{}'.", zipPath.string());
			}
		}
		catch (const std::exception& ex)
		{
			spdlog::error("Exception while creating zip: {}", ex.what());
			ret = ax::Error::IO;
		}
	}

	return ret;
}
