#include "axeng/core/user_files.h"

#include "axeng/core/application.h"

#include <optional>
#include <shlobj.h>
#include <windows.h>

std::optional<std::filesystem::path> get_app_data_directory() noexcept
{
    char path[MAX_PATH];

    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path)))
        return std::filesystem::path(path);

    return std::nullopt;
}

ax::Error ax::UserFileManager::set_application(const Application& app) noexcept
{
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
    const auto full_file_path = std::filesystem::absolute(file);
    const auto full_directory_path = std::filesystem::absolute(directory);
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
    const auto ret{ m_directory / filename };

    if (!is_file_inside_directory(ret, m_directory))
    {
        spdlog::error("Attempted to get a user file path outside the user directory: {}", ret.string());
        return std::nullopt;
    }

    return ret;
}

bool ax::UserFileManager::remove_file(const std::string& filename) noexcept
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
