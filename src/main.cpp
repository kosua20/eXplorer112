#include "core/Log.hpp"
#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/DFFParser.hpp"

#include <fstream>

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

		Model model;

		Log::info("In: %s", modelPath.c_str());
		if(!Dff::parse(modelPath, model)){
			Log::error("Failed to parse.");
			continue;
		}

		// Convert geometries.
		for(Geometry& geom : model.geometries){
			// Sort triangles based on material first.
			std::sort(geom.faces.begin(), geom.faces.end(), [](const Triangle& a, const Triangle& b){
				if(a.id < b.id){
					return true;
				}
				if(a.id > b.id){
					return false;
				}
				return (a.v0 < b.v0) || (a.v0 == b.v0 && a.v1 < b.v1) || (a.v0 == b.v0 && a.v1 == b.v1 && a.v2 < b.v2);
			});

			for(auto& uvs : geom.uvs){
				for(auto& uv : uvs){
					uv[1] = 1.f - uv[1];
				}
			}
		}

		Log::info("Output: %d elements", model.pairings.size());
		if(model.pairings.empty()){
			continue;
		}

		const std::string baseName = modelPath.filename().replace_extension().string();
		const std::string itemName = baseName;
		std::ofstream outputObj(outputPath / (itemName + ".obj") );
		std::ofstream outputMtl(outputPath / (itemName + ".mtl") );
		outputObj << "mtllib " << itemName << ".mtl" << "\n";

		// From all the pairs of frame/geometry, we need to build a valid set of objects, each with a texture.
		int pairId = 0;
		size_t vertexIndex = 0;
		size_t uvIndex = 0;
		size_t normalIndex = 0;

		for(const Model::Pair& pair : model.pairings){

			// Rebuild frame.
			int32_t frameIndex = pair.frame;
			glm::mat4 totalFrame(1.0f);
			do {
				totalFrame = model.frames[frameIndex].mat * totalFrame;
				frameIndex = model.frames[frameIndex].parent;
				if(frameIndex >= (int32_t)model.frames.size()){
					Log::warning("Unexpected frame index: %d, stopping.", frameIndex);
					break;
				}
			} while(frameIndex >= 0);

			const glm::mat3 totalFrameNormal = glm::transpose(glm::inverse(glm::mat3(totalFrame)));
			// Fetch geometry.
			const Geometry& geom = model.geometries[pair.geometry];
			// Assumption: always one morphset and one texset.
			const MorphSet& set = geom.sets[0];

			// Output vertices, normals and uvs if present.
			const size_t vertCount = set.positions.size();
			const bool hasNormals = set.normals.size() == vertCount;

			const bool hasUvs = !geom.uvs.empty() && (geom.uvs[0].size() == vertCount);
			const bool hasColors = geom.colors.size() == vertCount;


			for(size_t vid = 0; vid < vertCount; ++vid){
				const glm::vec3 tpos = glm::vec3(totalFrame * glm::vec4(set.positions[vid], 1.f));
				outputObj << "v " << tpos[0] << " " << tpos[1] << " " << tpos[2];
				if(hasColors){
					// Skip colors for now as Meshlab uses them.
					//const Color& col = geom.colors[vid];
					//outputObj << " " << int(col.r) << " " << int(col.g) << " " << int(col.b);
				}
				outputObj << "\n";
			}
			if(hasNormals){
				for(size_t vid = 0; vid < vertCount; ++vid){
					const glm::vec3 tnor = glm::normalize(totalFrameNormal * set.normals[vid]);
					outputObj << "vn " << tnor[0] << " " << tnor[1] << " " << tnor[2] << "\n";
				}
			}
			if(hasUvs){
				const TexSet& uvs = geom.uvs[0];
				for(size_t vid = 0; vid < vertCount; ++vid){
					const glm::vec2& uv = uvs[vid];
					outputObj << "vt " << uv[0] << " " << uv[1] << "\n";
				}
			}

			// Then triangles, split by material.
			const size_t triCount = geom.faces.size();
			outputObj << "s 1\n";

			uint16_t materialId = 0xFF;
			for(size_t tid = 0; tid < triCount; ++tid){

				if(geom.faces[tid].id != materialId){
					materialId = geom.faces[tid].id;
					// New group.
					const std::string matName = itemName + "_" + std::to_string(pairId) + "_" + std::to_string(materialId);
					outputMtl << "newmtl " << matName << "\n";

					const Material& material = geom.materials[geom.mappings[materialId]];
					const float& amb = material.ambSpecDiff[0];
					outputMtl << "Ka " << amb << " " << amb << " " << amb << "\n";
					const float& diff = material.ambSpecDiff[2];
					outputMtl << "Kd " << diff << " " << diff << " " << diff << "\n";
					const float& spec = material.ambSpecDiff[1];
					outputMtl << "Ks " << spec << " " << spec << " " << spec << "\n";
					outputMtl << "Ns " << 100 << "\n";
					if(!material.diffuseName.empty()){
						outputMtl << "map_Kd " << "textures/" << material.diffuseName << "\n";
					}

					outputObj << "usemtl " << matName << "\n";
				}
				const size_t v0 = geom.faces[tid].v0 + 1u;
				const size_t v1 = geom.faces[tid].v1 + 1u;
				const size_t v2 = geom.faces[tid].v2 + 1u;

				outputObj << "f ";
				// Remember: v/vt/vn
				if(hasUvs && hasNormals){
					outputObj << (v0+vertexIndex) << "/" << (v0+uvIndex) << "/" << (v0+normalIndex) << " ";
					outputObj << (v1+vertexIndex) << "/" << (v1+uvIndex) << "/" << (v1+normalIndex) << " ";
					outputObj << (v2+vertexIndex) << "/" << (v2+uvIndex) << "/" << (v2+normalIndex) << "\n";
				} else if(hasUvs){
					outputObj << (v0+vertexIndex) << "/" << (v0+uvIndex) << " ";
					outputObj << (v1+vertexIndex) << "/" << (v1+uvIndex) << " ";
					outputObj << (v2+vertexIndex) << "/" << (v2+uvIndex) << "\n";

				} else if(hasNormals){
					outputObj << (v0+vertexIndex) << "//" << (v0+normalIndex) << " ";
					outputObj << (v1+vertexIndex) << "//" << (v1+normalIndex) << " ";
					outputObj << (v2+vertexIndex) << "//" << (v2+normalIndex) << "\n";

				} else {
					outputObj << (v0+vertexIndex) << " ";
					outputObj << (v1+vertexIndex) << " ";
					outputObj << (v2+vertexIndex) << "\n";

				}
			}

			vertexIndex += vertCount;
			uvIndex += hasUvs ? vertCount : 0;
			normalIndex += hasNormals ? vertCount : 0;
			++pairId;
		}
		outputObj.close();
		outputMtl.close();
		Log::info("-------------------------------------------------------------");

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
