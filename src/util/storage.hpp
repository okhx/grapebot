#pragma once

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif
#include <Geode/Geode.hpp>

#include <asp/fs/std.hpp>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace grape::paths {



inline std::filesystem::path toStd(asp::fs::Path const& path) {
    return path.toStdPath().unwrapOr(std::filesystem::path{});
}

inline std::filesystem::path toStd(asp::fs::PathView path) {
    return path.toStdPath().unwrapOr(std::filesystem::path{});
}



inline std::string pathToString(asp::fs::Path const& path) {
    return path.string();
}

inline std::string pathToString(asp::fs::PathView path) {
    return path.string();
}

inline std::string pathToString(std::filesystem::path const& path) {
    return path.string();
}

inline const std::filesystem::path& gameRoot() {
    static const std::filesystem::path root = [] {
#ifdef GEODE_IS_WINDOWS
        std::wstring buffer(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0 || length == buffer.size()) {
            throw std::runtime_error("GetModuleFileNameW failed");
        }
        buffer.resize(length);
        return std::filesystem::path(buffer).parent_path();
#else
        return toStd(geode::Mod::get()->getConfigDir())
            .parent_path()
            .parent_path();
#endif
    }();
    return root;
}

inline const std::filesystem::path& dataRoot() {
    static const std::filesystem::path root = [] {
        auto path = gameRoot() / "Grape";
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error) {
            geode::log::error("Could not create Grape data directory: {}",
                              error.message());
        }
        return path;
    }();
    return root;
}

inline std::filesystem::path directory(std::string_view name) {
    auto path = dataRoot() / name;
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        geode::log::error("Could not create {}: {}", path, error.message());
    }
    return path;
}

inline std::filesystem::path file(std::string_view name) {
    return dataRoot() / name;
}

}
