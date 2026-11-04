#include "axeng/comp/compiler.h"

#include "axeng/core/application.h"
#include "axeng/core/axeng.h"

#include <minizip-ng/mz.h>
#include <minizip-ng/mz_strm.h>
#include <minizip-ng/mz_zip.h>
#include <minizip-ng/mz_zip_rw.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <vector>

static ax::Error setup_directory(std::string_view inDir, std::string_view outDir)
{
	ax::LogTimer tmr{ "setup output directory" };

	std::filesystem::path in{ std::filesystem::canonical(inDir) };

	std::error_code err;
	if (!std::filesystem::create_directory(outDir, err))
	{
		if (err.value() > 0)
		{
			spdlog::error("Failed to create directory '{}': {}", outDir, err.message());
			return ax::Error::IO;
		}
	}

	std::filesystem::path out{ std::filesystem::canonical(outDir) };

	for (const auto& dir : std::filesystem::recursive_directory_iterator(in))
	{
		if (dir.is_directory())
		{
			const auto& substr{ dir.path().string().substr(in.string().length() + 1) };
			const auto& newDir{ out / substr };

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

		std::vector<std::pair<std::filesystem::path, std::string>> files;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(outPath))
		{
			if (!entry.is_regular_file())
				continue;
			files.emplace_back(entry.path(), entry.path().lexically_relative(outPath).generic_string());
		}
		std::ranges::sort(files, [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });

		std::error_code filesystemError;
		std::filesystem::remove(zipPath, filesystemError);
		if (filesystemError)
		{
			spdlog::error("Failed to clear old ZIP output '{}': {}", zipPath.string(), filesystemError.message());
			return ax::Error::IO;
		}

		void* writer{ mz_zip_writer_create() };
		if (writer == nullptr)
		{
			spdlog::error("Failed to create ZIP writer for '{}'.", zipPath.string());
			return ax::Error::IO;
		}

		const auto openResult{ mz_zip_writer_open_file(writer, zipPath.string().c_str(), 0, 0) };
		if (openResult != MZ_OK)
		{
			mz_zip_writer_delete(&writer);
			spdlog::error("Failed to open ZIP output '{}' (minizip error {}).", zipPath.string(), openResult);
			return ax::Error::IO;
		}

		mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
		mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_NORMAL);
		for (const auto& [inputPath, entryName] : files)
		{
			const auto addResult{ mz_zip_writer_add_file(writer, inputPath.string().c_str(), entryName.c_str()) };
			if (addResult != MZ_OK)
			{
				mz_zip_writer_close(writer);
				mz_zip_writer_delete(&writer);
				std::filesystem::remove(zipPath, filesystemError);
				spdlog::error("Failed to add ZIP entry '{}' (minizip error {}).", entryName, addResult);
				return ax::Error::IO;
			}
		}

		const auto closeResult{ mz_zip_writer_close(writer) };
		mz_zip_writer_delete(&writer);
		if (closeResult != MZ_OK)
		{
			std::filesystem::remove(zipPath, filesystemError);
			spdlog::error("Failed to finalize ZIP archive '{}' (minizip error {}).", zipPath.string(), closeResult);
			return ax::Error::IO;
		}
		spdlog::info("<Build> Packaged output to '{}'.", zipPath.string());
	}
	catch (const std::exception& ex)
	{
		spdlog::error("Exception while creating zip: {}", ex.what());
		ret = ax::Error::IO;
	}

	return ret;
}

ax::Error ax::comp::compile(std::string_view inDir, std::string_view outDir, bool zipItUp, bool sourceScripts)
{
	LogTimer tmr{ "compilation" };

	ax::Error ret{ ax::Error::Success };

	spdlog::info("<Build> Building project from '{}'.", outDir);

	AX_RETURN_ERROR_IF_FAIL(ret, setup_directory(inDir, outDir));

	std::vector<std::string> args{ "--in", std::string(inDir), "--out", std::string(outDir) };
	if (sourceScripts)
		args.emplace_back("--source");
	AX_RETURN_ERROR_IF_FAIL(ret, ax::run_from_directory({ lua::Permission::IO | lua::Permission::OS }, args, "AxCompiler"));

	if (zipItUp)
		ret = zip(outDir);

	return ret;
}

ax::Error ax::comp::recompile_compiler()
{
	LogTimer tmr{ "recompilation" };
	ax::Error ret{ ax::Error::Success };

	const std::filesystem::path inDir{ "compiler" };
	const std::filesystem::path outDir{ "AxCompiler2" };
	const std::filesystem::path finalDir{ "AxCompiler" };
	const std::filesystem::path oldDir{ "AxCompiler_old" };

	clean(oldDir.string());
	clean(outDir.string());

	spdlog::info("<Build> Building compiler from '{}' to '{}'.", inDir.string(), outDir.string());

	AX_RETURN_ERROR_IF_FAIL(ret, compile(inDir.string(), outDir.string(), false, false));

	if (ret == ax::Error::Success)
	{
		spdlog::info("<Build> Renaming old compiler to: '{}'", oldDir.string());
		std::error_code ioErr;
		
		std::filesystem::rename(finalDir, oldDir, ioErr);
		if (ioErr)
		{
			spdlog::error("Failed to rename old compiler directory: '{}'", ioErr.message());
			ret = ax::Error::IO;
			return ret;
		}

		spdlog::info("<Build> New compiler built successfully, renaming to: '{}'", finalDir.string());

		std::filesystem::rename(outDir, finalDir, ioErr);
		if (ioErr)
		{
			spdlog::error("Failed to rename new compiler directory: '{}'", ioErr.message());
			ret = ax::Error::IO;
			return ret;
		}
	}

	return ret;
}
