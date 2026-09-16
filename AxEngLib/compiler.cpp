#include "compiler.h"

#include "application.h"
#include "axeng.h"

#include <filesystem>
#include <fstream>
#include <ranges>

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

	AX_RETURN_ERROR_IF_FAIL(ret, setup_directory(inDir, outDir));

	const std::vector<std::string> args{ "--in", std::string(inDir), "--out", std::string(outDir) };
	AX_RETURN_ERROR_IF_FAIL(ret, ax::run_from_directory(args, "../AxCompiler"));

	if (zipItUp)
		ret = zip(outDir);

	return ret;
}
