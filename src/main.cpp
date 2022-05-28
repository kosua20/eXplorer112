#include "core/Log.hpp"
#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/DFFParser.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <fstream>
#include <sstream>
#include <map>
#include <unordered_map>

glm::vec2 parseVec2(const char* val){
	std::string valStr(val);
	valStr = TextUtilities::trim(valStr, "()");
	const std::vector<std::string> tokens = TextUtilities::split(valStr, " ", true);
	if(tokens.size() != 2){
		Log::error("Unable to parse vec2.");
		return glm::vec2(0.0f);
	}
	return { std::stof(tokens[0]), std::stof(tokens[1])};
}

glm::vec3 parseVec3(const char* val){
	std::string valStr(val);
	valStr = TextUtilities::trim(valStr, "()");
	const std::vector<std::string> tokens = TextUtilities::split(valStr, " ", true);
	if(tokens.size() != 3){
		Log::error("Unable to parse vec3.");
		return glm::vec3(0.0f);
	}
	return { std::stof(tokens[0]), std::stof(tokens[1]), std::stof(tokens[2])};
}

glm::mat4 parseFrame(const char* val){
	if(val == nullptr || val[0] == '\0'){
		return glm::mat4(1.0f);
	}
	std::string valStr(val);
	valStr = TextUtilities::trim(valStr, "()");
	TextUtilities::replace(valStr, ")(", ";");
	const auto rows = TextUtilities::split(valStr, ";", true);
	if(rows.size() != 4){
		Log::error("Unable to parse frame.");
		return glm::mat4(1.0f);
	}
	glm::mat4 res(1.0f);

	for(int i = 0; i < 4; ++i){
		const auto coeffs = TextUtilities::split(rows[i], " ", true);
		if(coeffs.size() != 3){
			Log::error("Unable to parse frame.");
			return glm::mat4(1.0f);
		}
		for(int j = 0; j < 3; ++j){
			res[i][j] = std::stof(coeffs[j]);
		}
	}
	return res;
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

void loadArea(const fs::path& path, Obj& outObject){

	pugi::xml_document areaFile;
	areaFile.load_file(path.c_str());
	const auto& areaScene = areaFile.child("RwRf3").child("scene");

	// Parse shaders first
	struct Shader {
		std::string content;
		bool used = false;
	};
	std::unordered_map<std::string, Shader> shaders;

	const auto& shaderList = areaScene.child("shaderlist");
	for(const auto& shader : shaderList.children("shader")){
		if(!shader){
			continue;
		}
		std::stringstream mtlContent;
		const char* shaderName = shader.attribute("name").as_string();
		
		mtlContent << "newmtl " << shaderName << "\n";
		mtlContent << "Ka " << 0.f << " " << 0.f << " " << 0.f << "\n";
		mtlContent << "Kd " << 1.0f << " " << 1.0f << " " << 1.0f << "\n";
		mtlContent << "Ks " << 1.0f << " " << 1.0f << " " << 1.0f << "\n";
		mtlContent << "Ns " << 100 << "\n";

		const auto textureRef = shader.child("shaderfunc").find_child_by_attribute("channel", "name", "color").child("texture");
		if(textureRef){
			const char* textureRefName = textureRef.attribute("ref").value();
			const auto textureDec = shader.find_child_by_attribute("texture", "name", textureRefName);
			if(textureDec){
				std::string texturePath = textureDec.attribute("sourcename").as_string();
				TextUtilities::replace(texturePath, "\\", "/");
				std::string textureName = texturePath;
				const std::string::size_type slashPos = texturePath.find_last_of("/");
				if(slashPos != std::string::npos){
					textureName = texturePath.substr(slashPos+1);
				}
				if(textureName.find("#") != std::string::npos){
					Log::warning("Skipping texture named %s for shader %s", textureName.c_str(), shaderName);
					continue;
				}
				mtlContent << "map_Kd " << "textures/" << textureName << "\n";
			}
		}

		shaders[std::string(shaderName)].content = mtlContent.str();
	}


	ObjOffsets offsets;
	glm::mat4 areaFrame(1.0f);
	const auto axisNode = areaScene.find_child_by_attribute("param", "name", "axis system");
	if(axisNode){
		areaFrame = parseFrame(axisNode.child_value());
	}

	const auto& groups = areaScene.children("group");
	for(const auto& group : groups){
		const auto frameNode = group.find_child_by_attribute("name", "localxform");
		glm::mat4 frame(1.0f);
		if(frameNode){
			frame = parseFrame(frameNode.child_value());
		}

		frame = areaFrame * frame;

		const glm::mat3 frameNormal = glm::transpose(glm::inverse(glm::mat3(frame)));


		// In practice there is always one polymesh per group.
		{
			const auto polymesh = group.child("polymesh");
			const auto vertexList = polymesh.child("vertexlist");
			const size_t vCount = vertexList.attribute("count").as_int();

			const auto format = vertexList.child("format");
			int vIndex = -1;
			int nIndex = -1;
			int tIndex = -1;
			int count = 0;
			for(const auto& param : format.children()){
				const char* name = param.attribute("name").value();
				if(strcmp(name, "position") == 0){
					vIndex = count;
				} else if(strcmp(name, "normal") == 0){
					nIndex = count;
				} else if(strcmp(name, "uv0") == 0){
					tIndex = count;
				}
				++count;
			}
			if(tIndex < 0){
				continue;
			}


			for(const auto& v : vertexList.children("v")){
				// Assume index at the beginning.
				// Split on opening parenthesis
				const auto tokens = TextUtilities::split(v.child_value(), "(", true);
				if(tokens.size() != (size_t)count){
					Log::error("Unexpected token count");
					continue;
				}
				//indices.push_back(std::stoul(tokens[0]));
				assert(vIndex >= 0);

				const glm::vec3 pos = parseVec3(tokens[vIndex].c_str());
				outObject.positions.push_back(glm::vec3(frame * glm::vec4(pos, 1.0f)));

				if(tIndex >= 0){
					outObject.uvs.push_back(parseVec2(tokens[tIndex].c_str()));
				}
				if(nIndex >= 0){
					const glm::vec3 nor = parseVec3(tokens[nIndex].c_str());
					outObject.normals.push_back(glm::normalize(frameNormal * nor));
				}
			}


			for(const auto& primList : polymesh.children("primlist")){
				const size_t pCount = primList.attribute("count").as_int();
				const char* pShader = primList.attribute("shader").as_string();


				Obj::Set& set = outObject.faceSets.emplace_back();
				set.material = std::string(pShader);
				set.faces.reserve(pCount);

				shaders[set.material].used = true;

				for(const auto& p : primList.children("p")){
					const auto tokens = TextUtilities::split(p.child_value(), " ", true);
					if(tokens.size() != 3){
						Log::error("Unexpected primitive index count");
						continue;
					}
					Obj::Set::Face& f = set.faces.emplace_back();
					const uint32_t v0 = std::stoul(tokens[0]) + 1u;
					const uint32_t v1 = std::stoul(tokens[1]) + 1u;
					const uint32_t v2 = std::stoul(tokens[2]) + 1u;
					f.v0 = v0 + offsets.v;
					f.v1 = v1 + offsets.v;
					f.v2 = v2 + offsets.v;

					if(tIndex >= 0){
						f.t0 = v0 + offsets.t;
						f.t1 = v1 + offsets.t;
						f.t2 = v2 + offsets.t;
					}
					if(nIndex >= 0){
						f.n0 = v0 + offsets.n;
						f.n1 = v1 + offsets.n;
						f.n2 = v2 + offsets.n;
					}

				}

			}

			offsets.v += vCount;
			offsets.n += (nIndex >= 0 ? vCount : 0);
			offsets.t += (tIndex >= 0 ? vCount : 0);
		}

	}

	// Only output used materials (some unused have erroneous texture names).
	for(const auto& shader : shaders){
		if(!shader.second.used){
			continue;
		}
		outObject.materials.append(shader.second.content);
		outObject.materials.append("\n");
	}
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


	std::map<fs::path, Obj> objectsLibrary;

	std::vector<fs::path> templatesList;
	System::listAllFilesOfType(templatesPath, ".template", templatesList);

	std::vector<fs::path> worldsList;
	System::listAllFilesOfType(worldsPath, ".world", worldsList);

	//for(const auto& worldPath : worldsList)
	{
		const fs::path worldPath = worldsPath / "1poop_int.world";
		Log::info("Processing world %s", worldPath.c_str());

		// Save obj file
		const std::string baseName = worldPath.filename().replace_extension();
		const std::string outPath = outputPath / baseName;

		std::ofstream outputMtl(outPath + ".mtl");
		std::ofstream outputObj(outPath + ".obj");
		outputObj << "mtllib " << baseName << ".mtl" << "\n";

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

				glm::vec3 position = parseVec3(objPosStr);
				glm::vec3 rotAngles = parseVec3(objRotStr) / 180.0f * (float)M_PI;
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

					if(!loadDFF(objPath, objectsLibrary[objPath])){
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
			loadArea(areaPath, areaObj);
			writeMtlToStream(areaObj, outputMtl);
			writeObjToStream(areaObj, outputObj, offsets, glm::mat4(1.0f));

		}


		outputObj.close();
		outputMtl.close();
	}

	// texturesPath+modelPath many formats(dds,...)
	// zonesPath .rf3

	return 0;

}
