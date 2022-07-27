#pragma once

#define GHC_FILESYSTEM_FWD
#include <ghc/filesystem.hpp>

namespace fs = ghc::filesystem;

#include <pugixml/pugixml.hpp>
#include <string>

namespace System {

	void listAllFilesOfType(const fs::path& root, const std::string& ext, std::vector<fs::path>& paths);

	char * loadData(const fs::path& path, size_t & size);

	std::string loadString(const fs::path & path);

	void saveData(const fs::path & path, char * rawContent, size_t size);

	void saveString(const fs::path & path, const std::string & content);

}
