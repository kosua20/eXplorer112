#include "core/AreaParser.hpp"
#include "core/Log.hpp"
#include "core/TextUtilities.hpp"

#include <unordered_map>
#include <set>
#include <deque>

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

// e for exponent should not be first/last character.
// f at the end of a float has no extra meaning
static const std::string kTrimVecStr = "()abcdefghijklmnopqrstuvwxyz_ABCDEFGHIJKLMNOPQRSTUVWXYZ[]{}";

glm::vec2 parseVec2(const char* val, const glm::vec2& fallback){
	if(val == nullptr || val[0] == '\0'){
		return fallback;
	}

	std::string valStr(val);
	valStr = TextUtilities::trim(valStr, kTrimVecStr);
	const std::vector<std::string> tokens = TextUtilities::split(valStr, " ", true);
	const uint tokenCount = glm::min(2u, (uint)tokens.size());
	if(tokenCount < 2u){
		Log::warning("Unable to fully parse vec2: %s", valStr.c_str());
	}
	glm::vec2 res = fallback;
	for(uint i = 0; i < tokenCount; ++i){
		res[i] = std::stof(tokens[i]);
	}
	return res;
}

glm::vec3 parseVec3(const char* val, const glm::vec3& fallback){
	if(val == nullptr || val[0] == '\0'){
		return fallback;
	}

	std::string valStr(val);
	valStr = TextUtilities::trim(valStr, kTrimVecStr);
	const std::vector<std::string> tokens = TextUtilities::split(valStr, " ", true);
	const uint tokenCount = glm::min(3u, (uint)tokens.size());
	if(tokenCount < 3u){
		Log::warning("Unable to fully parse vec3: %s", valStr.c_str());
	}
	glm::vec3 res = fallback;
	for(uint i = 0; i < tokenCount; ++i){
		res[i] = std::stof(tokens[i]);
	}
	return res;
}

glm::vec4 parseVec4(const char* val, const glm::vec4& fallback){
	if(val == nullptr || val[0] == '\0'){
		return fallback;
	}

	std::string valStr(val);
	valStr = TextUtilities::trim(valStr, kTrimVecStr);
	const std::vector<std::string> tokens = TextUtilities::split(valStr, " ", true);

	const uint tokenCount = glm::min(4u, (uint)tokens.size());
	if(tokenCount < 4u){
		Log::warning("Unable to fully parse vec4: %s", valStr.c_str());
	}
	glm::vec4 res = fallback;
	for(uint i = 0; i < tokenCount; ++i){
		res[i] = std::stof(tokens[i]);
	}
	return res;
}

