
#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/Common.hpp"
#include "core/DFFParser.hpp"
#include "core/WorldParser.hpp"

#include "system/Window.hpp"
#include "graphics/GPU.hpp"
#include "graphics/Framebuffer.hpp"
#include "input/Input.hpp"
#include "input/ControllableCamera.hpp"
#include "Common.hpp"


// One bit for the shading
#define MODE_SHADING_NONE 0
#define MODE_SHADING_LIGHT 1
// Two bits for the albedo
#define MODE_ALBEDO_UNIFORM 0
#define MODE_ALBEDO_NORMAL 1
#define MODE_ALBEDO_TEXTURE 2

class ViewerConfig : public RenderingConfig {
public:

	ViewerConfig(const std::vector<std::string> & argv) : RenderingConfig(argv) {
		for(const auto & arg : arguments()) {
			const std::string key					= arg.key;
			const std::vector<std::string> & values = arg.values;

			if(key == "path") {
				path = values[0];
			}
		}

		registerSection("Viewer");
		registerArgument("path", "", "Path to the game 'resources' directory");

	}

	fs::path path;
};


Program* loadProgram(const std::string& vertName, const std::string& fragName){
	std::vector<fs::path> names;
	const std::string vertContent = System::getStringWithIncludes(APP_RESOURCE_DIRECTORY / "shaders" / (vertName + ".vert"), names);
	names.clear();
	const std::string fragContent = System::getStringWithIncludes(APP_RESOURCE_DIRECTORY / "shaders" / (fragName + ".frag"), names);

	return new Program(vertName + "_" + fragName, vertContent, fragContent);
}

Program* loadProgram(const std::string& computeName){
	std::vector<fs::path> names;
	const std::string compContent = System::getStringWithIncludes(APP_RESOURCE_DIRECTORY / "shaders" / (computeName + ".comp"), names);

	return new Program(computeName, compContent);
}

struct GameFiles {

	GameFiles(){}

	GameFiles(const fs::path& installPath){

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

		std::sort(modelsList.begin(), modelsList.end());
		std::sort(worldsList.begin(), worldsList.end());
	}


	fs::path resourcesPath;
	fs::path modelsPath;
	fs::path texturesPath;
	fs::path templatesPath;
	fs::path zonesPath;
	fs::path worldsPath;

	std::vector<fs::path> worldsList;
	std::vector<fs::path> modelsList;
	std::vector<fs::path> texturesList;
	std::vector<fs::path> templatesList;

};

struct Scene {


	struct MeshInfos {
		glm::vec4 bboxMin;
		glm::vec4 bboxMax;
		uint indexCount;
		uint instanceCount;
		uint firstIndex;
		uint vertexOffset;
		uint firstInstanceIndex;
		uint materialIndex;
		uint pad0, pad1;
	};

	struct MeshInstanceInfos {
		glm::mat4 frame;
	};

	struct TextureInfos {
		uint index;
		uint layer;
		uint pad0, pad1;
	};

	struct MaterialInfos {
		TextureInfos texture;
	};

	World world;
	Mesh globalMesh{"None"};
	std::vector<Texture> textures;

	std::unique_ptr<StructuredBuffer<MeshInfos>> meshInfosBuffer;
	std::unique_ptr<StructuredBuffer<MeshInstanceInfos>> meshInstanceInfosBuffer;
	std::unique_ptr<StructuredBuffer<MaterialInfos>> materialInfosBuffer;


	void clean(){
		globalMesh.clean();

		meshInfosBuffer.reset();
		meshInstanceInfosBuffer.reset();
		materialInfosBuffer.reset();

		for(Texture& tex : textures ){
			tex.clean();
		}
		textures.clear();
		world = World();
	}

