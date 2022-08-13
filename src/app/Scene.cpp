#include "Scene.hpp"

#include "core/DFFParser.hpp"
#include "core/AreaParser.hpp"

#include "graphics/GPU.hpp"
#include "Common.hpp"


GameFiles::GameFiles(){}

GameFiles::GameFiles(const fs::path& installPath){

	resourcesPath = installPath / "resources";
	modelsPath = resourcesPath / "models";
	texturesPath = resourcesPath / "textures";
	templatesPath = resourcesPath / "templates";
	zonesPath = resourcesPath / "zones";
	worldsPath = zonesPath / "world";

	System::listAllFilesOfType(worldsPath, ".world", worldsList);
	System::listAllFilesOfType(modelsPath, ".dff", modelsList);
	System::listAllFilesOfType(templatesPath, ".template", templatesList);
	// Textures can be a bit everywhere...
	System::listAllFilesOfType(modelsPath, ".dds", texturesList);
	System::listAllFilesOfType(modelsPath, ".tga", texturesList);
	System::listAllFilesOfType(texturesPath, ".dds", texturesList);
	System::listAllFilesOfType(texturesPath, ".tga", texturesList);
	System::listAllFilesOfType(zonesPath, ".rf3", areasList);

	std::sort(modelsList.begin(), modelsList.end());
	std::sort(worldsList.begin(), worldsList.end());
	std::sort(areasList.begin(), areasList.end());
}



void Scene::clean(){
	globalMesh.clean();

	meshInfos.reset();
	instanceInfos.reset();
	materialInfos.reset();

	for(Texture& tex : textures ){
		tex.clean();
	}

	textures.clear();
	meshDebugInfos.clear();
	instanceDebugInfos.clear();
	textureDebugInfos.clear();
}

