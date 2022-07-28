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


std::string System::getStringWithIncludes(const fs::path & filename, std::vector<fs::path>& names){

	// Special case: if names is empty, we are at the root and no special name was specified, add the filename.
	if(names.empty()) {
		names.push_back(filename);
	}

	// Reset line count for the current file.
	const std::string currentLoc = std::to_string(names.size() - 1);
	std::string newStr = "#line 1 " + currentLoc + "\n";

	const auto lines = TextUtilities::splitLines(loadString(filename), false);
	// Check if some lines are include.
	for(size_t lid = 0; lid < lines.size(); ++lid){
		const std::string & line = lines[lid];
		const std::string::size_type pos = line.find("#include");
		if(pos == std::string::npos){
			newStr.append(line);
			newStr.append("\n");
			continue;
		}
		const std::string::size_type bpos = line.find('"', pos);
		const std::string::size_type epos = line.find('"', bpos+1);
		if(bpos == std::string::npos || epos == std::string::npos){
			Log::warning( "Misformed include at line %llu of %s, empty line.", (unsigned long)lid , filename.c_str());
			newStr.append("\n");
			continue;
		}
		// Extract the file path.
		const fs::path subPath = line.substr(bpos + 1, epos - (bpos + 1));

		// If the file has already been included, skip it.
		if(std::find(names.begin(), names.end(), subPath) != names.end()){
			newStr.append("\n");
			continue;
		}

		names.push_back(subPath);
		// Insert the content.
		const std::string content = System::getStringWithIncludes(filename.parent_path() / subPath, names);
		newStr.append(content);
		newStr.append("\n");
		// And reset to where we were before in the current file.
		newStr.append("#line " + std::to_string(lid+2) + " " + currentLoc + "\n");

	}
	return newStr;
}
