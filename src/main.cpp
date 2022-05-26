#include "core/Log.hpp"
#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/DFFParser.hpp"

#include <fstream>
#include <sstream>

glm::vec3 parseVec3(const char* val){
	std::string valStr(val);
	valStr = TextUtilities::trim(valStr, "()");
	const std::vector<std::string> tokens = TextUtilities::split(valStr, " ", true);
	if(tokens.size() != 3){
		return glm::vec3(0.0f);
	}
	return { std::stof(tokens[0]), std::stof(tokens[1]), std::stof(tokens[2])};
}

bool loadDFF(const fs::path& path, Obj& outObject){

	Dff::Model model;
	if(!Dff::parse(path, model)){
		Log::error("Failed to parse.");
		return false;
	}

	const std::string itemName = path.filename().replace_extension().string();
	Dff::convertToObj(model, outObject, itemName);

	return true;
}

int main(int argc, const char** argv)
{
	if(argc < 3){
		return 1;
	}

	const fs::path inputPath(argv[1]);
	const fs::path outputPath = fs::path(argv[2]);

	const fs::path modelsPath = inputPath / "models";
	const fs::path texturesPath = inputPath / "textures";
	const fs::path templatesPath = inputPath / "templates";

	const fs::path zonesPath = inputPath / "zones";
	const fs::path worldsPath = zonesPath / "world";

	std::vector<fs::path> modelsList;
	System::listAllFilesOfType(modelsPath, ".dff", modelsList);

	for(const fs::path& modelPath : modelsList){

		Obj outObject;
		if(!loadDFF(modelPath, outObject)){
			continue;
		}
		// Save obj file
		const std::string baseName = modelPath.filename().replace_extension();
		const std::string outPath = outputPath / baseName;

		std::ofstream outputMtl(outPath + ".mtl");
		std::ofstream outputObj(outPath + ".obj");
		outputObj << "mtllib " << baseName << ".mtl" << "\n";

		writeObjToStreams(outObject, outputObj, outputMtl);

		outputObj.close();
		outputMtl.close();

	}
	return 0;

	std::vector<fs::path> templatesList;
	System::listAllFilesOfType(templatesPath, ".template", templatesList);

	std::vector<fs::path> worldsList;
	System::listAllFilesOfType(worldsPath, ".world", worldsList);

	const fs::path worldPath = worldsPath / "2poop_int.world";
	Log::info("Processing world %s", worldPath.c_str());

	pugi::xml_document world;
	world.load_file(worldPath.c_str());
	const auto& entities = world.child("World").child("scene").child("entities");
	for(const auto& entity : entities.children()){
		const auto typeNode = entity.find_child_by_attribute("name", "type");
		if(!typeNode){
			continue;
		}
		const char* type = typeNode.first_child().value();
		if(strcmp(type, "ACTOR") == 0){

			const char* objName = entity.find_child_by_attribute("name", "name").first_child().value();
			const char* objPosStr = entity.find_child_by_attribute("name", "position").first_child().value();
			const char* objRotStr = entity.find_child_by_attribute("name", "rotation").first_child().value();
			const char* objPathStr = entity.find_child_by_attribute("name", "sourceName").first_child().value();
			const char* objVisibility = entity.find_child_by_attribute("name", "visible").first_child().value();

			glm::vec3 position = parseVec3(objPosStr);
			glm::vec3 rotAngles = parseVec3(objRotStr);
			const bool visible = !objVisibility || strcmp(objVisibility, "true") == 0;
			// Cleanup model path.
			std::string objPathStrUp(objPathStr);
			TextUtilities::replace(objPathStrUp, "\\", "/");
			objPathStrUp = TextUtilities::lowercase(objPathStrUp);

			fs::path objPath = inputPath / objPathStrUp;

			Log::info("Actor: %s, pos: (%f %f %f), path %s, visible: %s", objName, position[0], position[1], position[2], objPath.c_str(), visible ? "yes" : "no");
			if(std::find(modelsList.begin(), modelsList.end(), objPath) == modelsList.end()){
				Log::error("Not found");
			}

		} else if(strcmp(type, "DOOR") == 0){

		}
		// Ignore other types for now.
	}
	
	// texturesPath+modelPath many formats(dds,...)
	// zonesPath .rf3

	return 0;
}
