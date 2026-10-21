#pragma once

#include "forward.h"
#include "error.h"
#include "helpers.h"

#include <expected>
#include <filesystem>
#include <fstream>
#include <list>
#include "spdlog/spdlog.h"
#include <string>
#include <vector>

namespace ax
{
    class UserFileManager;

    struct UserFileHandle
    {
        DISABLE_COPY_AND_MOVE(UserFileHandle);

        UserFileHandle() = default;

        std::string read_line() noexcept
        {
            std::string line;
            if (std::getline(file, line))
                return line;
            return {};
        }

        std::string read_all() noexcept
        {
            std::string content;
            file.seekg(0, std::ios::beg);
            content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return content;
        }

        bool write(const std::string& content) noexcept
        {
            file << content;
            file.flush();
            return file.good();
        }

        void close() noexcept;

        std::fstream file;
        UserFileManager* manager = nullptr;
    };

    class UserFileManager
    {
    public:
        DISABLE_COPY_AND_MOVE(UserFileManager);

        UserFileManager() = default;

        ax::Error set_application(const Application& app) noexcept;

        UserFileHandle* open_file(const std::string& filename, const char* mode) noexcept;
        ax::Error close_file(UserFileHandle* handle) noexcept;
        // Deletes a file in the user directory. Returns false if it doesn't exist or can't be removed.
        bool remove_file(const std::string& filename) const noexcept;

        std::optional<std::filesystem::path> get_user_file_path(const std::string& filename) const noexcept;
        std::expected<void, std::string> create_zip
        (
            const std::vector<std::string>& filenames,
            const std::string& output_filename,
            bool remove_sources = false
        ) const;
        std::optional<std::string> read_zip_entry
        (
            const std::string& zip_filename,
            const std::string& entry_filename
        ) const;

    private:
        ax::Error setup_directory() noexcept;

        std::filesystem::path m_directory;
        // Handles exposed to Lua must keep their addresses until closed.
        std::list<UserFileHandle> m_open_files;
    };
}