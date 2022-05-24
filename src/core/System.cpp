// Include before header including the ghc forward declarations.
#define GHC_FILESYSTEM_IMPLEMENTATION
#include <ghc/filesystem.hpp>

#include "core/System.hpp"


void System::listAllFilesOfType(const fs::path& root, const std::string& ext, std::vector<fs::path>& paths){

	for (const fs::directory_entry& file : fs::recursive_directory_iterator(root)) {
		const fs::path& path = file.path();
		if(path.extension() == ext){
			paths.push_back(path);
		}
	}
}
