#include "core/Log.hpp"
#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/Texture.hpp"
#include "core/DFFParser.hpp"
#include "core/AreaParser.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <fstream>
#include <map>

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

	std::vector<fs::path> templatesList;
	System::listAllFilesOfType(templatesPath, ".template", templatesList);

	std::vector<fs::path> worldsList;
	System::listAllFilesOfType(worldsPath, ".world", worldsList);

	std::vector<fs::path> texturesList;
	System::listAllFilesOfType(modelsPath, ".dds", texturesList);
	System::listAllFilesOfType(modelsPath, ".tga", texturesList);
	System::listAllFilesOfType(texturesPath, ".dds", texturesList);
	System::listAllFilesOfType(texturesPath, ".tga", texturesList);

	std::map<fs::path, Obj> objectsLibrary;

	for(const auto& worldPath : worldsList)
	{
		//const fs::path worldPath = worldsPath / "1poop_int.world";
		Log::info("Processing world %s", worldPath.c_str());

		// Save obj file
		const std::string baseName = worldPath.filename().replace_extension();
		const fs::path outPath = outputPath / baseName;
		const fs::path outTexturePath = outPath / "textures";

		fs::create_directory(outPath);
		fs::create_directory(outTexturePath);

		std::ofstream outputMtl(outPath / (baseName + ".mtl"));
		std::ofstream outputObj(outPath / (baseName + ".obj"));
		outputObj << "mtllib " << baseName << ".mtl" << "\n";

		TexturesList usedTextures;
		ObjOffsets offsets;

		pugi::xml_document world;
		world.load_file(worldPath.c_str());
		const auto& entities = world.child("World").child("scene").child("entities");

		for(const auto& entity : entities.children()){
			const auto typeNode = entity.find_child_by_attribute("name", "type");
			if(!typeNode){
				continue;
			}

			const char* type = typeNode.first_child().value();
			if((strcmp(type, "ACTOR") == 0) || (strcmp(type, "DOOR") == 0)){

				const char* objName = entity.find_child_by_attribute("name", "name").first_child().value();
				const char* objPosStr = entity.find_child_by_attribute("name", "position").first_child().value();
				const char* objRotStr = entity.find_child_by_attribute("name", "rotation").first_child().value();
				const char* objPathStr = entity.find_child_by_attribute("name", "sourceName").first_child().value();
				const char* objVisibility = entity.find_child_by_attribute("name", "visible").first_child().value();

				glm::vec3 position = Area::parseVec3(objPosStr);
				glm::vec3 rotAngles = Area::parseVec3(objRotStr) / 180.0f * (float)M_PI;
				const bool visible = !objVisibility || strcmp(objVisibility, "true") == 0;

				if(!visible)
					continue;

				const glm::mat4 frame =  glm::translate(glm::mat4(1.0f), position)
					* glm::eulerAngleYXZ(rotAngles[1], rotAngles[0], rotAngles[2]);

				// Cleanup model path.
				std::string objPathStrUp(objPathStr);
				TextUtilities::replace(objPathStrUp, "\\", "/");
				objPathStrUp = TextUtilities::lowercase(objPathStrUp);

				const fs::path objPath = inputPath / objPathStrUp;
				const std::string modelName = objPath.filename().replace_extension();

				Log::info("Actor: %s", objName);
				//Log::info("Actor: %s, rot: (%f %f %f), model: %s, visible: %s", objName, rotAngles[0], rotAngles[1], rotAngles[2], modelName.c_str(), visible ? "yes" : "no");

				if(std::find(modelsList.begin(), modelsList.end(), objPath) == modelsList.end()){
					Log::error("Could not find model %s", modelName.c_str());
					continue;
				}

				// If object not already loaded, load it.
				if(objectsLibrary.find(objPath) == objectsLibrary.end()){
					objectsLibrary[objPath] = Obj();

					Log::info("Retrieving model %s", modelName.c_str());

					if(!Dff::load(objPath, objectsLibrary[objPath], usedTextures)){
						continue;
					}
					writeMtlToStream(objectsLibrary[objPath], outputMtl);

				}
				writeObjToStream(objectsLibrary[objPath], outputObj, offsets, frame);

			}
			// Ignore other types for now.
		}

		const auto& areas = world.child("World").child("scene").child("areas");

		int i = 0;
		for(const auto& area : areas.children()){

			i++;
			const char* areaPathStr = area.attribute("sourceName").value();
			// Cleanup model path.
			std::string areaPathStrUp(areaPathStr);
			TextUtilities::replace(areaPathStrUp, "\\", "/");
			areaPathStrUp = TextUtilities::lowercase(areaPathStrUp);

			const fs::path areaPath = inputPath / areaPathStrUp;
			const std::string areaName = areaPath.filename().replace_extension().string();
			Log::info("Area: %s", areaName.c_str());

			Obj areaObj;
			if(!Area::load(areaPath, areaObj, usedTextures)){
				continue;
			}
			writeMtlToStream(areaObj, outputMtl);
			writeObjToStream(areaObj, outputObj, offsets, glm::mat4(1.0f));

		}


		outputObj.close();
		outputMtl.close();


		// Try to find each texture.
		for(const std::string& textureName : usedTextures){

			bool found = false;
			fs::path selectedTexturePath;

			for(const fs::path& texturePath : texturesList){
				const std::string existingName = texturePath.filename().replace_extension().string();
				if(existingName == textureName){
					if(found){
						Log::warning("Conflict for texture %s, paths: %s and %s", textureName.c_str(), selectedTexturePath.c_str(), texturePath.c_str());
					} else {
						found = true;
						selectedTexturePath = texturePath;
					}
				}
			}

			const fs::path destinationPath = outTexturePath / (textureName + ".png");
			if(found){
				// Copy the file.
				if(selectedTexturePath.extension() == ".tga"){
					convertTGAtoPNG(selectedTexturePath, destinationPath);
				} else if(selectedTexturePath.extension() == ".dds"){
					convertDDStoPNG(selectedTexturePath, destinationPath);
				} else {
					Log::error("Unsupported texture format for file %s", selectedTexturePath.filename().c_str());
				}

			} else {
				// Generate a dummy texture.
				writeDefaultTexture(destinationPath);
			}

		}

	}

	// texturesPath+modelPath many formats(dds,...)
	// zonesPath .rf3

	return 0;

}
