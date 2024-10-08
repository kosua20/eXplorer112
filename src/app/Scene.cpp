#include "Scene.hpp"

#include "core/DFFParser.hpp"
#include "core/AreaParser.hpp"
#include "core/Random.hpp"

#include "graphics/GPU.hpp"
#include "Common.hpp"

// Fix up data

static const std::unordered_map<std::string, std::string> texFileSubtitutions = {
	/*
	// Probably unused normals for decals and billboards
	affiche_01_n
	balles_beton_n
	balles_rouille_n
	rust_n
	papier_02_n
	glass3break_n
	glass4break_n
	griffes_n
	dirty2_n
	dirty3_n
	dirty4_n
	dirty_n
	dirtymousse_n
	*/

	/*
	// Really missing
	biground1
	couverts_c
	couverts_n
	dresser5ice_n
	pochoir_03
	pochoirs_02
	pochoirs_1
	tache_06_c
	*/
	{ "ceiling2beam-n", "ceiling2beam_n"},
	{ "tuyau_02_c", "tuyaux_02_c"},
	{ "tuyau_02_n", "tuyaux_02_n"},
	{ "vegetal4_c", "vegetal_04_c"},
	{ "ventilo_c", "ventilateur_c"},
	{ "ventilo_n", "ventilateur_n"},
	{ "cable1c", "cable1_c"},
};

