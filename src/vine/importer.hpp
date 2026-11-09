#ifndef GRAPE_VINE_IMPORTER_HPP
#define GRAPE_VINE_IMPORTER_HPP

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>

#include <filesystem>
#include <string>

#include "format.hpp"

namespace grape::vine {

struct ForeignInputs {
    Inputs m_inputs;
    uint64_t m_seed = 0;
    double m_tps = 240.0;
    bool m_seeded = false;
    std::string m_format;
    std::string m_warning;
};

geode::Result<ForeignInputs> loadForeign(const std::filesystem::path& path);

bool storeInputs(const std::filesystem::path& path, const Inputs& inputs);

geode::utils::file::FilePickOptions foreignPickOptions();

}

#endif
