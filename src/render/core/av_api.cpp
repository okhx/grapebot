#include "av_api.hpp"

#include "oxorany.h"

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace grape::render {

namespace {

std::vector<std::string> libraryNames() {
    
    
#if defined(GEODE_IS_WINDOWS)
    return {oxorany("avutil-60.dll"), oxorany("swresample-6.dll"),
            oxorany("avcodec-62.dll"), oxorany("avformat-62.dll")};
#elif defined(__APPLE__)
    return {oxorany("libavutil.60.dylib"), oxorany("libswresample.6.dylib"),
            oxorany("libavcodec.62.dylib"), oxorany("libavformat.62.dylib")};
#else
    return {oxorany("libavutil.so.60"), oxorany("libswresample.so.6"),
            oxorany("libavcodec.so.62"), oxorany("libavformat.so.62")};
#endif
}

std::string lastLoadError() {
#ifdef GEODE_IS_WINDOWS
    const DWORD code = GetLastError();
    LPSTR buffer = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string message =
        buffer ? std::string(buffer) : fmt::format("error {}", code);
    if (buffer) LocalFree(buffer);
    return message;
#else
    char const* error = dlerror();
    return error ? std::string(error) : std::string("unknown error");
#endif
}

void* openLibrary(std::filesystem::path const& path) {
#ifdef GEODE_IS_WINDOWS
    return reinterpret_cast<void*>(LoadLibraryW(path.wstring().c_str()));
#else
    return dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void closeLibrary(void* module) {
    if (!module) return;
#ifdef GEODE_IS_WINDOWS
    FreeLibrary(reinterpret_cast<HMODULE>(module));
#else
    dlclose(module);
#endif
}

}

void* AvApi::resolve(char const* name) const {
    for (void* module : m_modules) {
        if (!module) continue;
#ifdef GEODE_IS_WINDOWS
        if (auto* fn = GetProcAddress(reinterpret_cast<HMODULE>(module), name))
            return reinterpret_cast<void*>(fn);
#else
        if (void* fn = dlsym(module, name)) return fn;
#endif
    }
    return nullptr;
}

geode::Result<> AvApi::load() {
    unload();

    auto const directory = grape::paths::directory("libraries");
    std::vector<std::filesystem::path> search{directory};
#ifdef __APPLE__
    
    
    
    search.emplace_back("/opt/homebrew/lib");
    search.emplace_back("/usr/local/lib");
#endif
    std::vector<std::string> unopened;

    for (auto const& name : libraryNames()) {
        void* module = nullptr;
        for (auto const& folder : search) {
            module = openLibrary(folder / name);
            if (module) break;
        }
        if (!module) {
            module = openLibrary(std::filesystem::path(name));
        }
        if (!module) {
            unopened.push_back(fmt::format("{} ({})", name, lastLoadError()));
            continue;
        }
        m_modules.push_back(module);
    }

    if (m_modules.empty()) {
        unload();
        return geode::Err("No FFmpeg libraries could be loaded: {}",
                          fmt::join(unopened, ", "));
    }

    std::vector<std::string> missing;
#define GRAPE_AV_RESOLVE_FIELD(name)                                       \
    this->name = reinterpret_cast<decltype(::name)*>(this->resolve(#name)); \
    if (!this->name) missing.emplace_back(#name);
    GRAPE_AV_FUNCTIONS(GRAPE_AV_RESOLVE_FIELD)
#undef GRAPE_AV_RESOLVE_FIELD

    if (!missing.empty()) {
        unload();
        return geode::Err("FFmpeg is missing {} entry point(s): {}",
                          missing.size(), fmt::join(missing, ", "));
    }

    m_loaded = true;
    return geode::Ok();
}

void AvApi::unload() {
#define GRAPE_AV_CLEAR_FIELD(name) this->name = nullptr;
    GRAPE_AV_FUNCTIONS(GRAPE_AV_CLEAR_FIELD)
#undef GRAPE_AV_CLEAR_FIELD

    for (void* module : m_modules) closeLibrary(module);
    m_modules.clear();
    m_loaded = false;
}

}