glm::mat4 parseFrame(const char* val){
	if(val == nullptr || val[0] == '\0'){
		return glm::mat4(1.0f);
	}
	std::string valStr(val);
	valStr = TextUtilities::trim(valStr, kTrimVecStr);
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
		// Check if the material has at least one texture, else ignore it (shadow mesh or bounds).
		const auto textureRef = shader.child("shaderfunc").find_child_by_attribute("channel", "name", "color").child("texture");
		std::string textureName = retrieveTextureName(textureRef, shader);
		if(textureName.empty())
			continue;

		Shader& shaderDesc = shaders[shaderFullName];
		// Color
		{
			shaderDesc.material.color = !textureName.empty() ? textureName : DEFAULT_ALBEDO_TEXTURE;
		}
		// Normal
		{
			const auto textureRefN = shader.child("shaderfunc").find_child_by_attribute("channel", "name", "normal").child("texture");
			const std::string textureNameN = retrieveTextureName(textureRefN, shader);
			shaderDesc.material.normal = !textureNameN.empty() ? textureNameN : DEFAULT_NORMAL_TEXTURE;
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
			} // "bounds" (see t16)
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
				auto shadIt = shaders.find(shaderFullName);
				// Skip missing (non-textured materials).
				if(shadIt == shaders.end()){
					continue;
				}
				Shader& shader = shadIt->second;
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

	// Split transparent sets in connected components, to improve sorting when rendering
	for(uint setId = 0; setId < outObject.faceSets.size(); ){
		const Object::Set& refSet = outObject.faceSets[ setId ];
		// Skip non transparent sets.
		if( refSet.material == Object::Material::NO_MATERIAL || refSet.faces.empty() 
			|| outObject.materials[ refSet.material ].type != Object::Material::TRANSPARENT ){
			++setId;
			continue;
		}
		// Copy the set
		const Object::Set set( refSet );
		// Remove it from the object.
		outObject.faceSets.erase(outObject.faceSets.begin() + setId);

		// Remap vertex indices based on position.
		std::unordered_map<uint, uint> oldToNewMapping;
		{
			// Collect vertices used in the set
			std::unordered_set<uint> oldIndicesSet;
			for( const auto& face : set.faces )
			{
				oldIndicesSet.emplace( face.v0 );
				oldIndicesSet.emplace( face.v1 );
				oldIndicesSet.emplace( face.v2 );
			}
			// For easier iteration
			const std::vector<uint> oldIndices( oldIndicesSet.begin(), oldIndicesSet.end() );
			// Here we care about closeness in space, merge vertices if positions are close.
			const uint oldCount = (uint)oldIndices.size();
			constexpr float posEpsilon = 1e-3f; // World in centimeters.
			for( uint vid = 0; vid < oldCount; ++vid )
			{
				oldToNewMapping[ oldIndices[ vid ] ] = oldIndices[ vid ];
				// Look for a predecessor with the same position.
				for( uint ovid = 0; ovid < vid; ++ovid )
				{
					if( glm::length( outObject.positions[ oldIndices[ vid ] ] - outObject.positions[ oldIndices[ ovid ] ] ) < posEpsilon )
					{
						oldToNewMapping[ oldIndices[ vid ] ] = oldToNewMapping[ oldIndices[ ovid ] ];
						break;
					}
				}
			}
		}
		// We'll now work with the remapped indices.
		
		// Build neighbor lists.
		struct VertexInfos
		{
			std::set<uint> neighborsNewIds;
			int component{ -1 };
		};
		std::unordered_map<uint, VertexInfos> newVerticesToInfos;
		for( const auto& face : set.faces )
		{
			const uint v0NewMapping = oldToNewMapping[ face.v0 ];
			const uint v1NewMapping = oldToNewMapping[ face.v1 ];
			const uint v2NewMapping = oldToNewMapping[ face.v2 ];
			newVerticesToInfos[ v0NewMapping ].neighborsNewIds.insert( v1NewMapping );
			newVerticesToInfos[ v0NewMapping ].neighborsNewIds.insert( v2NewMapping );
			newVerticesToInfos[ v1NewMapping ].neighborsNewIds.insert( v0NewMapping );
			newVerticesToInfos[ v1NewMapping ].neighborsNewIds.insert( v2NewMapping );
			newVerticesToInfos[ v2NewMapping ].neighborsNewIds.insert( v0NewMapping );
			newVerticesToInfos[ v2NewMapping ].neighborsNewIds.insert( v1NewMapping );
		}

		// Assign component index to each vertex, based on connectivity. Explore faces in a depth first search.
		uint currentSetCount = 1;
		{
			std::deque<uint> newVertsToVisit;
			// Init
			{
				auto firstVert = newVerticesToInfos.begin();
				newVertsToVisit.push_back( firstVert->first );
				firstVert->second.component = 0;
			}
			uint newAssignedCount = 1;
			const uint newVertexCount = ( uint )newVerticesToInfos.size();
			while( newAssignedCount < newVertexCount )
			{
				// Visit all neighbors while we have some.
				while( !newVertsToVisit.empty() )
				{
					uint newVertexId = newVertsToVisit.front();
					newVertsToVisit.pop_front();
					const VertexInfos& newVertInfo = newVerticesToInfos[ newVertexId ];
					for( const uint& newNeighborId : newVertInfo.neighborsNewIds )
					{
						if( newVerticesToInfos[ newNeighborId ].component == -1 )
						{
							newVerticesToInfos[ newNeighborId ].component = newVertInfo.component;
							newVertsToVisit.push_back( newNeighborId );
							++newAssignedCount;
						}
					}
				}
				// Find the next free vertex.
				for( auto& newVertexInfos : newVerticesToInfos )
				{
					if( newVertexInfos.second.component == -1 )
					{
						newVertsToVisit.push_back( newVertexInfos.first );
						newVertexInfos.second.component = currentSetCount++;
						++newAssignedCount;
						break;
					}
				}
			}
			Log::verbose( "%s - %u: found %u disjoint sub-sets.", outObject.name.c_str(), setId, currentSetCount );
		}

		// Generate currentSetCount sets to replace the initial set.
		for( uint sid = 0; sid < currentSetCount; ++sid )
		{
			outObject.faceSets.insert(outObject.faceSets.begin() + setId, Object::Set{} );
			Object::Set& newSet = outObject.faceSets[setId];
			newSet.material = set.material;
			for(const auto& face : set.faces){
				// Look at the first index in the face (any other would do too).
				uint newVertId = oldToNewMapping[ face.v0 ];
				if( newVerticesToInfos[ newVertId ].component == (int)sid){
					newSet.faces.push_back(face);
				}
			}
		}
		// Skip newly insert sets.
		setId += currentSetCount;
	}
	return true;
}

}