void Scene::upload(const World& world, const GameFiles& files){

	clean();

	/// Populate the mesh geometry and corresponding sub-mesh info.
	{
		const size_t instanceCount = world.instances().size();
		const size_t objectCount = world.objects().size();
		globalMesh = Mesh(world.name());

		// Estimate the number of meshes ahead, and per-object mesh range in the list.
		size_t meshCount = 0;
		std::vector<std::pair<size_t, size_t>> objectMeshIndicesRange;
		objectMeshIndicesRange.reserve(objectCount);

		for(const Object& obj : world.objects()){
			const size_t oldMeshCount = meshCount;
			meshCount += obj.faceSets.size();
			objectMeshIndicesRange.emplace_back(oldMeshCount, meshCount);
		}
		meshInfos = std::make_unique<StructuredBuffer<MeshInfos>>(meshCount, BufferType::STORAGE);
		meshDebugInfos.resize(meshCount);

		uint currentMeshId = 0;
		for(uint oid = 0u; oid < objectCount; ++oid){
			const uint vertexOffset = (uint)globalMesh.positions.size();

			// Copy attributes.
			const Object& obj = world.objects()[oid];
			Log::check(!obj.positions.empty(), "Object with no positions.");
			globalMesh.positions.insert(globalMesh.positions.end(), obj.positions.begin(), obj.positions.end());
			globalMesh.texcoords.insert(globalMesh.texcoords.end(), obj.uvs.begin(), obj.uvs.end());
			globalMesh.normals.insert(globalMesh.normals.end(), obj.normals.begin(), obj.normals.end());

			Log::check((obj.positions.size() == obj.uvs.size()) && (obj.positions.size() == obj.normals.size()), "Discrepancy between positions and other attributes.");

			// Total index count fo the object.
			size_t totalIndexSize = 0;
			for(const Object::Set& set : obj.faceSets){
				totalIndexSize += set.faces.size() * 3;
			}
			globalMesh.indices.reserve(globalMesh.indices.size() + totalIndexSize);

			// Pack each mesh.
			uint currentSetId = 0u;
			for(const Object::Set& set : obj.faceSets){
				// Build index buffer.
				const uint indexOffset = (uint)globalMesh.indices.size();
				for(const Object::Set::Face& f : set.faces){
	#ifdef DEBUG
					if(f.t0 != f.v0 || f.t1 != f.v1 || f.t2 != f.v2 || f.n0 != f.v0 || f.n1 != f.v1 || f.n2 != f.v2 ){
						Log::error("Discrepancy between position indices and other attribute indices.");
					}
	#endif
					globalMesh.indices.push_back(f.v0);
					globalMesh.indices.push_back(f.v1);
					globalMesh.indices.push_back(f.v2);
				}

				// Populate mesh info.
				MeshInfos& infos = (*meshInfos)[currentMeshId];
				infos.vertexOffset = vertexOffset;
				infos.firstIndex = indexOffset;
				infos.indexCount = (uint)set.faces.size() * 3u;
				infos.materialIndex = (uint)set.material;
				// ...and additional CPU info (bounding box).
				MeshCPUInfos& debugInfos = meshDebugInfos[currentMeshId];
				debugInfos.name = obj.name + "_part_" + std::to_string(currentSetId);
				debugInfos.bbox = BoundingBox();
				for(const Object::Set::Face& f : set.faces){
					debugInfos.bbox.merge(obj.positions[f.v0]);
					debugInfos.bbox.merge(obj.positions[f.v1]);
					debugInfos.bbox.merge(obj.positions[f.v2]);
				}

				++currentMeshId;
				++currentSetId;
			}
		}

		// For each mesh of each object, how many instances are there.
		std::vector<std::vector<uint>> perMeshInstanceIndices(meshCount);
		size_t totalInstancesCount = 0u;

		for(uint iid = 0; iid < instanceCount; ++iid){
			// From an object instance, create a set of meshes instances.
			const World::Instance& instance = world.instances()[iid];
			const auto meshIndicesRange = objectMeshIndicesRange[instance.object];

			for(uint mid = meshIndicesRange.first; mid < meshIndicesRange.second; ++mid){
				perMeshInstanceIndices[mid].push_back(iid);
				++totalInstancesCount;
			}
		}

		// Build a list of unrolled instance data (frames...) and update mesh infos.
		instanceInfos = std::make_unique<StructuredBuffer<MeshInstanceInfos>>(totalInstancesCount, BufferType::STORAGE);
		instanceDebugInfos.resize(totalInstancesCount);

		uint currentInstanceId = 0u;
		currentMeshId = 0u;
		for(const auto& instanceIndices : perMeshInstanceIndices){
			// Update the corresponding mesh infos
			MeshInfos& infos = (*meshInfos)[currentMeshId];
			infos.firstInstanceIndex = currentInstanceId;
			infos.instanceCount = (uint)instanceIndices.size();

			const MeshCPUInfos& parentDebugInfos = meshDebugInfos[currentMeshId];

			// And pouplate the instance data.
			for(const auto& iid : instanceIndices){
				const World::Instance& instance = world.instances()[iid];
				// Populate rendering info.
				(*instanceInfos)[currentInstanceId].frame = instance.frame;
				// ...and additional CPU info.
				InstanceCPUInfos& debugInfos = instanceDebugInfos[currentInstanceId];
				debugInfos.name = instance.name + "_" + parentDebugInfos.name;
				debugInfos.bbox = parentDebugInfos.bbox.transformed(instance.frame);
				++currentInstanceId;
			}
			++currentMeshId;
		}
	}

	/// Material and textures.
	{
		const std::vector<Object::Material>& materials = world.materials();
		materialInfos = std::make_unique<StructuredBuffer<MaterialInfos>>(materials.size(), BufferType::STORAGE);

		// Load all textures.
		std::vector<Texture> textures2D;
		textures2D.reserve(materials.size());

		std::vector<TextureArrayInfos> arraysToCreate;

		uint materialId = 0u;
		for(const Object::Material& material : materials){
			const std::string textureName = material.texture.empty() ? "Empty texture" : material.texture;

			// Do we have the texture already.
			uint tid = 0;
			for( ; tid < textures2D.size(); ++tid){
				if(textures2D[tid].name() == textureName)
					break;
			}
			// Else emplace the texture.
			if(tid == textures2D.size()){
				Texture& tex = textures2D.emplace_back(textureName);
				// Find the file on disk.
				for( const fs::path& texturePath : files.texturesList){
					const std::string existingName = texturePath.filename().replace_extension().string();
					if(existingName == textureName){
						tex.images.resize(1);
						tex.images[0].load(texturePath);
						break;
					}
				}
				// If nothing was loaded, populate with default data.
				if(tex.images.empty()){
					tex.images.emplace_back();
					Image::generateDefaultImage(tex.images[0]);
				}
				// Update texture parameters.
				tex.width = tex.images[0].width;
				tex.height = tex.images[0].height;
				tex.depth = tex.levels = 1;
				tex.shape = TextureShape::D2;
				// Split BCn slices if needed.
				tex.uncompress();
			}

			// Now that we have the 2D texture, we need to find a compatible texture array to insert it in.
			const Texture& tex = textures2D[tid];
			MaterialInfos& matInfos = (*materialInfos)[materialId];
			matInfos.type = uint(material.type);

			uint arrayIndex = 0;
			for(TextureArrayInfos& textureArray : arraysToCreate){
				// Is the array compatible.
				if((textureArray.width == tex.width) && (textureArray.height == tex.height) && (textureArray.format == tex.images[0].compressedFormat)){
					// We found one! update the infos.
					matInfos.texture.index = arrayIndex;
					// Is the texture already in there.
					uint localIndex = 0;
					for(uint texId : textureArray.textures){
						if(texId == tid){
							break;
						}
						++localIndex;
					}
					// Else add it to the list.
					if(localIndex == textureArray.textures.size()){
						textureArray.textures.push_back(tid);
					}
					// Update the infos.
					matInfos.texture.layer = localIndex;
					break;
				}
				++arrayIndex;
			}
			// Else create a new array.
			if(arrayIndex == arraysToCreate.size()){
				arraysToCreate.push_back({ tex.width, tex.height, tex.images[0].compressedFormat, {tid} });
				// Update the infos.
				matInfos.texture.index = arrayIndex;
				matInfos.texture.layer = 0;
			}
			++materialId;
		}

		// Build the texture arrays we have generated above.
		for(const TextureArrayInfos& arrayInfos : arraysToCreate){
			const std::string texName = "TexArray_" + std::to_string(arrayInfos.width)
											  + "_" + std::to_string(arrayInfos.height)
											  + "_" + std::to_string((uint)arrayInfos.format);
			Texture& tex = textures.emplace_back(texName);
			tex.width = arrayInfos.width;
			tex.height = arrayInfos.height;
			tex.shape = TextureShape::Array2D;
			tex.depth = (uint)arrayInfos.textures.size();
			// Use the minimum mip count provided by all textures in the array.
			tex.levels = 0xFFFF;
			for(const uint tid : arrayInfos.textures){
				const Texture& layerTex = textures2D[tid];
				tex.levels = std::min(tex.levels, layerTex.levels);
			}
			tex.images.resize(tex.depth * tex.levels);
			for(uint mid = 0; mid < tex.levels; ++mid){
				for(uint lid = 0; lid < tex.depth; ++lid){
					const uint texId = arrayInfos.textures[lid];
					textures2D[texId].images[mid].clone(tex.images[mid * tex.depth + lid]);
				}
			}
			// Now we have a beautiful texture2D array with all images set.
			tex.upload( Layout::SRGB8_ALPHA8, false );
		}

		// Unroll the arrays to generate additional CPU infos on all textures.
		uint currentArrayIndex = 0u;
		for(const TextureArrayInfos& arrayInfos : arraysToCreate){
			uint currentLayerIndex = 0u;
			for(const uint tid : arrayInfos.textures){
				TextureCPUInfos& debugInfos = textureDebugInfos.emplace_back();
				debugInfos.name = textures2D[tid].name();
				debugInfos.data.index = currentArrayIndex;
				debugInfos.data.layer = currentLayerIndex;
				++currentLayerIndex;
			}
			++currentArrayIndex;
		}
	}

	// Send data to the GPU.
	globalMesh.upload();
	instanceInfos->upload();
	meshInfos->upload();
	materialInfos->upload();

	GPU::registerTextures( textures );
}

void Scene::load(const fs::path& worldPath, const GameFiles& files){
	
	World world;
	world.load(worldPath, files.resourcesPath);
	upload(world, files);

}

void Scene::loadFile(const fs::path& filePath, const GameFiles& files){

	Object obj;
	const std::string extension = filePath.extension();
	if(extension == ".dff"){
		if(!Dff::load(filePath, obj)){
			return;
		}
	} else if(extension== ".rf3"){
		if(!Area::load(filePath, obj)){
			return;
		}
	} else {
		Log::error("Unknown file extension: %s", extension.c_str());
		return;
	}

	World fakeWorld(obj);
	upload(fakeWorld, files);

}

BoundingBox Scene::computeBoundingBox() const {
	BoundingBox bbox;
	for(const InstanceCPUInfos& infos : instanceDebugInfos){
		bbox.merge(infos.bbox);
	}
	return bbox;
}
