#include "compiler.h"

#include "lua_engine.h"
#include "bind_all.h"
#include "lua_bindings.h"
#include "log_timer.h"

#include <filesystem>
#include <fstream>
#include <ranges>

static ax::Error run_lua_compiler(sol::state& compiler, sol::environment& env, std::string_view inDir, std::string_view outDir)
{
	ax::LogTimer tmr{ "setup lua compiler" };

	// Load the compiler script
	sol::load_result load_res{ compiler.load_file("lua_comp/compiler.lua", sol::load_mode::text)};
	if (!load_res.valid())
	{
		sol::error err = load_res;
		spdlog::error("Failed to load compilation file, err: {}", err.what());
		return ax::Error::Lua;
	}
	
	sol::function func{ load_res };
	sol::set_environment(env, func);
	const auto& scriptRes{ func() };

	if (!scriptRes.valid())
	{
		sol::error err = scriptRes;
		spdlog::error("Failed to run compilation file, err: {}", err.what());
		return ax::Error::Lua;
	}

	// Run compiler.lua::compile(inDir, outDir)
	sol::function compileFunc{ env["compile"] };
	sol::set_environment(env, compileFunc);
	const auto& compileRes = compileFunc(inDir, outDir);
	if (!compileRes.valid())
	{
		sol::error err = compileRes;
		spdlog::error("Failed run compile function, err: {}", err.what());
		return ax::Error::Lua;
	}

	return static_cast<ax::Error>(compileRes.get<uint32_t>());
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

ax::Error zip(std::string_view outDir)
{
	ax::Error ret{ ax::Error::Success };

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

		// TODO: Replace this with minizip or some such
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

	return ret;
}

ax::Error ax::comp::compile(std::string_view inDir, std::string_view outDir, bool zipItUp)
{
	LogTimer tmr{ "compilation" };

	ax::Error ret{ ax::Error::Success };

	spdlog::info("<Build> Building project from '{}'.", outDir);

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
	AX_RETURN_ERROR_IF_FAIL(ret, run_lua_compiler(compiler, env, inDir, outDir));

	ax::lua::bindings::cleanup_state(compiler);

	spdlog::info("<Build> Built project '{}' to '{}'.", env["compiled_project"]["name"].get<std::string>(), outDir);

	if (zipItUp)
		ret = zip(outDir);

	return ret;
}
