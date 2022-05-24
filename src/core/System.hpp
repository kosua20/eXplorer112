#pragma once

#define GHC_FILESYSTEM_FWD
#include <ghc/filesystem.hpp>

namespace fs = ghc::filesystem;

#include <pugixml/pugixml.hpp>

namespace System {

void listAllFilesOfType(const fs::path& root, const std::string& ext, std::vector<fs::path>& paths);

}
