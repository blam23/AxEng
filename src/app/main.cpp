#include <windows.h>

#include "axeng/core/axeng.h"
#include "axeng/comp/compiler.h"
#include "axeng/debug/debug_view.h"

#include "app/argparse/argparse.hpp"

#define RET(x) std::to_underlying(x)

int main(int argc, char* argv[])
{
	//
	// Parse Args
	//

	argparse::ArgumentParser program("AxEng");

	std::string inDirectory;
	program.add_argument("-i", "--in")
		.store_into(inDirectory)
		.help("Specifies the directory/zip to use for the given operation");

	std::string outDirectory;
	program.add_argument("-o", "--out")
		.store_into(outDirectory)
		.help("Where to store any results for the given operation");

	bool compile{ false };
	program.add_argument("-c", "--comp")
		.store_into(compile)
		.flag()
		.help("Compiles given application");

	bool clean{ false };
	program.add_argument("-x", "--clean")
		.store_into(clean)
		.flag()
		.help("Deletes the existing output directory/zip");

	bool run{ false };
	program.add_argument("-r", "--run")
		.store_into(run)
		.flag()
		.help("Runs given application");

	bool verbose{ false };
	program.add_argument("-v", "--verbose")
		.store_into(verbose)
		.flag()
		.help("Raises log level to highest possible");

	bool timers{ false };
	program.add_argument("-t", "--timers")
		.store_into(timers)
		.flag()
		.help("Enables logging of various timers");

	bool useZipFiles{ false };
	program.add_argument("-z", "--zip")
		.store_into(useZipFiles)
		.flag()
		.help("Compiles to / loads from a ZIP file instead of directory");

	bool recompileCompiler{ false };
	program.add_argument("-q", "--recompile")
		.store_into(recompileCompiler)
		.flag()
		.help("Recompiles the compiler before compiling the given application");

	bool allowIO{ false };
	program.add_argument("--allow-io")
		.store_into(allowIO)
		.flag()
		.help("Allows the application to perform IO operations");

	bool allowOS{ false };
	program.add_argument("--allow-os")
		.store_into(allowOS)
		.flag()
		.help("Allows the application to perform OS operations");

	bool allowThreads{ false };
	program.add_argument("--allow-threads")
		.store_into(allowThreads)
		.flag()
		.help("Allows the application to perform Threads operations");

	const std::vector<std::string> in_args{ argv, argv + argc };
	std::vector<std::string> out_args{};
	flag_set<ax::lua::Permission> permissions{};

	try
	{
		out_args = program.parse_known_args(in_args);

		if (verbose)
		{
			spdlog::set_level(spdlog::level::trace);

			for (int i = 0; i < argc; i++)
				spdlog::trace("<Main> Argument: {}", argv[i]);
		}

		spdlog::trace("<Main> Returned args: {}", out_args.size());
		for (const auto& a : out_args)
		{
			spdlog::trace("<Main> \t'{}'", a);
		}
	}
	catch (const std::exception& err)
	{
		spdlog::error("Failed to parse arguments: {}", err.what());
		spdlog::error("{}", program.help().str());
		return RET(ax::Error::InvalidConfiguration);
	}

	//
	// Validate Args
	//

	if (allowIO)
		permissions |= ax::lua::Permission::IO;

	if (allowOS)
		permissions |= ax::lua::Permission::OS;

	if (allowThreads)
		permissions |= ax::lua::Permission::Threads;

	if (timers)
		ax::enable_log_timers();

	if (clean and (run and not compile))
	{
		spdlog::error("Cannot clean and run when not compiling.");
		spdlog::error("{}", program.help().str());
		return RET(ax::Error::InvalidConfiguration);
	}
	if (inDirectory == "" and (compile or run))
	{
		spdlog::error("In directory required for given operation.");
		spdlog::error("{}", program.help().str());
		return RET(ax::Error::InvalidConfiguration);
	}
	if (outDirectory == "" and (compile or clean))
	{
		spdlog::error("Output directory required for given operation.");
		spdlog::error("{}", program.help().str());
		return RET(ax::Error::InvalidConfiguration);
	}

	//
	// Run Given Operation
	//

	bool doneSomething{ false };

	if (recompileCompiler)
	{
		if (!allowIO || !allowOS)
		{
			spdlog::error("Compiling requires both IO and OS permissions.");
			spdlog::error("{}", program.help().str());
			return RET(ax::Error::InvalidConfiguration);
		}

		doneSomething = true;
		const auto err{ ax::comp::recompile_compiler() };
		if (err != ax::Error::Success)
			return RET(err);
	}

	if (clean)
	{
		if (!allowIO || !allowOS)
		{
			spdlog::error("Cleaning the output directory requires both IO and OS permissions.");
			spdlog::error("{}", program.help().str());
			return RET(ax::Error::InvalidConfiguration);
		}

		doneSomething = true;

		const auto err{ ax::comp::clean(outDirectory) };
		if (err != ax::Error::Success)
			return RET(err);
	}

	if (compile)
	{
		if (!allowIO || !allowOS)
		{
			spdlog::error("Compiling requires both IO and OS permissions.");
			spdlog::error("{}", program.help().str());
			return RET(ax::Error::InvalidConfiguration);
		}

		doneSomething = true;

		const auto err{ ax::comp::compile(inDirectory, outDirectory, useZipFiles) };
		if (err != ax::Error::Success)
			return RET(err);
	}

	if (run)
	{
		doneSomething = true;

		if (useZipFiles)
		{
			const auto err{ ax::run_from_zip(permissions, out_args, compile ? outDirectory + ".zip" : inDirectory)};
			if (err != ax::Error::Success)
				return RET(err);
		}
		else
		{
			const auto err{ ax::run_from_directory(permissions, out_args, compile ? outDirectory : inDirectory) };
			if (err != ax::Error::Success)
				return RET(err);
		}
	}

	if (!doneSomething)
	{
		spdlog::error("Please specify an operation to perform.");
		spdlog::error("{}", program.help().str());
		return RET(ax::Error::InvalidConfiguration);
	}

	return RET(ax::Error::Success);
}
