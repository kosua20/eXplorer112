#include "Scene.hpp"

#include "core/WorldParser.hpp"
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

	meshInfosBuffer.reset();
	meshInstanceInfosBuffer.reset();
	materialInfosBuffer.reset();

	for(Texture& tex : textures ){
		tex.clean();
	}
	textures.clear();
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


	Log::check(!obj.positions.empty(), "Object with no positions.");

	const size_t posCount = obj.positions.size();
	if(obj.uvs.size() != posCount){
		obj.uvs.resize(posCount, glm::vec2(0.5f));
	}
	if(obj.normals.size() != posCount){
		obj.normals.resize(posCount, glm::vec3(0.0f, 0.0f, 1.0f));
	}
	globalMesh = Mesh(filePath.filename().replace_extension());
	globalMesh.positions = obj.positions;
	globalMesh.texcoords = obj.uvs;
	globalMesh.normals = obj.normals;

	// Total index count fo the object.
	size_t totalIndexSize = 0;
	for(const Object::Set& set : obj.faceSets){
		totalIndexSize += set.faces.size() * 3;
	}

	globalMesh.indices.reserve(totalIndexSize);

	// Already allocate the buffers as we know the number of meshes and instances.
	uint meshesCount = obj.faceSets.size();
	meshInfosBuffer = std::make_unique<StructuredBuffer<MeshInfos>>(meshesCount, BufferType::STORAGE);
	// There is one instance per mesh.
	meshInstanceInfosBuffer = std::make_unique<StructuredBuffer<MeshInstanceInfos>>(meshesCount, BufferType::STORAGE);

	uint meshIndex = 0;
	// Pack each mesh.
	for(const Object::Set& set : obj.faceSets){
		// Build index buffer.
		const uint indexOffset = (uint)globalMesh.indices.size();

		for(const Object::Set::Face& f : set.faces){
#ifdef DEBUG
			if(f.t0 != f.v0 || f.t1 != f.v1 || f.t2 != f.v2 ||
			   f.n0 != f.v0 || f.n1 != f.v1 || f.n2 != f.v2 ){
				Log::error("Discrepancy between position indices and other attribute indices.");
			}
#endif
			globalMesh.indices.push_back(f.v0);
			globalMesh.indices.push_back(f.v1);
			globalMesh.indices.push_back(f.v2);

		}
		// Generate extra infos.
		BoundingBox bbox = BoundingBox();
		if(!set.faces.empty()) {
			bbox.minis = bbox.maxis = obj.positions[set.faces[0].v0];
		}
		for(const Object::Set::Face& f : set.faces){
			bbox.merge(obj.positions[f.v0]);
			bbox.merge(obj.positions[f.v1]);
			bbox.merge(obj.positions[f.v2]);

		}

		MeshInfos& meshInfos = meshInfosBuffer->at(meshIndex);
		meshInfos.bboxMin = glm::vec4(bbox.minis, 1.0f);
		meshInfos.bboxMax = glm::vec4(bbox.maxis, 1.0f);
		meshInfos.firstIndex = indexOffset;
		meshInfos.firstInstanceIndex = meshIndex;
		meshInfos.materialIndex = (uint)set.material;
		meshInfos.instanceCount = 1u; // One instance for each mesh.
		meshInfos.indexCount = (uint)set.faces.size() * 3u;
		meshInfos.vertexOffset = 0u; // Only one object.

		meshInstanceInfosBuffer->at(meshIndex).frame = glm::mat4(1.0f);

		++meshIndex;

	}

	// Populate material info and load textures.
	const std::vector<Object::Material>& materials = obj.materials;
	materialInfosBuffer = std::make_unique<StructuredBuffer<MaterialInfos>>(materials.size(), BufferType::STORAGE);

	// Load all textures each in its own array to keep things simple.
	textures.reserve(materials.size());

	uint materialId = 0u;
	for(const Object::Material& material : materials){
		const std::string textureName = material.texture.empty() ? "No texture" : material.texture;

		uint mid = 0;
		// Do we have the texture already.
		for( ; mid < textures.size(); ++mid){
			if(textures[mid].name() == textureName)
				break;
		}
		// Else emplace the texture.
		if(mid == textures.size()){
			Texture& tex = textures.emplace_back(textureName);

			for( const fs::path& texturePath : files.texturesList){
				const std::string existingName = texturePath.filename().replace_extension().string();
				if(existingName == textureName){
					tex.images.resize(1);
					tex.images[0].load(texturePath);
					break;
				}
			}
			if(tex.images.empty()){
				tex.images.emplace_back();
				Image::generateDefaultImage(tex.images[0]);
			}
			// Update texture parameters.
			tex.width = tex.images[0].width;
			tex.height = tex.images[0].height;
			tex.depth = tex.levels = 1;
			tex.shape = TextureShape::D2;
			tex.uncompress();
			// Convert it to a 2D array with one layer.
			tex.shape = TextureShape::Array2D;
			tex.upload(Layout::SRGB8_ALPHA8, false);
		}

		MaterialInfos& matInfos = materialInfosBuffer->at(materialId);
		matInfos.texture.index = mid;
		matInfos.texture.layer = 0;
		++materialId;
	}

	// Send data to the GPU.
	globalMesh.upload();
	meshInstanceInfosBuffer->upload();
	meshInfosBuffer->upload();
	materialInfosBuffer->upload();

	GPU::registerTextures( textures );

}

