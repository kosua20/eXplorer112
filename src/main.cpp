#include "core/Log.hpp"
#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/DFFParser.hpp"

glm::vec3 parseVec3(const char* val){
	std::string valStr(val);
	valStr = TextUtilities::trim(valStr, "()");
	const std::vector<std::string> tokens = TextUtilities::split(valStr, " ", true);
	if(tokens.size() != 3){
		return glm::vec3(0.0f);
	}
	return { std::stof(tokens[0]), std::stof(tokens[1]), std::stof(tokens[2])};
}

int main(int argc, const char** argv)
{
	if(argc < 2){
		return 1;
	}
	const fs::path rootPath(argv[1]);
	const fs::path modelsPath = rootPath / "models";
	const fs::path texturesPath = rootPath / "textures";
	const fs::path templatesPath = rootPath / "templates";

	const fs::path zonesPath = rootPath / "zones";
	const fs::path worldsPath = zonesPath / "world";

	std::vector<fs::path> modelsList;
	System::listAllFilesOfType(modelsPath, ".dff", modelsList);

	std::vector<fs::path> templatesList;
	System::listAllFilesOfType(templatesPath, ".template", templatesList);

	std::vector<fs::path> worldsList;
	System::listAllFilesOfType(worldsPath, ".world", worldsList);

	const fs::path worldPath = worldsPath / "2poop_int.world";
	Log::info("Processing world %s", worldPath.c_str());

	pugi::xml_document world;
	pugi::xml_parse_result result = world.load_file(worldPath.c_str());
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
			std::string objPathStrUp(objPathStr);
			TextUtilities::replace(objPathStrUp, "\\", "/");
			objPathStrUp = TextUtilities::lowercase(objPathStrUp);

			fs::path objPath = rootPath / objPathStrUp;

			Log::info("Actor: %s, pos: (%f %f %f), path %s, visible: %s", objName, position[0], position[1], position[2], objPath.c_str(), visible ? "yes" : "no");
			if(std::find(modelsList.begin(), modelsList.end(), objPath) != modelsList.end()){
				Log::info("Found");
			}

		} else if(strcmp(type, "DOOR") == 0){

		}
		// Ignore other types for now.
	}
	
	// texturesPath+modelPath many formats(dds,...)
	// zonesPath .rf3

	return 0;
}