	void loadDFF(const fs::path& dffPath, const GameFiles& files){
		Object obj;
		if(!Dff::load(dffPath, obj)){
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
		globalMesh = Mesh(dffPath.filename().replace_extension());
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

	void load(const fs::path& worldPath, const GameFiles& files){

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
};

int main(int argc, char ** argv) {
	// First, init/parse/load configuration.
	ViewerConfig config(std::vector<std::string>(argv, argv + argc));
	if(config.showHelp()) {
		return 0;
	}

	bool allowEscapeQuit = false;
#ifdef DEBUG
	allowEscapeQuit = true;
#endif

	Window window("eXperience112 viewer", config, allowEscapeQuit);
	const std::string iniPath = (APP_RESOURCE_DIRECTORY / "imgui.ini").string();
	ImGui::GetIO().IniFilename = iniPath.c_str();

	// Try to load configuration.
	GameFiles gameFiles;
	if(!config.path.empty()){
		if(fs::exists(config.path)){
			gameFiles = GameFiles(config.path);
		} else {
			Log::error("Unable to load game installation at path %s", config.path.string().c_str());
		}
	}

	// Interactions
	const double dt = 1.0/120;
	double timer = Input::getTime();
	double remainingTime = 0;

	ControllableCamera camera;
	camera.speed() = 100.0f;
	camera.projection(config.screenResolution[0] / config.screenResolution[1], glm::pi<float>() * 0.4f, 10.f, 10000.0f);
	camera.pose(glm::vec3(0.0f, 0.0f, 100.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	camera.ratio(config.screenResolution[0] / config.screenResolution[1]);

	// Rendering
	std::vector<Program*> programPool;

	Program* defaultQuad = loadProgram("passthrough", "passthrough");
	programPool.push_back(defaultQuad);
	Program* texturedObject = loadProgram("object_basic_texture", "object_basic_texture");
	programPool.push_back(texturedObject);
	Program* coloredObject = loadProgram("object_basic_color", "object_basic_color");
	programPool.push_back(coloredObject);

	Program* texturedInstancedObject = loadProgram("object_basic_texture_instanced", "object_basic_texture_instanced");
	programPool.push_back(texturedInstancedObject);
	Program* drawArgsCompute = loadProgram("draw_arguments_all");
	programPool.push_back(drawArgsCompute);

	struct FrameData {
		glm::mat4 vp{1.0f};
		glm::mat4 ivp{1.0f};
		glm::vec4 color{1.0f};
		int shadingMode = 0;
		int albedoMode = 0;
	};
	UniformBuffer<FrameData> frameInfos(1, 64);
	glm::vec2 renderingRes = config.resolutionRatio * config.screenResolution;
	Framebuffer fb(uint(renderingRes[0]), uint(renderingRes[1]), {Layout::RGBA8, Layout::DEPTH_COMPONENT32F}, "sceneFb");
	std::unique_ptr<Buffer> drawCommands = nullptr;

	// Data storage.
	Scene scene;

	// GUi state
	Mesh boundingBox("bbox");
	enum class ViewerMode {
		MODEL, AREA, WORLD
	};
	ViewerMode viewMode = ViewerMode::MODEL;
	float zoomPct = 100.f;
	glm::vec2 centerPct(50.f, 50.0f);
	int shadingMode = MODE_SHADING_LIGHT;
	int albedoMode = MODE_ALBEDO_TEXTURE;
	int selectedItem = -1;
	int selectedMesh = -1;
	int selectedTexture = -1;
	bool showWireframe = false;
#ifdef DEBUG
	bool showDemoWindow = false;
#endif

	while(window.nextFrame()) {

		// Handle window resize.
		if(Input::manager().resized()) {
			const uint width = uint(Input::manager().size()[0]);
			const uint height = uint(Input::manager().size()[1]);
			config.screenResolution[0] = float(width > 0 ? width : 1);
			config.screenResolution[1] = float(height > 0 ? height : 1);
			// Resources are not resized here, but when the containing ImGui window resizes.

		}

		if(Input::manager().triggered(Input::Key::P)) {
			// Load something.
		}

		// Compute new time.
		double currentTime = Input::getTime();
		double frameTime   = currentTime - timer;
		timer			   = currentTime;

		// Update camera.
		camera.update();

		// First avoid super high frametime by clamping.
		const double frameTimeUpdate = std::min(frameTime, 0.2);
		// Accumulate new frame time.
		remainingTime += frameTimeUpdate;
		// Instead of bounding at dt, we lower our requirement (1 order of magnitude).

		while(remainingTime > 0.2 *  dt) {
			const double deltaTime = std::min(remainingTime, dt);
			// Update physics.
			camera.physics(deltaTime);
			remainingTime -= deltaTime;
		}


		ImGui::DockSpaceOverViewport();

		bool reloaded = false;
		const ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable;
		const ImGuiSelectableFlags selectableTableFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap;

		if(ImGui::BeginMainMenuBar()){

			if(ImGui::BeginMenu("File")){

				if(ImGui::MenuItem("Load...")){
					fs::path newInstallPath;
					if(Window::showDirectoryPicker(fs::path(""), newInstallPath)){
						gameFiles = GameFiles( newInstallPath);
						scene = Scene();
						selectedItem = -1;
						selectedMesh = -1;
						selectedTexture = -1;
						reloaded = true;
					}
				}
				ImGui::Separator();
				if(ImGui::MenuItem("Close")){
					window.perform(Window::Action::Quit);
				}
				ImGui::EndMenu();
			}

			if(ImGui::BeginMenu("Options")){

				if(ImGui::MenuItem("Fullscreen", nullptr, config.fullscreen)){
					window.perform(Window::Action::Fullscreen);
				}
				if(ImGui::MenuItem("Vsync", nullptr, config.vsync)){
					window.perform(Window::Action::Vsync);
				}
#ifdef DEBUG
				ImGui::MenuItem("Show ImGui demo", nullptr, &showDemoWindow);
#endif

				ImGui::EndMenu();
			}
			
			ImGui::EndMainMenuBar();
		}

		// Begin GUI setup.
		if(ImGui::Begin("Files")){

			if(ImGui::BeginTabBar("#FilesTabBar")){

				if(ImGui::BeginTabItem("Models")){
					if(viewMode != ViewerMode::MODEL){
						selectedItem = -1;
						selectedMesh = -1;
						selectedTexture = -1;
						viewMode = ViewerMode::MODEL;
					}

					const unsigned int modelsCount = (uint)gameFiles.modelsList.size();
					ImGui::Text("Found %u models", modelsCount);

					if(ImGui::BeginTable("#ModelsTable", 2, tableFlags)){
						// Header
						ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
						ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
						ImGui::TableSetupColumn("Directory", ImGuiTableColumnFlags_None);
						ImGui::TableHeadersRow();

						// Demonstrate using clipper for large vertical lists
						ImGuiListClipper clipper;
						clipper.Begin(modelsCount);
						while (clipper.Step()) {
							for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++){
								ImGui::PushID(row);
								ImGui::TableNextColumn();

								const fs::path& modelPath = gameFiles.modelsList[row];
								std::string modelName = modelPath.filename().string();
								const std::string modelParent = modelPath.parent_path().filename().string();

								if(selectedItem == row){
									modelName = "* " + modelName;
								}

								if(ImGui::Selectable(modelName.c_str(), selectedItem == row, selectableTableFlags)){
									if(selectedItem != row){
										selectedItem = row;
										scene.clean();
										scene.loadDFF(modelPath, gameFiles);

										const size_t meshCount = scene.meshInfosBuffer->size();
										drawCommands = std::make_unique<Buffer>(meshCount * sizeof(GPU::DrawCommand), BufferType::INDIRECT);

										// Compute the total bounding box.
										BoundingBox modelBox = scene.globalMesh.computeBoundingBox();
										// Center the camera.
										const glm::vec3 center = modelBox.getCentroid();
										const glm::vec3 extent = modelBox.getSize();
										// Keep the camera off the object.
										const float maxExtent = glm::max(extent[0], glm::max(extent[1], extent[2]));
										// Handle case where the object is a flat quad (leves, decals...).
										glm::vec3 offset = std::abs(extent[0]) < 1.0f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
										camera.pose(center + maxExtent * offset, center, glm::vec3(0.0, 1.0f, 0.0f));
										selectedMesh = -1;
										selectedTexture = -1;
									}
								}
								ImGui::TableNextColumn();
								ImGui::Text("%s", modelParent.c_str());

								ImGui::PopID();
							}
						}

						ImGui::EndTable();
					}
					ImGui::EndTabItem();
				}

				if(ImGui::BeginTabItem("Worlds", nullptr)){
					if(viewMode != ViewerMode::WORLD){
						selectedItem = -1;
						selectedMesh = -1;
						selectedTexture = -1;
						viewMode = ViewerMode::WORLD;
					}
					
					const unsigned int worldCount = (uint)gameFiles.worldsList.size();
					ImGui::Text("Found %u worlds", worldCount);

					if(ImGui::BeginTable("#WorldsTable", 2, tableFlags)){
						// Header
						ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
						ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
						ImGui::TableSetupColumn("Directory", ImGuiTableColumnFlags_None);
						ImGui::TableHeadersRow();

						for(int row = 0; row < (int)worldCount; row++){
							ImGui::PushID(row);
							ImGui::TableNextColumn();


							const fs::path& worldPath = gameFiles.worldsList[row];
							std::string worldName = worldPath.filename().string();

							if(selectedItem == row){
								worldName = "* " + worldName;
							}

							if(ImGui::Selectable(worldName.c_str(), selectedItem == row, selectableTableFlags)){
								if(selectedItem != row){
									selectedItem = row;
									scene.clean();
									scene.load(worldPath, gameFiles);
									drawCommands = std::make_unique<Buffer>(scene.meshInfosBuffer->size() * sizeof(GPU::DrawCommand), BufferType::INDIRECT);

									// Compute the total bounding box.
									BoundingBox modelBox;
									const size_t meshCount = scene.meshInfosBuffer->size();
									for(size_t mid = 0; mid < meshCount; ++mid){
										const Scene::MeshInfos& infos = scene.meshInfosBuffer->at(mid);
										const BoundingBox bbox(infos.bboxMin, infos.bboxMax);
										for(size_t iid = 0; iid < infos.instanceCount; ++iid ){
											const size_t iiid = infos.firstInstanceIndex + iid;
											const glm::mat4& frame = scene.meshInstanceInfosBuffer->at(iiid).frame;
											modelBox.merge( bbox.transformed(frame));
										}
									}
									// Center the camera.
									const glm::vec3 center = modelBox.getCentroid();
									const glm::vec3 extent = modelBox.getSize();
									// Keep the camera off the object.
									const float maxExtent = glm::max(extent[0], glm::max(extent[1], extent[2]));
									// Handle case where the object is a flat quad (leves, decals...).
									glm::vec3 offset = std::abs(extent[0]) < 1.0f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
									camera.pose(center + maxExtent * offset, center, glm::vec3(0.0, 1.0f, 0.0f));
									selectedMesh = -1;
									selectedTexture = -1;
								}
							}

							ImGui::TableNextColumn();
							ImGui::Text("");

							ImGui::PopID();
						}

						ImGui::EndTable();
					}
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}

		}
		ImGui::End();

		if(ImGui::Begin("Inspector")){
			if(selectedItem >= 0){
				const std::string itemName = scene.globalMesh.name();
				ImGui::Text("Item: %s (%lu vertices)", itemName.c_str(), scene.globalMesh.positions.size());
				ImGui::SameLine();
				if(ImGui::SmallButton("Deselect")){
					selectedMesh = -1;
				}
				ImVec2 winSize = ImGui::GetContentRegionAvail();
				winSize.y = 0.48f * winSize.y;

				if(ImGui::BeginTable("#MeshList", 3, tableFlags, winSize)){
					// Header
					ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
					ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
					ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_None);
					ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_None);
					ImGui::TableHeadersRow();

					const int meshCount = (int)scene.meshInfosBuffer->size();
					for(int row = 0; row < meshCount; ++row){
						ImGui::TableNextColumn();
						ImGui::PushID(row);

						std::string meshName = itemName + "_part_" + std::to_string(row);
						if(selectedMesh == row){
							meshName = "* " + meshName;
						}

						const Scene::MeshInfos& meshInfos = scene.meshInfosBuffer->at(row);
						if(ImGui::Selectable(meshName.c_str(), selectedMesh == row, selectableTableFlags)){
							if(selectedMesh != row){
								selectedMesh = row;

								// Update bbox mesh.
								BoundingBox bbox(meshInfos.bboxMin, meshInfos.bboxMax);
								boundingBox.clean();
								boundingBox.positions = bbox.getCorners();
								boundingBox.colors.resize(boundingBox.positions.size(), glm::vec3(1.0f, 0.0f, 0.0f));
								// Setup degenerate triangles for each line of a cube.
								boundingBox.indices = {
									0, 1, 0, 0, 2, 0, 1, 3, 1, 2, 3, 2, 4, 5, 4, 4, 6, 4, 5, 7, 5, 6, 7, 6, 1, 5, 1, 0, 4, 0, 2, 6, 2, 3, 7, 3};
								boundingBox.upload();
							}
						}
						ImGui::TableNextColumn();
						ImGui::Text("%u", meshInfos.indexCount / 3u);
						ImGui::TableNextColumn();
						ImGui::Text("%u", meshInfos.materialIndex);

						ImGui::PopID();
					}

					ImGui::EndTable();
				}

				if(ImGui::BeginTable("#TextureList", 3, tableFlags, winSize)){
					// Header
					ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
					ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
					ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None);
					ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_None);
					ImGui::TableHeadersRow();

					const int texCount = (int)scene.textures.size();
					for(int row = 0; row < texCount; ++row){
						ImGui::TableNextColumn();
						ImGui::PushID(row);

						const Texture& tex = scene.textures[row];
						std::string texName = tex.name();
						if(selectedTexture == row){
							texName = "* " + texName;
						}

						if(ImGui::Selectable(texName.c_str(), selectedTexture == row, selectableTableFlags)){
							selectedTexture = row;
							zoomPct = 100.f;
							centerPct = glm::vec2(50.f, 50.0f);
						}

						ImGui::TableNextColumn();
						ImGui::Text("%ux%u", tex.width, tex.height);
						ImGui::TableNextColumn();
						static const std::unordered_map<Image::Compression, const char*> compressionNames = {
							{ Image::Compression::NONE, "BGRA8" },
							{ Image::Compression::BC1, "BC1/DXT1" },
							{ Image::Compression::BC2, "BC2/DXT3" },
							{ Image::Compression::BC3, "BC3/DXT5" },
						};
						ImGui::Text("%s", compressionNames.at(tex.images[0].compressedFormat));
						ImGui::PopID();
					}

					ImGui::EndTable();
				}
			}

		}
		ImGui::End();

		if(ImGui::Begin("Texture")){
			if(selectedTexture >= 0){

				ImGui::PushItemWidth(120);
				ImGui::SliderFloat("Zoom (%)", &zoomPct, 0.0f, 1000.0f);
				ImGui::SameLine();
				ImGui::SliderFloat("X (%)", &centerPct[0], 0.0f, 100.0f);
				ImGui::SameLine();
				ImGui::SliderFloat("Y (%)", &centerPct[1], 0.0f, 100.0f);
				ImGui::PopItemWidth();

				// Adjust the texture display to the window size.
				ImVec2 winSize = ImGui::GetContentRegionAvail();
				winSize.x = std::max(winSize.x, 2.f);
				winSize.y = std::max(winSize.y, 2.f);

				const Texture& tex = scene.textures[selectedTexture];
				const glm::vec2 texCenter = centerPct / 100.f;
				const glm::vec2 texScale(100.0f / std::max(zoomPct, 1.f));
				const glm::vec2 miniUV = texCenter - texScale * 0.5f;
				const glm::vec2 maxiUV = texCenter + texScale * 0.5f;

				ImGui::ImageButton(tex, ImVec2(winSize.x, winSize.y), miniUV, maxiUV, 0, ImVec4(1.0f, 0.0f, 1.0f, 1.0f));
			}
		}
		ImGui::End();

		if(ImGui::Begin("Settings")) {
			int ratioPercentage = int(std::round(config.resolutionRatio * 100.0f));

			ImGui::Text("Rendering at %ux%upx", fb.width(), fb.height());
			if(ImGui::InputInt("Rendering ratio %", &ratioPercentage, 10, 25)) {
				ratioPercentage = glm::clamp(ratioPercentage, 10, 200);

				const float newRatio = (float)ratioPercentage / 100.0f;
				glm::vec2 renderRes(fb.width(), fb.height());
				renderRes = (renderRes / config.resolutionRatio) * newRatio;

				config.resolutionRatio = newRatio;
				fb.resize(uint(renderRes[0]), uint(renderRes[1]));
				camera.ratio(renderRes[0]/renderRes[1]);
			}

			ImGui::Text("Shading"); ImGui::SameLine();
			ImGui::RadioButton("None", &shadingMode, MODE_SHADING_NONE); ImGui::SameLine();
			ImGui::RadioButton("Light", &shadingMode, MODE_SHADING_LIGHT);

			ImGui::Text("Albedo"); ImGui::SameLine();
			ImGui::RadioButton("Color", &albedoMode, MODE_ALBEDO_UNIFORM); ImGui::SameLine();
			ImGui::RadioButton("Normal", &albedoMode, MODE_ALBEDO_NORMAL); ImGui::SameLine();
			ImGui::RadioButton("Texture", &albedoMode, MODE_ALBEDO_TEXTURE);

			ImGui::Checkbox("Wireframe", &showWireframe);
			ImGui::Separator();

			camera.interface();
		}
		ImGui::End();

		if(ImGui::Begin("Main view", nullptr)){
			// Adjust the texture display to the window size.
			ImVec2 winSize = ImGui::GetContentRegionAvail();
			winSize.x = std::max(winSize.x, 2.f);
			winSize.y = std::max(winSize.y, 2.f);

			ImGui::ImageButton(*fb.texture(0), ImVec2(winSize.x, winSize.y), ImVec2(0.0,0.0), ImVec2(1.0,1.0), 0);
			if(ImGui::IsItemHovered()) {
				ImGui::SetNextFrameWantCaptureMouse(false);
				ImGui::SetNextFrameWantCaptureKeyboard(false);
			}

			// If the aspect ratio changed, trigger a resize.
			const float ratioCurr = float(fb.width()) / float(fb.height());
			const float ratioWin = winSize.x / winSize.y;
			// \todo Derive a more robust threshold.
			if(std::abs(ratioWin - ratioCurr) > 0.01f){
				const glm::vec2 renderRes = config.resolutionRatio * glm::vec2(winSize.x, winSize.y);
				fb.resize(renderRes);
				camera.ratio(renderRes[0]/renderRes[1]);
			}
		}
		ImGui::End();

#ifdef DEBUG
		if(showDemoWindow){
			ImGui::ShowDemoWindow();
		}
#endif

		/// Rendering.
		const glm::mat4 vp		   = camera.projection() * camera.view();

		frameInfos.at(0).vp = vp;
		frameInfos.at(0).ivp = glm::transpose(glm::inverse(vp));
		frameInfos.at(0).color = glm::vec4(1.0f);
		frameInfos.at(0).albedoMode = albedoMode;
		frameInfos.at(0).shadingMode = shadingMode;
		frameInfos.upload();



		fb.bind(glm::vec4(0.25f, 0.25f, 0.25f, 1.0f), 1.0f, LoadOperation::DONTCARE);

		if(selectedItem >= 0){


			// Populate drawCommands by using a compute shader (that will later perform culling).
			drawArgsCompute->use();
			drawArgsCompute->buffer(frameInfos, 0);
			drawArgsCompute->buffer(*scene.meshInfosBuffer, 1);
			drawArgsCompute->buffer(*scene.meshInstanceInfosBuffer, 2);
			drawArgsCompute->buffer(*drawCommands, 3);
			GPU::dispatch((uint)scene.meshInfosBuffer->size(), 1, 1);

			fb.bind(glm::vec4(0.25f, 0.25f, 0.25f, 1.0f), 1.0f, LoadOperation::DONTCARE);
			GPU::setViewport(0, 0, fb.width(), fb.height());

			if(selectedMesh >= 0){
				coloredObject->use();
				coloredObject->buffer(frameInfos, 0);
				GPU::setPolygonState(PolygonMode::LINE);
				GPU::setCullState(false);
				GPU::setDepthState(true, TestFunction::LESS, true);
				GPU::setBlendState(false);
				GPU::drawMesh(boundingBox);
			}

			GPU::setPolygonState(PolygonMode::FILL);
			GPU::setCullState(true);
			GPU::setDepthState(true, TestFunction::LESS, true);
			GPU::setBlendState(false);

			texturedInstancedObject->use();
			texturedInstancedObject->buffer(frameInfos, 0);
			texturedInstancedObject->buffer(*scene.meshInfosBuffer, 1);
			texturedInstancedObject->buffer( *scene.meshInstanceInfosBuffer, 2 );
			texturedInstancedObject->buffer( *scene.materialInfosBuffer, 3 );

			GPU::drawIndirectMesh(scene.globalMesh, *drawCommands);


			//if(showWireframe){
				// Temporarily force the mode.
//				frameInfos.at(0).shadingMode = MODE_SHADING_NONE;
//				frameInfos.at(0).albedoMode = MODE_ALBEDO_UNIFORM;
//				frameInfos.upload();
//
//				texturedObject->use();
//				texturedObject->buffer(frameInfos, 0);
//				GPU::setPolygonState(PolygonMode::LINE);
//				for(unsigned int i = 0; i < model.meshes.size(); ++i){
//					if(selectedMesh == -1 || selectedMesh == (int)i){
//						GPU::drawMesh(model.meshes[i]);
//					}
//				}
	//		}


		}

		Framebuffer::backbuffer()->bind(glm::vec4(0.2f, 0.2f, 0.2f, 1.0f), LoadOperation::DONTCARE, LoadOperation::DONTCARE);

	}

	scene.clean();
	for(Program* program : programPool){
		program->clean();
		delete program;
	}

	return 0;
}