void Scene::load(const fs::path& worldPath, const GameFiles& files){

	struct MeshExtInfos {
	 BoundingBox bbox;
	 uint object;
	 uint material;
	 uint indexCount;
	 uint firstIndex;
	 uint vertexOffset;

	 MeshExtInfos(const BoundingBox& _bbox, uint _object, uint _material, uint _indexCount, uint _firstIndex, uint _vertexOffset) :
			 bbox(_bbox), object(_object), material(_material),
			 indexCount(_indexCount), firstIndex(_firstIndex), vertexOffset(_vertexOffset)
		 { }
	 };

	// Load the world and setup all the GPU data.
	World world;
	world.load(worldPath, files.resourcesPath);
	globalMesh = Mesh(worldPath.filename().replace_extension());

	const size_t instanceCount = world.instances().size();
	const size_t objectCount = world.objects().size();

	uint meshesCount = 0;
	std::vector<MeshExtInfos> meshInfos;
	std::vector<uint> objectFirstMeshIndex;

	for(uint oid = 0u; oid < objectCount; ++oid){
		const Object& obj = world.objects()[oid];

		// Book keeping for later.
		objectFirstMeshIndex.push_back(meshesCount);
		meshesCount += (uint)obj.faceSets.size();

		// Copy attributes.
		const uint vertexOffset = (uint)globalMesh.positions.size();

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
		for(const Object::Set& set : obj.faceSets){
			// Build index buffer.
			const uint indexOffset = (uint)globalMesh.indices.size();

			for(const Object::Set::Face& f : set.faces){
#ifdef DEBUG
				if(f.t0 != f.v0 || f.t1 != f.v1 || f.t2 != f.v2 ||
				   f.n0 != f.v0 || f.n1 != f.v1 || f.n2 != f.v2 ){
					Log::error("Discrepancy between position indices and other attribute indices.");
				}
#endif
				globalMesh.indices.push_back(f.v0);
				globalMesh.indices.push_back(f.v1);
				globalMesh.indices.push_back(f.v2);

			}
			// Generate extra infos.
			BoundingBox bbox = BoundingBox();
			if(!set.faces.empty()) {
				bbox.minis = bbox.maxis = obj.positions[set.faces[0].v0];
			}
			for(const Object::Set::Face& f : set.faces){
				bbox.merge(obj.positions[f.v0]);
				bbox.merge(obj.positions[f.v1]);
				bbox.merge(obj.positions[f.v2]);

			}

			meshInfos.emplace_back(bbox, (uint)oid, (uint)set.material, (uint)set.faces.size() * 3u, indexOffset, vertexOffset);
		}
	}


	// For each mesh of each object, how many instances and which ones?
	std::vector<std::vector<uint>> instancesReferencingMesh(meshesCount);
	for(uint iid = 0; iid < instanceCount; ++iid){
		const World::Instance& instance = world.instances()[iid];
		const uint firstMeshIndex = objectFirstMeshIndex[instance.object];
		const uint nextObjectIndex = instance.object + 1u;
		const uint afterLastMeshIndex = nextObjectIndex < objectCount ? objectFirstMeshIndex[nextObjectIndex] : meshesCount;
		for(uint mid = firstMeshIndex; mid < afterLastMeshIndex; ++mid){
			instancesReferencingMesh[mid].push_back(iid);
		}
	}
	uint totalMeshInstancesCount = 0;
	for(const auto& meshInstances : instancesReferencingMesh){
		totalMeshInstancesCount += (uint)meshInstances.size();
	}
	// Build a list of unrolled frames.
	meshInstanceInfosBuffer = std::make_unique<StructuredBuffer<MeshInstanceInfos>>(totalMeshInstancesCount, BufferType::STORAGE);
	meshInfosBuffer = std::make_unique<StructuredBuffer<MeshInfos>>(meshesCount, BufferType::STORAGE);
	uint currentMeshInstanceIndex = 0;
	uint currentMeshIndex = 0;
	for(const auto& instanceIndices : instancesReferencingMesh){

		// TODO: do above by estimating meshesCount first.
		const MeshExtInfos& infos = meshInfos[currentMeshIndex];
		MeshInfos& gpuInfos = meshInfosBuffer->at(currentMeshIndex);
		gpuInfos.bboxMin = glm::vec4(infos.bbox.minis, 1.f);
		gpuInfos.bboxMax = glm::vec4(infos.bbox.maxis, 1.f);
		gpuInfos.indexCount = infos.indexCount;
		gpuInfos.firstIndex = infos.firstIndex;
		gpuInfos.vertexOffset = infos.vertexOffset;
		gpuInfos.instanceCount = (uint)instanceIndices.size();
		gpuInfos.firstInstanceIndex = currentMeshInstanceIndex;
		gpuInfos.materialIndex = infos.material;

		for(const auto& iid : instanceIndices){
			meshInstanceInfosBuffer->at(currentMeshInstanceIndex).frame = world.instances()[iid].frame;
			++currentMeshInstanceIndex;
		}


		++currentMeshIndex;
	}

	const std::vector<Object::Material>& materials = world.materials();
	materialInfosBuffer = std::make_unique<StructuredBuffer<MaterialInfos>>(materials.size(), BufferType::STORAGE);

	// Load all textures.
	std::vector<Texture> textures2D;
	textures2D.reserve(materials.size());

	struct TextureArrayInfos {
		uint width = 0;
		uint height = 0;
		Image::Compression format = Image::Compression::NONE;
		std::vector<uint> textures; // Indices in textures2D
	};
	std::vector<TextureArrayInfos> arraysToCreate;

	uint materialId = 0u;
	for(const Object::Material& material : materials){
		const std::string textureName = material.texture.empty() ? "No texture" : material.texture;

		uint mid = 0;
		// Do we have the texture already.
		for( ; mid < textures2D.size(); ++mid){
			if(textures2D[mid].name() == textureName)
				break;
		}
		// Else emplace the texture.
		if(mid == textures2D.size()){
			Texture& tex = textures2D.emplace_back(textureName);

			for( const fs::path& texturePath : files.texturesList){
				const std::string existingName = texturePath.filename().replace_extension().string();
				if(existingName == textureName){
					tex.images.resize(1);
					tex.images[0].load(texturePath);
					break;
				}
			}
			if(tex.images.empty()){
				tex.images.emplace_back();
				Image::generateDefaultImage(tex.images[0]);
			}
			// Update texture parameters.
			tex.width = tex.images[0].width;
			tex.height = tex.images[0].height;
			tex.depth = tex.levels = 1;
			tex.shape = TextureShape::D2;
			// Split BC slices if needed.
			tex.uncompress();
		}

		// Now that we have the 2D texture, we need to find a compatible texture array to insert it in.
		const Texture& tex = textures2D[mid];
		MaterialInfos& matInfos = materialInfosBuffer->at(materialId);

		uint arrayIndex = 0;
		for(TextureArrayInfos& textureArray : arraysToCreate){
			if((textureArray.width == tex.width) && (textureArray.height == tex.height) && (textureArray.format == tex.images[0].compressedFormat)){
				// We found one! update the infos.
				matInfos.texture.index = arrayIndex;
				// Is the texture already in there
				uint localIndex = 0;
				for(uint texId : textureArray.textures){
					if(texId == mid){
						break;
					}
					++localIndex;
				}
				// Else add it to the list.
				if(localIndex == textureArray.textures.size()){
					textureArray.textures.push_back(mid);
				}
				// update the infos.
				matInfos.texture.layer = localIndex;
				break;
			}
			++arrayIndex;
		}
		if(arrayIndex == arraysToCreate.size()){
			arraysToCreate.push_back({ tex.width, tex.height, tex.images[0].compressedFormat, {mid} });
				// update the infos.
			matInfos.texture.index = arrayIndex;
			matInfos.texture.layer = 0;
		}
		++materialId;
	}

	// From the list of texture arrays, build them.
	for(const TextureArrayInfos& arrayInfos : arraysToCreate){
		const std::string texName = "TexArray_" + std::to_string(arrayInfos.width)
										  + "_" + std::to_string(arrayInfos.height)
										  + "_" + std::to_string((uint)arrayInfos.format);
		Texture& tex = textures.emplace_back(texName);
		tex.width = arrayInfos.width;
		tex.height = arrayInfos.height;
		tex.shape = TextureShape::Array2D;
		tex.depth = (uint)arrayInfos.textures.size();
		// Safety check.
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

	// Send data to the GPU.
	globalMesh.upload();
	meshInstanceInfosBuffer->upload();
	meshInfosBuffer->upload();
	materialInfosBuffer->upload();

	GPU::registerTextures( textures );

}
