#include "axeng/core/user_files.h"

#include "axeng/core/application.h"

#include <minizip-ng/mz.h>
#include <minizip-ng/mz_strm.h>
#include <minizip-ng/mz_zip.h>
#include <minizip-ng/mz_zip_rw.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <shlobj.h>
#include <thread>
#include <windows.h>

std::optional<std::filesystem::path> get_app_data_directory() noexcept
{
    char path[MAX_PATH];

    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path)))
        return std::filesystem::path(path);

    return std::nullopt;
}

ax::Error ax::UserFileManager::set_application(const Application& app, const std::filesystem::path& user_directory) noexcept
{
	if (!user_directory.empty())
	{
		m_directory = user_directory;
		return setup_directory();
	}

    const auto base = get_app_data_directory();
    if (!base.has_value())
    {
        spdlog::error("Failed to get the AppData directory.");
        return ax::Error::IO;
    }

    m_directory = *base / "AxEng" / "app_user" / app.safe_directory_name();
    return setup_directory();
}

ax::Error ax::UserFileManager::setup_directory() noexcept
{
    spdlog::debug("Setting up user file directory: {}", m_directory.string());

    if (!std::filesystem::exists(m_directory))
    {
        if (!std::filesystem::create_directories(m_directory))
        {
            spdlog::error("Failed to create directory: {}", m_directory.string());
            return ax::Error::IO;
        }
    }
    return ax::Error::Success;
}

bool is_file_inside_directory(const std::filesystem::path& file, const std::filesystem::path& directory) noexcept
{
    const auto full_file_path = std::filesystem::absolute(file).lexically_normal();
    const auto full_directory_path = std::filesystem::absolute(directory).lexically_normal();
    return std::mismatch(full_directory_path.begin(), full_directory_path.end(), full_file_path.begin()).first == full_directory_path.end();
}

ax::UserFileHandle* ax::UserFileManager::open_file(const std::string &filename, const char *mode) noexcept
{
    m_open_files.emplace_back();
    UserFileHandle& handle = m_open_files.back();
    handle.manager = this;

    const auto filePath = m_directory / filename;
    if (!is_file_inside_directory(filePath, m_directory))
    {
        spdlog::error("Attempted to open a file outside the user directory: {}", filePath.string());
        return nullptr;
    }

    const std::string openMode{ mode };
    const bool readMode{ openMode.find('r') != std::string::npos };
    handle.file.open(filePath, std::ios::binary | (readMode ? std::ios::in : 0) | (openMode.find('w') != std::string::npos ? std::ios::out : 0));
    if (!handle.file.is_open())
    {
        if (!readMode || std::filesystem::exists(filePath))
            spdlog::error("Failed to open user file '{}'.", filePath.string());
        m_open_files.pop_back();
        return nullptr;
    }
    return &handle;
}

ax::Error ax::UserFileManager::close_file(UserFileHandle *handle) noexcept
{
    if (!handle)
        return ax::Error::Success;

    m_open_files.remove_if([handle](const UserFileHandle& file) { return &file == handle; });
    return ax::Error::Success;
}

void ax::UserFileHandle::close() noexcept
{
    if (manager)
        manager->close_file(this);
}

std::optional<std::filesystem::path> ax::UserFileManager::get_user_file_path(const std::string& filename) const noexcept
{
    const auto ret{ (m_directory / filename).lexically_normal() };

    if (!is_file_inside_directory(ret, m_directory))
    {
        spdlog::error("Attempted to get a user file path outside the user directory: {}", ret.string());
        return std::nullopt;
    }

    return ret;
}

std::expected<void, std::string> ax::UserFileManager::create_zip
(
    const std::vector<std::string>& filenames,
    const std::string& output_filename,
    bool remove_sources
) const
{
    if (filenames.empty())
        return std::unexpected{ "ZIP archive requires at least one file" };

    const auto output_path{ get_user_file_path(output_filename) };
    if (!output_path)
        return std::unexpected{ "ZIP output path is outside the user directory" };

    std::vector<std::pair<std::filesystem::path, std::string>> entries;
    entries.reserve(filenames.size());
    for (const auto& filename : filenames)
    {
        const auto input_path{ get_user_file_path(filename) };
        if (!input_path)
            return std::unexpected{ "ZIP input path is outside the user directory: " + filename };
        if (*input_path == *output_path)
            return std::unexpected{ "ZIP output cannot also be an input file" };

        std::error_code error;
        if (!std::filesystem::is_regular_file(*input_path, error) || error)
            return std::unexpected{ "ZIP input file does not exist or is not a regular file: " + filename };

        entries.emplace_back(*input_path, input_path->filename().string());
    }

    auto temporary_path{ *output_path };
    temporary_path += L".tmp";
    std::error_code filesystem_error;
    std::filesystem::remove(temporary_path, filesystem_error);
    if (filesystem_error)
        return std::unexpected{ "Failed to clear temporary ZIP output: " + filesystem_error.message() };

    void* writer{ mz_zip_writer_create() };
    if (writer == nullptr)
        return std::unexpected{ "Failed to create ZIP writer" };

    const auto open_result{ mz_zip_writer_open_file(writer, temporary_path.string().c_str(), 0, 0) };
    if (open_result != MZ_OK)
    {
        mz_zip_writer_delete(&writer);
        std::filesystem::remove(temporary_path, filesystem_error);
        return std::unexpected{ "Failed to open ZIP output (minizip error " + std::to_string(open_result) + ")" };
    }

    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_NORMAL);

    for (const auto& [input_path, entry_name] : entries)
    {
        const auto add_result{ mz_zip_writer_add_file(writer, input_path.string().c_str(), entry_name.c_str()) };
        if (add_result != MZ_OK)
        {
            mz_zip_writer_close(writer);
            mz_zip_writer_delete(&writer);
            std::filesystem::remove(temporary_path, filesystem_error);
            return std::unexpected{ "Failed to add ZIP entry '" + entry_name + "' (minizip error " + std::to_string(add_result) + ")" };
        }
    }

    const auto close_result{ mz_zip_writer_close(writer) };
    mz_zip_writer_delete(&writer);
    if (close_result != MZ_OK)
    {
        std::filesystem::remove(temporary_path, filesystem_error);
        return std::unexpected{ "Failed to finalize ZIP archive (minizip error " + std::to_string(close_result) + ")" };
    }

    bool replaced{ false };
    DWORD replace_error{};
    auto retryDelay{ std::chrono::milliseconds{ 10 } };
    for (std::uint32_t attempt{}; attempt < 8; ++attempt)
    {
        replaced = MoveFileExW(temporary_path.c_str(), output_path->c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (replaced) break;
        replace_error = GetLastError();
        if (replace_error != ERROR_ACCESS_DENIED && replace_error != ERROR_SHARING_VIOLATION) break;
        std::this_thread::sleep_for(retryDelay);
        retryDelay = std::min(retryDelay * 2, std::chrono::milliseconds{ 640 });
    }
    if (!replaced)
    {
        std::filesystem::remove(temporary_path, filesystem_error);
        return std::unexpected{ "Failed to replace ZIP output (Windows error " + std::to_string(replace_error) + ")" };
    }

    if (remove_sources)
    {
        for (const auto& filename : filenames)
        {
            if (remove_file(filename))
                continue;

            const auto source_path{ get_user_file_path(filename) };
            std::error_code error;
            if (!source_path || std::filesystem::exists(*source_path, error) || error)
                return std::unexpected{ "ZIP created, but failed to remove source file: " + filename };
        }
    }

    return {};
}

