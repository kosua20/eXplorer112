// Include before header including the ghc forward declarations.
#define GHC_FILESYSTEM_IMPLEMENTATION
#include <ghc/filesystem.hpp>

#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/Log.hpp"

#include <sstream>

void System::listAllFilesOfType(const fs::path& root, const std::string& ext, std::vector<fs::path>& paths){

	for (const fs::directory_entry& file : fs::recursive_directory_iterator(root)) {
		const fs::path& path = file.path();
		if(path.extension() == ext){
			paths.push_back(path);
		}
	}
}


char * System::loadData(const fs::path& path, size_t & size) {

	std::ifstream inputFile(path, std::ios::binary | std::ios::ate);
	if(inputFile.bad() || inputFile.fail()) {
		Log::warning("Resources: Unable to load file at path %s.", path.c_str());
		size = 0;
		return nullptr;
	}
	const std::ifstream::pos_type fileSize = inputFile.tellg();
	char * rawContent					   = new char[fileSize];
	inputFile.seekg(0, std::ios::beg);
	inputFile.read(&rawContent[0], fileSize);
	inputFile.close();
	size = fileSize;
	return rawContent;
}

std::string System::loadString(const fs::path & path) {
	std::ifstream inputFile(path);
	if(inputFile.bad() || inputFile.fail()) {
		Log::warning("Resources: Unable to load file at path %s.", path.c_str());
		return "";
	}
	std::stringstream buffer;
	// Read the stream in a buffer.
	buffer << inputFile.rdbuf();
	inputFile.close();
	// Create a string based on the content of the buffer.
	std::string line = buffer.str();
	return line;
}

void System::saveData(const fs::path & path, char * rawContent, size_t size) {
	std::ofstream outputFile(path, std::ios::binary);

	if(!outputFile.is_open()) {
		Log::warning("Resources: Unable to save file at path %s.", path.c_str());
		return;
	}
	outputFile.write(rawContent, size);
	outputFile.close();
}

void System::saveString(const fs::path & path, const std::string & content) {
	std::ofstream outputFile(path);
	if(outputFile.bad() || outputFile.fail()) {
		Log::warning("Resources: Unable to load file at path %s.", path.c_str());
		return;
	}
	outputFile << content;
	outputFile.close();
}
