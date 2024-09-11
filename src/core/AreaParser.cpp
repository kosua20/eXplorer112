#include "core/AreaParser.hpp"
#include "core/Log.hpp"
#include "core/TextUtilities.hpp"

#include <unordered_map>
#include <sstream>

namespace Area {

bool parseBool(const char* val, bool fallback){
	if(val == nullptr || val[0] == '\0'){
		return fallback;
	}
	return strcmp(val, "true") == 0 || strcmp(val, "TRUE") == 0 || strcmp(val, "True") == 0 || strcmp(val, "1") == 0;
}

int parseInt(const char* val, int fallback){
	if(val == nullptr || val[0] == '\0'){
		return fallback;
	}
	std::string valStr(val);
	return std::stoi(val);
}

float parseFloat(const char* val, float fallback){
	if(val == nullptr || val[0] == '\0'){
		return fallback;
	}
	std::string valStr(val);
	return std::stof(val);
}

glm::vec2 parseVec2(const char* val, const glm::vec2& fallback){
	if(val == nullptr || val[0] == '\0'){
		return fallback;
	}

	std::string valStr(val);
	valStr = TextUtilities::trim(valStr, "()");
	const std::vector<std::string> tokens = TextUtilities::split(valStr, " ", true);
	if(tokens.size() < 2){
		Log::error("Unable to parse vec2.");
		return fallback;
	}
	return { std::stof(tokens[0]), std::stof(tokens[1])};
}

glm::vec3 parseVec3(const char* val, const glm::vec3& fallback){
	if(val == nullptr || val[0] == '\0'){
		return fallback;
	}

	std::string valStr(val);
	valStr = TextUtilities::trim(valStr, "()");
	const std::vector<std::string> tokens = TextUtilities::split(valStr, " ", true);
	if(tokens.size() < 3){
		// We have to be lenient for some parameters (RGB/RGBA colors)
		Log::error("Unable to parse vec3.");
		return fallback;
	}
	return { std::stof(tokens[0]), std::stof(tokens[1]), std::stof(tokens[2])};
}

glm::vec4 parseVec4(const char* val, const glm::vec4& fallback){
	if(val == nullptr || val[0] == '\0'){
		return fallback;
	}

	std::string valStr(val);
	valStr = TextUtilities::trim(valStr, "()");
	const std::vector<std::string> tokens = TextUtilities::split(valStr, " ", true);
	if(tokens.size() < 4){
		Log::error("Unable to parse vec4.");
		return fallback;
	}
	return { std::stof(tokens[0]), std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3])};
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

std::string retrieveTextureName(const pugi::xml_node& textureRef, const pugi::xml_node& shader){
	if(!textureRef){
		return "";
	}

	const char* textureRefName = textureRef.attribute("ref").value();
	const auto textureDec = shader.find_child_by_attribute("texture", "name", textureRefName);
	if(!textureDec){
		return "";
	}

	std::string texturePath = textureDec.attribute("sourcename").as_string();
	TextUtilities::replace(texturePath, "\\", "/");
	texturePath = TextUtilities::trim(texturePath, "/");

	std::string textureName = texturePath;
	const std::string::size_type slashPos = texturePath.find_last_of("/");
	if(slashPos != std::string::npos){
		textureName = texturePath.substr(slashPos+1);
	}
	if(textureName.find("#") != std::string::npos){
		Log::warning("Skipping texture named %s", textureName.c_str());
		return "";
	}
	return TextUtilities::lowercase(textureName);
}

bool load(const fs::path& path, Object& outObject){

	pugi::xml_document areaFile;
	if(!areaFile.load_file(path.c_str())){
		Log::error("Unable to load area file at path %s", path.string().c_str());
		return false;
	}
	
	const auto& areaScene = areaFile.child("RwRf3").child("scene");
	const std::string areaName = path.filename().replace_extension().string();

	// Parse shaders first
	struct Shader {
		Object::Material material;
		int index = -1;
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

		// Color
		{
			const auto textureRef = shader.child("shaderfunc").find_child_by_attribute("channel", "name", "color").child("texture");
			const std::string textureName = retrieveTextureName(textureRef, shader);
			shaderDesc.material.color = !textureName.empty() ? textureName : DEFAULT_ALBEDO_TEXTURE;
		}
		// Normal
		{
			const auto textureRef = shader.child("shaderfunc").find_child_by_attribute("channel", "name", "normal").child("texture");
			const std::string textureName = retrieveTextureName(textureRef, shader);
			shaderDesc.material.normal = !textureName.empty() ? textureName : DEFAULT_NORMAL_TEXTURE;
		}
	}

	ObjOffsets offsets;
	glm::mat4 areaFrame(1.0f);

	const auto axisNode = areaScene.find_child_by_attribute("param", "name", "axis system");
	if(axisNode){
		areaFrame = parseFrame(axisNode.child_value());
	}

	outObject.name = areaName + "_groups";

	const auto& groups = areaScene.children("group");
	for(const auto& group : groups){


		const auto frameNode = group.find_child_by_attribute("name", "localxform");
		glm::mat4 frame(1.0f);
		if(frameNode){
			frame = parseFrame(frameNode.child_value());
		}

		frame = areaFrame * frame;
		const glm::mat3 frameNormal = glm::transpose(glm::inverse(glm::mat3(frame)));

		// Skip some objects, determine type.
		Object::Material::Type materialType = Object::Material::OPAQUE;
		const auto userData = group.child("userdata").find_child_by_attribute("name", "3dsmax User Properties");
		if(userData){
			const char* userType = userData.child_value();
			if(strcmp(userType, "\"decal\"") == 0){
				materialType = Object::Material::DECAL;
			} else if(strcmp(userType, "\"transparent\"") == 0){
				materialType = Object::Material::TRANSPARENT;
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
					const glm::vec2 flipUV = parseVec2(tokens[tIndex].c_str());
					outObject.uvs.emplace_back(flipUV.x, 1.0f - flipUV.y);
				}
				if(nIndex >= 0){
					const glm::vec3 nor = glm::normalize(parseVec3(tokens[nIndex].c_str()));
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

				Shader& shader = shaders[shaderFullName];
				if(shader.index < 0){
					shader.index = ( int )outObject.materials.size();
					outObject.materials.push_back(shader.material);
					outObject.materials.back().type = materialType;
				}

				Object::Set& set = outObject.faceSets.emplace_back();
				set.material = shader.index;
				set.faces.reserve(pCount);

				for(const auto& p : primList.children("p")){
					const auto tokens = TextUtilities::split(p.child_value(), " ", true);
					if(tokens.size() != 3){
						Log::error("Unexpected primitive index count");
						continue;
					}
					Object::Set::Face& f = set.faces.emplace_back();
					const uint32_t v0 = std::stoul(tokens[0]);
					const uint32_t v1 = std::stoul(tokens[1]);
					const uint32_t v2 = std::stoul(tokens[2]);
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

	return true;
}

}
