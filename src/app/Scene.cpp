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
	materialsPath = resourcesPath / "materials";

	System::listAllFilesOfType(worldsPath, ".world", worldsList);
	System::listAllFilesOfType(modelsPath, ".dff", modelsList);
	System::listAllFilesOfType(templatesPath, ".template", templatesList);
	// Textures can be a bit everywhere...
	System::listAllFilesOfType(modelsPath, ".dds", texturesList);
	System::listAllFilesOfType(modelsPath, ".tga", texturesList);
	System::listAllFilesOfType(modelsPath, ".png", texturesList);
	System::listAllFilesOfType(texturesPath, ".dds", texturesList);
	System::listAllFilesOfType(texturesPath, ".tga", texturesList);
	System::listAllFilesOfType(texturesPath, ".png", texturesList);

	System::listAllFilesOfType(zonesPath, ".rf3", areasList);
	System::listAllFilesOfType(materialsPath, ".mtl", materialsList);

	std::sort(modelsList.begin(), modelsList.end());
	std::sort(worldsList.begin(), worldsList.end());
	std::sort(areasList.begin(), areasList.end());
	std::sort(materialsList.begin(), materialsList.end());
}

void Scene::clean(){
	globalMesh.clean();

	meshInfos.reset();
	instanceInfos.reset();
	materialInfos.reset();
	lightInfos.reset();

	for(Texture& tex : textures ){
		tex.clean();
	}

	textures.clear();
	meshDebugInfos.clear();
	instanceDebugInfos.clear();
	textureDebugInfos.clear();
}

uint Scene::retrieveTexture(const std::string& textureName, const GameFiles& files, std::vector<Texture>& textures2D) const {
	// Do we have the texture already.
	uint tid = 0;
	for( ; tid < textures2D.size(); ++tid){
		if(textures2D[tid].name() == textureName)
			return tid;
	}

	// Else emplace the texture.
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
		Log::warning("Unable to find texture named: %s", tex.name().c_str());
		// Try to be clever
		if(TextUtilities::hasSuffix(tex.name(), "_n")){
			Image::generateDefaultNormalImage(tex.images[0]);
		} else {
			Image::generateDefaultColorImage(tex.images[0]);
		}
	}
	// Update texture parameters.
	tex.width = tex.images[0].width;
	tex.height = tex.images[0].height;
	tex.depth = tex.levels = 1;
	tex.shape = TextureShape::D2;
	// Split BCn slices if needed.
	tex.uncompress();
	return tid;
}

