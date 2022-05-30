#include "core/AreaParser.hpp"
#include "core/Log.hpp"
#include "core/TextUtilities.hpp"

#include <unordered_map>
#include <sstream>

namespace Area {


glm::vec2 parseVec2(const char* val){
	if(val == nullptr || val[0] == '\0'){
		return glm::vec2(0.0f);
	}

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
	if(val == nullptr || val[0] == '\0'){
		return glm::vec3(0.0f);
	}

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

bool load(const fs::path& path, Obj& outObject, TexturesList& usedTextures ){

	pugi::xml_document areaFile;
	if(!areaFile.load_file(path.c_str())){
		Log::error("Unable to load area file at path %s", path.c_str());
		return false;
	}
	
	const auto& areaScene = areaFile.child("RwRf3").child("scene");
	const std::string areaName = path.filename().replace_extension().string();

	// Parse shaders first
	struct Shader {
		std::string content;
		std::string textureName;
		bool used = false;
	};
	std::unordered_map<std::string, Shader> shaders;

	const auto& shaderList = areaScene.child("shaderlist");
	for(const auto& shader : shaderList.children("shader")){
		if(!shader){
			continue;
		}

		const char* shaderName = shader.attribute("name").as_string();
		std::string shaderBaseName(shaderName);
		TextUtilities::replace(shaderBaseName, "-", "_");
		const std::string shaderFullName = areaName + "_" + shaderBaseName;
		Shader& shaderDesc = shaders[shaderFullName];

		std::stringstream mtlContent;
		mtlContent << "newmtl " << shaderFullName << "\n";
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
				texturePath = TextUtilities::trim(texturePath, "/");

				std::string textureName = texturePath;
				const std::string::size_type slashPos = texturePath.find_last_of("/");
				if(slashPos != std::string::npos){
					textureName = texturePath.substr(slashPos+1);
				}
				if(textureName.find("#") != std::string::npos){
					Log::warning("Skipping texture named %s for shader %s", textureName.c_str(), shaderName);
					textureName = "default_texture";
				}

				textureName = TextUtilities::lowercase(textureName);
				mtlContent << "map_Kd " << "textures/" << textureName << ".png\n";
				shaderDesc.textureName = textureName;

			}
		}

		shaderDesc.content = mtlContent.str();
	}


	ObjOffsets offsets;
	glm::mat4 areaFrame(1.0f);
	const auto axisNode = areaScene.find_child_by_attribute("param", "name", "axis system");
	if(axisNode){
		areaFrame = parseFrame(axisNode.child_value());
	}

	const auto& groups = areaScene.children("group");
	for(const auto& group : groups){
		const char* groupName = group.attribute("name").value();
		const auto frameNode = group.find_child_by_attribute("name", "localxform");
		glm::mat4 frame(1.0f);
		if(frameNode){
			frame = parseFrame(frameNode.child_value());
		}

		frame = areaFrame * frame;
		const glm::mat3 frameNormal = glm::transpose(glm::inverse(glm::mat3(frame)));

		// Skip some objects
		const auto userData = group.child("userdata").find_child_by_attribute("name", "3dsmax User Properties");
		if(userData){
			const char* userType = userData.child_value();
			// Also encountered "transparent"
			if(strcmp(userType, "\"decal\"") == 0){
				continue;
			} else if(strcmp(userType, "\"transparent\"") == 0){
				continue;
			} else if(strstr(userType, "\"portal") != nullptr){
				// Skip physics/sound/visibility portals
				continue;
			}
		}

		// In practice there is always one polymesh per group.
		{
			const auto polymesh = group.child("polymesh");
			const auto vertexList = polymesh.child("vertexlist");
			const uint32_t vCount = vertexList.attribute("count").as_int();

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

			uint32_t polymeshId = 0;
			for(const auto& primList : polymesh.children("primlist")){
				const size_t pCount = primList.attribute("count").as_int();
				const char* pShader = primList.attribute("shader").as_string();
				// If no shader, probably some physics collision data (or maybe a scene bouding shape with a color?)
				if(pShader[0] == '\0'){
					continue;
				}

				std::string shaderBaseName(pShader);
				TextUtilities::replace(shaderBaseName, "-", "_");
				const std::string shaderFullName = areaName + "_" + shaderBaseName;

				Obj::Set& set = outObject.faceSets.emplace_back();

				set.material = shaderFullName;
				set.name = areaName + "_" + groupName + std::to_string(polymeshId);
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
				++polymeshId;

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
		usedTextures.insert(shader.second.textureName);
	}
	return true;
}

}
