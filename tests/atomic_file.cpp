#include "util/atomic_file.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#define CHECK(condition) \
    do { if (!(condition)) return __LINE__; } while (false)

int main() {
    std::error_code error;
    const auto temporary = std::filesystem::temp_directory_path(error);
    CHECK(!error);
    const auto directory = temporary / ("grape-atomic-file-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory, error);
    CHECK(!error);
    const auto file = directory / "settings.json";

    CHECK(grape::files::writeAtomically(file, "old", error));
    CHECK(grape::files::writeAtomically(file, "new", error));

    std::ifstream input(file, std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(input), {}};
    CHECK(contents == "new");
    CHECK(!std::filesystem::exists(file.string() + ".tmp"));

    const auto missing = directory / "missing" / "settings.json";
    CHECK(!grape::files::writeAtomically(missing, "data", error));
    CHECK(error);
    CHECK(!std::filesystem::exists(missing.string() + ".tmp"));

    std::filesystem::remove_all(directory);
}