Scene::TextureInfos Scene::storeTexture(const Texture& tex, uint tid, std::vector<TextureArrayInfos>& arraysToCreate) const{

	TextureInfos texInfos;

	uint arrayIndex = 0;
	for(TextureArrayInfos& textureArray : arraysToCreate){
		// Is the array compatible.
		if((textureArray.width == tex.width) && (textureArray.height == tex.height) && (textureArray.format == tex.images[0].compressedFormat)){
			// We found one! update the infos.
			texInfos.index = arrayIndex;
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
			texInfos.layer = localIndex;
			return texInfos;
		}
		++arrayIndex;
	}
	// Else create a new array.
	arraysToCreate.push_back({ tex.width, tex.height, tex.images[0].compressedFormat, {tid} });
	// Update the infos.
	texInfos.index = arrayIndex;
	texInfos.layer = 0;
	return texInfos;
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
		meshInfos = std::make_unique<StructuredBuffer<MeshInfos>>(meshCount, BufferType::STORAGE, "MeshInfos");
		meshDebugInfos.resize(meshCount);

		uint currentMeshId = 0;
		for(uint oid = 0u; oid < objectCount; ++oid){
			const uint vertexOffset = (uint)globalMesh.positions.size();
			uint indexOffset = (uint)globalMesh.indices.size();

			// Copy attributes.
			const Object& obj = world.objects()[oid];
			Log::check(!obj.positions.empty(), "Object with no positions.");
			Log::check((obj.positions.size() == obj.uvs.size()) && (obj.positions.size() == obj.normals.size()), "Discrepancy between positions and other attributes.");

			Mesh objMesh("obj");
			objMesh.positions = obj.positions;
			objMesh.normals = obj.normals;
			objMesh.texcoords = obj.uvs;
			objMesh.colors.resize(obj.colors.size());
			for(uint cid = 0; cid < obj.colors.size(); ++cid){
				objMesh.colors[cid] = glm::vec3(obj.colors[cid]);
			}

			// Total index count fo the object.
			size_t totalIndexSize = 0;
			for(const Object::Set& set : obj.faceSets){
				totalIndexSize += set.faces.size() * 3;
			}
			objMesh.indices.reserve(totalIndexSize);
			for(const Object::Set& set : obj.faceSets){
				for(const Object::Set::Face& f : set.faces){
	#ifdef DEBUG
					if(f.t0 != f.v0 || f.t1 != f.v1 || f.t2 != f.v2 || f.n0 != f.v0 || f.n1 != f.v1 || f.n2 != f.v2 ){
						Log::error("Discrepancy between position indices and other attribute indices.");
					}
	#endif
					objMesh.indices.push_back(f.v0);
					objMesh.indices.push_back(f.v1);
					objMesh.indices.push_back(f.v2);
				}
			}
			objMesh.computeTangentsAndBitangents(true);

			globalMesh.positions.insert(globalMesh.positions.end(), objMesh.positions.begin(), objMesh.positions.end());
			globalMesh.texcoords.insert(globalMesh.texcoords.end(), objMesh.texcoords.begin(), objMesh.texcoords.end());
			globalMesh.normals.insert(globalMesh.normals.end(), objMesh.normals.begin(), objMesh.normals.end());
			globalMesh.tangents.insert(globalMesh.tangents.end(), objMesh.tangents.begin(), objMesh.tangents.end());
			globalMesh.bitangents.insert(globalMesh.bitangents.end(), objMesh.bitangents.begin(), objMesh.bitangents.end());
			globalMesh.indices.insert(globalMesh.indices.end(), objMesh.indices.begin(), objMesh.indices.end());

			// Pack each mesh.
			uint currentSetId = 0u;
			for(const Object::Set& set : obj.faceSets){

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
				infos.bboxMin = glm::vec4(debugInfos.bbox.minis, 1.0f);
				infos.bboxMax = glm::vec4(debugInfos.bbox.maxis, 1.0f);
				indexOffset += infos.indexCount;
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
		instanceInfos = std::make_unique<StructuredBuffer<MeshInstanceInfos>>(totalInstancesCount, BufferType::STORAGE, "InstanceInfos");
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
				debugInfos.meshIndex = currentMeshId;
				++currentInstanceId;
			}
			++currentMeshId;
		}
	}

	/// Material and textures.
	{
		const std::vector<Object::Material>& materials = world.materials();
		materialInfos = std::make_unique<StructuredBuffer<MaterialInfos>>(materials.size(), BufferType::STORAGE, "MaterialInfos");

		// Load all textures.
		std::vector<Texture> textures2D;
		textures2D.reserve(materials.size());

		std::vector<TextureArrayInfos> arraysToCreate;

		uint materialId = 0u;
		for(const Object::Material& material : materials){
			MaterialInfos& matInfos = (*materialInfos)[materialId];
			matInfos.type = uint(material.type);
			// Color
			{
				const std::string textureName = !material.color.empty() ? material.color : DEFAULT_ALBEDO_TEXTURE;
				const uint tid = retrieveTexture(textureName, files, textures2D);
				// Now that we have the 2D texture, we need to find a compatible texture array to insert it in.
				matInfos.color = storeTexture(textures2D[tid], tid, arraysToCreate);
			}
			// Normal
			{
				const std::string textureName = !material.normal.empty() ? material.normal : DEFAULT_NORMAL_TEXTURE;
				const uint tid = retrieveTexture(textureName, files, textures2D);
				// Now that we have the 2D texture, we need to find a compatible texture array to insert it in.
				matInfos.normal = storeTexture(textures2D[tid], tid, arraysToCreate);
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
			// Do not apply gamma correction as there are normal maps in the array.
			// Conversion for color will be done in the shaders.
			tex.upload(Layout::RGBA8, false);
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

	// Lights
	{
		const float sceneRadius = computeBoundingBox().getSphere().radius;

		const uint lightsCount = world.lights().size();
		lightInfos = std::make_unique<StructuredBuffer<LightInfos>>(lightsCount, BufferType::STORAGE, "LightInfos");
		uint shadowIndex = 0u;
		for(uint i = 0; i < lightsCount; ++i){
			const World::Light& light = world.lights()[i];
			const float maxRadius = std::max(light.radius.x, std::max(light.radius.y, light.radius.z));
			const glm::vec3 lightPos = glm::vec3(light.frame[3]);
			LightInfos& info = (*lightInfos)[i];

			glm::mat4 view = glm::inverse(light.frame);
			view[0][2] *= -1.0f;
			view[1][2] *= -1.0f;
			view[2][2] *= -1.0f;
			view[3][2] *= -1.0f;
			glm::mat4 proj = glm::mat4(1.0f);
			info.shadow = World::Light::NO_SHADOW;
			if(light.shadow){
				info.shadow = shadowIndex;
				shadowIndex += light.type == World::Light::POINT ? 6u : 1u;
			}

			const float near = 5.0f;
			const float far = 2.f * (maxRadius == 0.f ? sceneRadius : maxRadius);
			if(light.type == World::Light::SPOT){
				proj = Frustum::perspective(std::max(light.angle, 0.1f), 1.0f, far, near);
			} else if(light.type == World::Light::DIRECTIONAL){
				proj = Frustum::ortho(-light.radius.x, light.radius.x, -light.radius.y, light.radius.y, far, near);
			} else {
				proj = Frustum::perspective(glm::half_pi<float>(), 1.0f, far, near);
				view = glm::mat4(1.0f);
			}
			info.vp = proj * view;

			info.positionAndMaxRadius = glm::vec4(lightPos, maxRadius);
			info.colorAndType = glm::vec4(light.color, float(light.type));
			const glm::vec3 axisX = glm::normalize(glm::vec3(light.frame[0]));
			const glm::vec3 axisY = glm::normalize(glm::vec3(light.frame[1]));
			const glm::vec3 axisZ = glm::normalize(glm::vec3(light.frame[2]));
			info.axisAndRadiusX = glm::vec4(axisX / light.radius.x, 0.0f);
			info.axisAndRadiusY = glm::vec4(axisY / light.radius.y, 0.0f);
			info.axisAndRadiusZ = glm::vec4(axisZ / light.radius.z, 0.0f);
			info.materialIndex = light.material;
		}
	}

	// Send data to the GPU.
	globalMesh.upload();
	instanceInfos->upload();
	meshInfos->upload();
	materialInfos->upload();
	lightInfos->upload();

	GPU::registerTextures( textures );
}

void Scene::load(const fs::path& worldPath, const GameFiles& files){
	
	world = World();
	world.load(worldPath, files.resourcesPath);
	upload(world, files);

}

void Scene::loadFile(const fs::path& filePath, const GameFiles& files){

	Object obj;
	const std::string extension = filePath.extension().string();
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

	world = World(obj);
	upload(world, files);

}

BoundingBox Scene::computeBoundingBox() const {
	if(instanceDebugInfos.empty()){
		return BoundingBox(glm::vec3(-100.0f), glm::vec3(100.0f));
	}
	
	BoundingBox bbox;
	for(const InstanceCPUInfos& infos : instanceDebugInfos){
		bbox.merge(infos.bbox);
	}
	return bbox;
}