std::optional<std::string> ax::UserFileManager::read_zip_entry
(
    const std::string& zip_filename,
    const std::string& entry_filename
) const
{
    const auto zip_path{ get_user_file_path(zip_filename) };
    if (!zip_path)
        return std::nullopt;

    void* reader{ mz_zip_reader_create() };
    if (reader == nullptr)
    {
        spdlog::error("Failed to create ZIP reader for '{}'.", zip_filename);
        return std::nullopt;
    }

    const auto open_result{ mz_zip_reader_open_file(reader, zip_path->string().c_str()) };
    if (open_result != MZ_OK)
    {
        spdlog::error("Failed to open ZIP file '{}': {}", zip_filename, open_result);
        mz_zip_reader_delete(&reader);
        return std::nullopt;
    }

    const auto close_reader = [&reader]()
    {
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
    };

    const auto locate_result{ mz_zip_reader_locate_entry(reader, entry_filename.c_str(), 1) };
    if (locate_result != MZ_OK)
    {
        spdlog::error("ZIP entry '{}' was not found in '{}': {}", entry_filename, zip_filename, locate_result);
        close_reader();
        return std::nullopt;
    }

    const auto entry_open_result{ mz_zip_reader_entry_open(reader) };
    if (entry_open_result != MZ_OK)
    {
        spdlog::error("Failed to open ZIP entry '{}' in '{}': {}", entry_filename, zip_filename, entry_open_result);
        close_reader();
        return std::nullopt;
    }

    mz_zip_file* info{ nullptr };
    const auto info_result{ mz_zip_reader_entry_get_info(reader, &info) };
    if (info_result != MZ_OK || info == nullptr || info->uncompressed_size < 0
        || static_cast<uint64_t>(info->uncompressed_size) > std::string{}.max_size())
    {
        spdlog::error("Failed to read ZIP entry metadata '{}' from '{}': {}", entry_filename, zip_filename, info_result);
        mz_zip_reader_entry_close(reader);
        close_reader();
        return std::nullopt;
    }

    std::string content(static_cast<size_t>(info->uncompressed_size), '\0');
    size_t offset{ 0 };
    bool read_success{ true };
    while (offset < content.size())
    {
        const auto chunk_size{ static_cast<int32_t>(std::min<size_t>(content.size() - offset, 64 * 1024)) };
        const auto bytes_read{ mz_zip_reader_entry_read(reader, content.data() + offset, chunk_size) };
        if (bytes_read <= 0)
        {
            spdlog::error("Failed to read ZIP entry '{}' from '{}': {}", entry_filename, zip_filename, bytes_read);
            read_success = false;
            break;
        }
        offset += static_cast<size_t>(bytes_read);
    }

    const auto entry_close_result{ mz_zip_reader_entry_close(reader) };
    const auto zip_close_result{ mz_zip_reader_close(reader) };
    mz_zip_reader_delete(&reader);
    if (!read_success || entry_close_result != MZ_OK || zip_close_result != MZ_OK)
    {
        spdlog::error("Failed to close ZIP entry '{}' from '{}': entry {}, archive {}",
            entry_filename, zip_filename, entry_close_result, zip_close_result);
        return std::nullopt;
    }

    return content;
}

bool ax::UserFileManager::remove_file(const std::string& filename) const noexcept
{
    const auto path{ get_user_file_path(filename) };
    if (!path.has_value())
        return false;

    std::error_code error{};
    if (!std::filesystem::is_regular_file(*path, error))
        return false;

    const bool removed{ std::filesystem::remove(*path, error) };
    if (error)
        spdlog::error("Failed to remove user file '{}': {}", path->string(), error.message());
    return removed && !error;
}
