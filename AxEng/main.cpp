#include <windows.h>

#include "axenglib/axeng.h"
#include "axenglib/compiler.h"
#include "axenglib/debug_view.h"

#include "argparse/argparse.hpp"

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
		.help("Specifies the directory to use for the given operation.");

	std::string outDirectory;
	program.add_argument("-o", "--out")
		.store_into(outDirectory)
		.help("Where to store any results for the given operation");

	bool compile{ false };
	program.add_argument("-c", "--comp")
		.store_into(compile)
		.flag()
		.help("Compiles given application.");

	bool clean{ false };
	program.add_argument("-x", "--clean")
		.store_into(clean)
		.flag()
		.help("Deletes the existing output directory.");

	bool run{ false };
	program.add_argument("-r", "--run")
		.store_into(run)
		.flag()
		.help("Runs given application.");

	bool verbose{ false };
	program.add_argument("-v", "--verbose")
		.store_into(verbose)
		.flag()
		.help("Raises log level to highest possible.");

	try
	{
		program.parse_args(argc, argv);
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

	if (verbose)
		spdlog::set_level(spdlog::level::trace);

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

	if (clean)
	{
		doneSomething = true;

		const auto err{ ax::comp::clean(outDirectory) };
		if (err != ax::Error::Success)
			return RET(err);
	}

	if (compile)
	{
		doneSomething = true;

		const auto err{ ax::comp::compile(inDirectory, outDirectory) };
		if (err != ax::Error::Success)
			return RET(err);
	}

	if (run)
	{
		doneSomething = true;

		const auto err{ ax::run_from_directory(compile ? outDirectory : inDirectory) };
		if (err != ax::Error::Success)
			return RET(err);
	}

	if (!doneSomething)
	{
		spdlog::error("Please specify an operation to perform.");
		spdlog::error("{}", program.help().str());
		return RET(ax::Error::InvalidConfiguration);
	}

	return RET(ax::Error::Success);
}
