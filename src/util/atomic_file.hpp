#pragma once

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

namespace grape::files {

inline bool replace(const std::filesystem::path& source,
                    const std::filesystem::path& target,
                    std::error_code& error) {
    error.clear();
#ifdef GEODE_IS_WINDOWS
    if (MoveFileExW(source.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    error = {static_cast<int>(GetLastError()), std::system_category()};
#else
    std::filesystem::rename(source, target, error);
    if (!error) return true;
#endif
    return false;
}

inline bool writeAtomically(const std::filesystem::path& target,
                            std::string_view data,
                            std::error_code& error) {
    auto temporary = target;
    temporary += ".tmp";

    std::filesystem::remove(temporary, error);
    error.clear();

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        if (!output) {
            error = std::make_error_code(std::io_errc::stream);
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }

    if (replace(temporary, target, error)) return true;
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
}

}
