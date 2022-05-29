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

bool isEntityVisible(const pugi::xml_node& entity){
	const char* objVisibility = entity.find_child_by_attribute("name", "visible").child_value();
	const bool visible = !objVisibility || strcmp(objVisibility, "true") == 0 || strcmp(objVisibility, "1") == 0;
	return visible;
}

glm::mat4 getEntityFrame(const pugi::xml_node& entity){
	const char* objPosStr = entity.find_child_by_attribute("name", "position").child_value();
	const char* objRotStr = entity.find_child_by_attribute("name", "rotation").child_value();
	const glm::vec3 position = Area::parseVec3(objPosStr);
	const glm::vec3 rotAngles = Area::parseVec3(objRotStr) / 180.0f * (float)M_PI;

	glm::mat4 frame = glm::translate(glm::mat4(1.0f), position)
	* glm::rotate(glm::mat4(1.0f), rotAngles[2], glm::vec3(0.0f, 0.0f, 1.0f))
	* glm::rotate(glm::mat4(1.0f), rotAngles[1], glm::vec3(0.0f, 1.0f, 0.0f))
	* glm::rotate(glm::mat4(1.0f), rotAngles[0], glm::vec3(1.0f, 0.0f, 0.0f));

	return frame;
}

void processEntity(const pugi::xml_node& entity, const glm::mat4& globalFrame, bool templated, const std::vector<fs::path>& modelsList, const fs::path& inputPath,
				   std::map<fs::path, Obj>& objectsLibrary, TexturesList& usedTextures,
				   ObjOffsets& offsets, std::ofstream& outputObj,  std::ofstream& outputMtl){

	const auto typeNode = entity.find_child_by_attribute("name", "type");
	if(!typeNode){
		return;
	}

	const char* type = typeNode.first_child().value();
	if((strcmp(type, "ACTOR") != 0) && (strcmp(type, "DOOR") != 0) && (strcmp(type, "CREATURE") != 0)
	   && (strcmp(type, "LIGHT") != 0) && (strcmp(type, "CAMERA") != 0) ){
		return;
	}

	if(!isEntityVisible(entity)){
		return;
	}

	const char* objName = entity.find_child_by_attribute("name", "name").child_value();
	const char* objPathStr = entity.find_child_by_attribute("name", "sourceName").child_value();

	if(strcmp(type, "CAMERA") == 0){
		objPathStr = entity.find_child_by_attribute("name", "cameramodel").child_value();
		if(!objPathStr || objPathStr[0] == '\0'){
			objPathStr = entity.find_child_by_attribute("name", "cameraModel").child_value();
		}
		if(!objPathStr || objPathStr[0] == '\0'){
			objPathStr = "models\\objets\\cameras\\camera.dff";
		}
	}

	// Only keep elements linked with a model.
	if(!objPathStr || objPathStr[0] == '\0')
		return;

	// Application of the frame on templates is weird.
	// It seems the template frame takes priority. Maybe it's the delta from the template frame to the first sub-element frame that should be used on other elements?
	glm::mat4 frame = templated ? globalFrame : getEntityFrame(entity);

	// Special case for lights
	if(strcmp(type, "LIGHT") == 0){

		const char* mdlPosStr = entity.find_child_by_attribute("name", "modelPosition").child_value();
		const char* mdlRotStr = entity.find_child_by_attribute("name", "modelRotation").child_value();

		const glm::vec3 mdlPosition = Area::parseVec3(mdlPosStr);
		const glm::vec3 mdlRotAngles = Area::parseVec3(mdlRotStr) / 180.0f * (float)M_PI;
		const glm::mat4 mdlFrame =  glm::translate(glm::mat4(1.0f), mdlPosition)
			* glm::eulerAngleYXZ(mdlRotAngles[1], mdlRotAngles[0], mdlRotAngles[2]);

		frame = frame * mdlFrame;
	} else if(strcmp(type, "CAMERA") == 0){
		const char* cam2DRotStr = entity.find_child_by_attribute("name", "camerarotation").child_value();
		const glm::vec2 cam2DRot = Area::parseVec2(cam2DRotStr);
		// This is a wild guess.
		const glm::mat4 mdlFrame = glm::rotate(glm::mat4(1.0f), -cam2DRot[1], glm::vec3(0.0f, 1.0f, 0.0f));
		frame = frame * mdlFrame;
	}

	// Cleanup model path.
	std::string objPathStrUp(objPathStr);
	TextUtilities::replace(objPathStrUp, "\\", "/");
	objPathStrUp = TextUtilities::lowercase(objPathStrUp);

	fs::path objPath = inputPath / objPathStrUp;
	objPath.replace_extension("dff");
	const std::string modelName = objPath.filename().replace_extension();

	Log::info("Actor: %s", objName);
	//Log::info("Actor: %s, rot: (%f %f %f), model: %s, visible: %s", objName, rotAngles[0], rotAngles[1], rotAngles[2], modelName.c_str(), visible ? "yes" : "no");

	if(std::find(modelsList.begin(), modelsList.end(), objPath) == modelsList.end()){
		Log::error("Could not find model %s", modelName.c_str());
		return;
	}

	// If object not already loaded, load it.
	if(objectsLibrary.find(objPath) == objectsLibrary.end()){
		objectsLibrary[objPath] = Obj();

		Log::info("Retrieving model %s", modelName.c_str());

		if(!Dff::load(objPath, objectsLibrary[objPath], usedTextures)){
			return;
		}
		writeMtlToStream(objectsLibrary[objPath], outputMtl);

	}
	writeObjToStream(objectsLibrary[objPath], outputObj, offsets, frame);
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

	std::vector<fs::path> templatesList;
	System::listAllFilesOfType(templatesPath, ".template", templatesList);

	std::vector<fs::path> worldsList;
	System::listAllFilesOfType(worldsPath, ".world", worldsList);

	std::vector<fs::path> texturesList;
	System::listAllFilesOfType(modelsPath, ".dds", texturesList);
	System::listAllFilesOfType(modelsPath, ".tga", texturesList);
	System::listAllFilesOfType(texturesPath, ".dds", texturesList);
	System::listAllFilesOfType(texturesPath, ".tga", texturesList);


	//for(const auto& worldPath : worldsList)
	{
		const fs::path worldPath = worldsPath / "1poop_int.world";
		//const fs::path worldPath = worldsPath / "tutoeco.world";
		//const fs::path worldPath = worldsPath / "zt.world";
		Log::info("Processing world %s", worldPath.c_str());
		std::map<fs::path, Obj> objectsLibrary;

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
		if(!world.load_file(worldPath.c_str())){
			Log::error("Unable to load template file at path %s", worldPath.c_str());
			//continue;
			return 1;
		}

		const auto& entities = world.child("World").child("scene").child("entities");

		for(const auto& entity : entities.children("entity")){
			processEntity(entity, glm::mat4(1.0f), false, modelsList, inputPath, objectsLibrary, usedTextures, offsets, outputObj, outputMtl );

			// TODO: Interesting types to investigate: FX?
		}

		for(const auto& instance : entities.children("instance")){
			if(!isEntityVisible(instance)){
				continue;
			}

			const glm::mat4 frame = getEntityFrame(instance);

			std::string xmlFile = instance.find_child_by_attribute("name", "template").child_value();
			TextUtilities::replace(xmlFile, "\\", "/");
			const fs::path xmlPath = inputPath / xmlFile;

			pugi::xml_document templateDef;
			if(!templateDef.load_file(xmlPath.c_str())){
				Log::error("Unable to load template file at path %s", xmlPath.c_str());
			}

			const auto& entities = templateDef.child("template").child("entities");
			for(const auto& entity : entities.children("entity")){
				processEntity(entity, frame, true, modelsList, inputPath, objectsLibrary, usedTextures, offsets, outputObj, outputMtl );
			}
		}

		const auto& areas = world.child("World").child("scene").child("areas");

		for(const auto& area : areas.children()){

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
			if(textureName.empty()){
				continue;
			}
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
			if(!fs::exists(destinationPath)){
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

	}

	// texturesPath+modelPath many formats(dds,...)
	// zonesPath .rf3

	return 0;

}