// Scene

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
	billboardsMesh.clean();
	billboardRanges.fill({0,0});
	particleRanges.fill({0,0});

	meshInfos.reset();
	instanceInfos.reset();
	materialInfos.reset();
	lightInfos.reset();
	zoneInfos.reset();

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

	// Might be an inline color.
	if(TextUtilities::hasPrefix(textureName, INTERNAL_TEXTURE_PREFIX)){
		auto tokens = TextUtilities::split(textureName.substr(3), " ", false);
		glm::vec3 color(1.0f);
		color[0] = std::stof(tokens[0]);
		color[1] = std::stof(tokens[1]);
		color[2] = std::stof(tokens[2]);
		tex.images.resize(1);
		Image::generateImageWithColor(tex.images[0], color);
	} else {
		// Find the file on disk.
		for( const fs::path& texturePath : files.texturesList){
			const std::string existingName = texturePath.filename().replace_extension().string();
			if(existingName == textureName){
				tex.images.resize(1);
				tex.images[0].load(texturePath);
				break;
			}
		}

	}

	if(tex.images.empty()){

		// Find a substitution
		auto substitute = texFileSubtitutions.find(textureName);
		if(substitute != texFileSubtitutions.end()){
			Log::info("Substituting %s to %s", substitute->second.c_str(), substitute->first.c_str());
			// Find the file on disk.
			for( const fs::path& texturePath : files.texturesList){
				const std::string existingName = texturePath.filename().replace_extension().string();
				if(existingName == substitute->second){
					tex.images.resize(1);
					tex.images[0].load(texturePath);
					break;
				}
			}
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

void Scene::generate(const World& world, const GameFiles& files){

	clean();

	/// Populate the mesh geometry and corresponding sub-mesh info.
	Log::verbose("Generating meshes...");
	{
		const size_t instanceCount = world.instances().size();
		const size_t objectCount = world.objects().size();
		globalMesh = Mesh(world.name());

		// Estimate the number of meshes ahead, and per-object submeshes indices in the mesh list.
		std::vector<std::vector<uint>> objectMeshIndicesRange;
		objectMeshIndicesRange.resize(objectCount);

		uint meshCount = 0;
		// Find per material type count, to ensure meshes of the same type are consecutive in the list.
		const std::vector<Object::Material>& materials = world.materials();
		std::array<uint, Object::Material::COUNT> meshCountPerMaterial;
		meshCountPerMaterial.fill(0u);

		for(const Object& obj : world.objects()){
			meshCount += (uint)obj.faceSets.size();
			for(const Object::Set& set : obj.faceSets){
				assert(set.material != Object::Material::NO_MATERIAL);
				++meshCountPerMaterial[materials[set.material].type];
			}
		}
		if(meshCount != (meshCountPerMaterial[Object::Material::OPAQUE]
		   + meshCountPerMaterial[Object::Material::DECAL]
		   + meshCountPerMaterial[Object::Material::TRANSPARENT])){
			Log::warning("Unexpected material type for mesh!");
		}
		// Convert per type count to offsets in mesh list.
		std::array<uint, Object::Material::COUNT> meshOffsetPerMaterial;
		meshOffsetPerMaterial[0] = 0;
		for(uint mid = 1; mid < (uint)Object::Material::COUNT; ++mid){
			meshOffsetPerMaterial[mid] = meshOffsetPerMaterial[mid-1] + meshCountPerMaterial[mid-1];
		}
		// Save the original count and offsets for reference.
		for(uint mid = 0; mid < (uint)Object::Material::COUNT; ++mid){
			globalMeshMaterialRanges[mid].firstIndex = meshOffsetPerMaterial[mid];
			globalMeshMaterialRanges[mid].count = meshCountPerMaterial[mid];
		}

		meshInfos = std::make_unique<StructuredBuffer<MeshInfos>>(meshCount, BufferType::STORAGE, "MeshInfos");
		meshDebugInfos.resize(meshCount);

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

				uint& currentMeshId = meshOffsetPerMaterial[materials[set.material].type];
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

				objectMeshIndicesRange[oid].push_back(currentMeshId);
				++currentSetId;
				++currentMeshId;
			}
		}

		// For each mesh of each object, how many instances are there.
		std::vector<std::vector<uint>> perMeshInstanceIndices(meshCount);
		size_t totalInstancesCount = 0u;

		for(uint iid = 0; iid < instanceCount; ++iid){
			// From an object instance, create a set of meshes instances.
			const World::Instance& instance = world.instances()[iid];
			const auto& meshIndicesRange = objectMeshIndicesRange[instance.object];
			for(const size_t mid : meshIndicesRange){
				perMeshInstanceIndices[mid].push_back(iid);
				++totalInstancesCount;
			}
		}

		// Build a list of unrolled instance data (frames...) and update mesh infos.
		instanceInfos = std::make_unique<StructuredBuffer<MeshInstanceInfos>>(totalInstancesCount, BufferType::STORAGE, "InstanceInfos");
		instanceDebugInfos.resize(totalInstancesCount);

		uint currentInstanceId = 0u;
		uint currentMeshId = 0u;
		for(const auto& instanceIndices : perMeshInstanceIndices){
			// Update the corresponding mesh infos
			MeshInfos& infos = (*meshInfos)[currentMeshId];
			infos.firstInstanceIndex = currentInstanceId;
			infos.instanceCount = (uint)instanceIndices.size();

			const MeshCPUInfos& parentDebugInfos = meshDebugInfos[currentMeshId];

			// And populate the instance data.
			for(const auto& iid : instanceIndices){
				const World::Instance& instance = world.instances()[iid];
				// Populate rendering info.
				(*instanceInfos)[currentInstanceId].frame = instance.frame;
				(*instanceInfos)[currentInstanceId].heat = instance.heat;
				// ...and additional CPU info.
				InstanceCPUInfos& debugInfos = instanceDebugInfos[currentInstanceId];
				debugInfos.name = instance.name + "_" + parentDebugInfos.name;
				debugInfos.bbox = parentDebugInfos.bbox.transformed(instance.frame);
				debugInfos.meshIndex = currentMeshId;
				++currentInstanceId;
			}
			++currentMeshId;
		}

		// Update per material type instance count
		for(uint mid = 0; mid < Object::Material::COUNT; ++mid){
			MeshRange& range = globalMeshMaterialRanges[mid];
			range.instanceCount = 0u;
			for(uint oid = range.firstIndex; oid < range.firstIndex + range.count; ++oid){
				MeshInfos& infos = (*meshInfos)[oid];
				range.instanceCount += infos.instanceCount;
			}
		}
	}
	Log::verbose("Done.");

	/// Material and textures.
	Log::verbose("Generating materials...");
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
	Log::verbose("Done.");

	// Lights
	Log::verbose("Generating Lights...");
	{
		const float sceneRadius = computeBoundingBox().getSphere().radius;

		const uint lightsCount = ( uint )world.lights().size();
		lightInfos = std::make_unique<StructuredBuffer<LightInfos>>(lightsCount, BufferType::STORAGE, "LightInfos");
		uint shadowIndex = 0u;
		for(uint i = 0; i < lightsCount; ++i){
			const World::Light& light = world.lights()[i];
			float maxRadius = std::max( light.radius.x, std::max( light.radius.y, light.radius.z ) );
			// Pessimize radius.
			if( light.type == World::Light::DIRECTIONAL || maxRadius == 0.0f ){
				maxRadius = 2.f * sceneRadius;
			}

			const glm::vec3 lightPos = glm::vec3(light.frame[3]);
			LightInfos& info = (*lightInfos)[i];
			info.enabled = true;
			
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
			const float far = 2.f * maxRadius;
			if(light.type == World::Light::SPOT){
				proj = Frustum::perspective(std::max(light.angle, 0.1f), 1.0f, far, near);
			} else if(light.type == World::Light::DIRECTIONAL){
				proj = Frustum::ortho(-light.radius.x, light.radius.x, -light.radius.y, light.radius.y, far, near);
			} else {
				proj = Frustum::perspective(glm::half_pi<float>(), 1.0f, far, near);
				view = glm::mat4(1.0f);
			}
			info.vp = proj * view;

			info.positionAndMaxRadius = glm::vec4( lightPos, maxRadius );

			info.colorAndType = glm::vec4(light.color, float(light.type));
			const glm::vec3 axisX = glm::normalize(glm::vec3(light.frame[0]));
			const glm::vec3 axisY = glm::normalize(glm::vec3(light.frame[1]));
			const glm::vec3 axisZ = glm::normalize(glm::vec3(light.frame[2]));
			info.axisAndRadiusX = glm::vec4(axisX / glm::max(light.radius.x, 1.f), 0.0f);
			info.axisAndRadiusY = glm::vec4(axisY / glm::max(light.radius.y, 1.f), 0.0f);
			info.axisAndRadiusZ = glm::vec4(axisZ / glm::max(light.radius.z, 1.f), 0.0f);
			info.materialIndex = light.material;
		}
	}
	Log::verbose("Done.");
	
	// Zones
	Log::verbose("Generating Zones...");
	{
		const uint zonesCount = ( uint )world.zones().size();
		zoneInfos = std::make_unique<StructuredBuffer<ZoneInfos>>(zonesCount, BufferType::STORAGE, "ZoneInfos");

		for(uint i = 0; i < zonesCount; ++i){
			const World::Zone& zone = world.zones()[i];
			ZoneInfos& info = (*zoneInfos)[i];
			info.ambientColor = zone.ambientColor;
			info.fogColorAndDensity = glm::vec4(glm::vec3(zone.fogColor), zone.fogDensity);
			info.fogParams = zone.fogParams;
			info.bboxMin = glm::vec4(zone.bbox.minis, 0.f);
			info.bboxMax = glm::vec4(zone.bbox.maxis, 0.f);
		}
	}
	Log::verbose("Done.");

	// FXs
	Log::verbose("Generating FXs...");
	{
		const std::array<glm::vec2, 4> uvs = {
			glm::vec2(0.f, 1.f),
			glm::vec2(0.f, 0.f),
			glm::vec2(1.f, 0.f),
			glm::vec2(1.f, 1.f),
		};

		const std::array<glm::vec2, 4> positions = {
			glm::vec2(-0.5f, -0.5f),
			glm::vec2(-0.5f,  0.5f),
			glm::vec2( 0.5f,  0.5f),
			glm::vec2( 0.5f, -0.5f),
		};

		const std::array<uint, 6> indices = {
			0, 2, 1,
			0, 3, 2,
		};

		const size_t approximateQuadCount = world.billboards().size() + 4 * world.particles().size();
		const size_t vertCount = approximateQuadCount * positions.size();
		const size_t indicesCount = approximateQuadCount * indices.size();

		billboardsMesh.positions.reserve(vertCount);
		billboardsMesh.texcoords.reserve(vertCount);
		billboardsMesh.colors.reserve(vertCount);
		billboardsMesh.normals.reserve(vertCount); // Used for material index
		billboardsMesh.tangents.reserve(vertCount);
		billboardsMesh.bitangents.reserve(vertCount);
		billboardsMesh.indices.reserve(indicesCount);
		
		// Billboards
		{
			uint currentBlend = 0;
			particleRanges[currentBlend].firstIndex = (uint)billboardsMesh.indices.size();

			for(const World::Billboard& billboard : world.billboards()){

				if(billboard.blending != currentBlend){
					const uint firstIndex = ( uint )billboardsMesh.indices.size();
					assert(billboard.blending < World::BLEND_COUNT);
					billboardRanges[currentBlend].count = firstIndex - billboardRanges[currentBlend].firstIndex;
					currentBlend = billboard.blending;
					billboardRanges[currentBlend].firstIndex = firstIndex;
				}

				// Generate a quad with vertex colors.
				const glm::mat4 frame = billboard.alignment == World::ALIGN_WORLD ? billboard.frame : glm::mat4(1.0f);
				const glm::vec3 center = glm::vec3(billboard.frame * glm::vec4(0.f, 0.f,0.f, 1.0f));
				std::array<glm::vec3, 4> verts;
				for(int i = 0; i < 4; ++i){
					verts[i] = glm::vec3(frame * glm::vec4(billboard.size * positions[i], 0.f, 1.0f));
				}

				const uint firstVertexIndex = ( uint )billboardsMesh.positions.size();
				billboardsMesh.positions.insert(billboardsMesh.positions.end(), verts.begin(), verts.end());
				billboardsMesh.colors.insert(billboardsMesh.colors.end(), 4, billboard.color);
				billboardsMesh.normals.insert(billboardsMesh.normals.end(), 4, glm::vec3(billboard.material, billboard.alignment, 0.f));
				billboardsMesh.tangents.insert(billboardsMesh.tangents.end(), 4, center);
				billboardsMesh.bitangents.insert(billboardsMesh.bitangents.end(), 4, glm::vec3(1.0f,0.0f,0.0f));
				billboardsMesh.texcoords.insert(billboardsMesh.texcoords.end(), uvs.begin(), uvs.end());

				for(uint ind : indices){
					billboardsMesh.indices.push_back(firstVertexIndex + ind);
				}
			}
			billboardRanges[currentBlend].count = ( uint )billboardsMesh.indices.size() - billboardRanges[currentBlend].firstIndex;

		}
		
		// Append particles in the same mesh.
		{
			uint currentBlend = 0;
			particleRanges[currentBlend].firstIndex = ( uint )billboardsMesh.indices.size();

			for(const World::Emitter& emitter : world.particles()){

				if(emitter.blending != currentBlend){
					const uint firstIndex = ( uint )billboardsMesh.indices.size();
					assert(emitter.blending < World::BLEND_COUNT);
					particleRanges[currentBlend].count = firstIndex - particleRanges[currentBlend].firstIndex;
					currentBlend = emitter.blending;
					particleRanges[currentBlend].firstIndex = firstIndex;
				}
				// 'Interpete' parameters.
				const bool isBoxFilling = emitter.type == 2u;
				uint particleCount = isBoxFilling ? emitter.maxCount : glm::min(emitter.maxCount, (uint)(2.f * emitter.rate));
				float radius = isBoxFilling ? 0.0f : glm::max(emitter.radius, 1.0f);
				glm::vec2 sizeRange = emitter.sizeRange;
				const glm::vec3 velocityScale(0.0f, 0.0f, -0.1f);
				const bool needDrasticReduction = emitter.name.find("pheromone") != std::string::npos;
				if(needDrasticReduction){
					particleCount = 1u;
				}

				for(uint bId = 0; bId < particleCount; ++bId){
					const uint firstVertexIndex = ( uint )billboardsMesh.positions.size();
					// Generate parameters for this quad.
					float size = glm::mix(sizeRange.x, sizeRange.y, Random::Float());
					glm::vec4 color = glm::mix(emitter.colorMin, emitter.colorMax, glm::vec4(Random::Float()));
					glm::vec3 pos = glm::mix(emitter.bbox.minis, emitter.bbox.maxis, Random::Float3());
					// Specific to punctual emitters.
					if(!isBoxFilling){
						pos += radius * Random::sampleBall();
						// Pick a velocity.
						float velocity = glm::mix(emitter.velocityRange.x, emitter.velocityRange.y, Random::Float());
						pos += velocity * velocityScale;
					}
					// Angular rotation around Z axis.
					float angle = glm::mix(emitter.angleRange.x, emitter.angleRange.y, Random::Float());
					angle *= glm::pi<float>() / 180.f;
					const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f,0.0f,1.0f));

					const glm::mat4 billboardFrame = emitter.frame * glm::translate(glm::mat4(1.0f), pos) * rotation;
					// Generate a quad with vertex colors.
					const glm::vec3 center = glm::vec3(billboardFrame * glm::vec4(0.f, 0.f,0.f, 1.0f));
					const glm::mat4 frame = emitter.alignment == World::ALIGN_WORLD ? billboardFrame : glm::mat4(1.0f);

					std::array<glm::vec3, 4> verts;
					for(int i = 0; i < 4; ++i){
						verts[i] = glm::vec3(frame * glm::vec4(size * positions[i], 0.f, 1.0f));
					}

					billboardsMesh.positions.insert(billboardsMesh.positions.end(), verts.begin(), verts.end());
					billboardsMesh.colors.insert(billboardsMesh.colors.end(), 4, color);
					billboardsMesh.normals.insert(billboardsMesh.normals.end(), 4, glm::vec3(emitter.material, emitter.alignment, 0.f));
					billboardsMesh.tangents.insert(billboardsMesh.tangents.end(), 4, center);
					billboardsMesh.bitangents.insert(billboardsMesh.bitangents.end(), 4, glm::vec3(glm::cos(angle), glm::sin(angle), 0.f));
					billboardsMesh.texcoords.insert(billboardsMesh.texcoords.end(), uvs.begin(), uvs.end());

					for(uint ind : indices){
						billboardsMesh.indices.push_back(firstVertexIndex + ind);
					}
				}

			}
			particleRanges[currentBlend].count = ( uint )billboardsMesh.indices.size() - particleRanges[currentBlend].firstIndex;

		}

	}
	Log::verbose("Done.");
}

void Scene::upload(){
	Log::verbose("Uploading...");
	// Send data to the GPU.
	for(auto& tex : textures){
		// Now we have a beautiful texture2D array with all images set.
		// Do not apply gamma correction as there are normal maps in the array.
		// Conversion for color will be done in the shaders.
		tex.upload(Layout::RGBA8, false);
	}
	globalMesh.upload();
	billboardsMesh.upload();
	instanceInfos->upload();
	meshInfos->upload();
	materialInfos->upload();
	lightInfos->upload();
	zoneInfos->upload();

	GPU::registerTextures( textures );
	Log::verbose("Done.");
}

void Scene::load(const fs::path& worldPath, const GameFiles& files){
	
	world = World();
	if( !world.load( worldPath, files.resourcesPath ) ){
		world = World();
		return;
	}
	generate(world, files);
	upload();

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
	generate(world, files);
	upload();
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
