
#include "Scene.hpp"

#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/Common.hpp"

#include "system/Window.hpp"
#include "graphics/GPU.hpp"
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

#define CLUSTER_XY_SIZE 64
#define CLUSTER_Z_COUNT 32

static const std::vector<uint> boxIndices = { 0, 1, 0, 0, 2, 0,
	1, 3, 1, 2, 3, 2,
	4, 5, 4, 4, 6, 4,
	5, 7, 5, 6, 7, 6,
	1, 5, 1, 0, 4, 0,
	2, 6, 2, 3, 7, 3};

static const std::vector<uint> octaIndices = {
	0, 2, 0, 0, 3, 0, 0, 4, 0, 0, 5, 0,
	1, 2, 1, 1, 3, 1, 1, 4, 1, 1, 5, 1,
	2, 4, 2, 2, 5, 2, 3, 4, 3, 3, 5, 3,
	0, 1, 0, 2, 3, 2, 4, 5, 4
};


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


struct FrameData {
	glm::mat4 v{1.0f};
	glm::mat4 p{1.0f};
	glm::mat4 vp{1.0f};
	glm::mat4 vpCulling{1.0f};
	glm::mat4 iv{1.0f};
	glm::mat4 ip{1.0f};
	glm::mat4 nvp{1.0f};
	glm::vec4 resolution{0.0f};
	glm::vec4 color{1.0f};
	glm::vec4 camPos{1.0f};
	glm::vec4 camPlanes{0.0f};

	glm::vec4 ambientColor{0.0f};
	glm::vec4 fogColor{0.0f};
	glm::vec4 fogParams{0.0f};
	float fogDensity = 0.0f;

	// Shading settings.
	uint shadingMode = 0;
	uint albedoMode = 0;
	// Clustering
	uint lightsCount;
	glm::uvec4 clustersSize{1u}; // tile size in w
	glm::vec4 clustersParams{0.0f};
	// Selection data.
	int selectedMesh = -1;
	int selectedInstance= -1;
	int selectedTextureArray= -1;
	int selectedTextureLayer= -1;
};

struct SelectionState {
   int item = -1;
   int mesh = -1;
   int instance = -1;
   int texture = -1;
};

struct AmbientEffects {
	glm::vec4 color = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
	glm::vec4 fogColor = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
	glm::vec4 fogParams = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
	float fogDensity = 0.0f;

	static AmbientEffects mix(const AmbientEffects& a, const AmbientEffects& b, float t){
		AmbientEffects c;
		c.color = glm::mix(a.color, b.color, t);
		c.fogColor = glm::mix(a.fogColor, b.fogColor, t);
		c.fogParams = glm::mix(a.fogParams, b.fogParams, t);
		c.fogDensity = glm::mix(a.fogDensity, b.fogDensity, t);
		return c;
	}
};

struct ProgramInfos {
	Program* program;
	std::vector<std::string> names;

	void use(){
		program->use();
	}
};

#define SHADERS_DIRECTORY (APP_RESOURCE_DIRECTORY / "shaders")

ProgramInfos loadProgram(const std::string& vertName, const std::string& fragName){
	std::vector<fs::path> names;
	const std::string vertContent = System::getStringWithIncludes(SHADERS_DIRECTORY / (vertName + ".vert"), names);
	names.clear();
	const std::string fragContent = System::getStringWithIncludes(SHADERS_DIRECTORY / (fragName + ".frag"), names);

	Program* prog = new Program(vertName + "_" + fragName, vertContent, fragContent);
	return {prog, {vertName, fragName}};
}

ProgramInfos loadProgram(const std::string& computeName){
	std::vector<fs::path> names;
	const std::string compContent = System::getStringWithIncludes(SHADERS_DIRECTORY / (computeName + ".comp"), names);
	Program* prog =  new Program(computeName, compContent);
	return {prog, {computeName}};
}

void reload(ProgramInfos& infos){
	if(infos.program->type() == Program::Type::COMPUTE){
		std::vector<fs::path> names;
		const std::string compContent = System::getStringWithIncludes(SHADERS_DIRECTORY / (infos.names[0] + ".comp"), names);
		infos.program->reload(compContent);
	} else {
		std::vector<fs::path> names;
		const std::string vertContent = System::getStringWithIncludes(SHADERS_DIRECTORY / (infos.names[0] + ".vert"), names);
		names.clear();
		const std::string fragContent = System::getStringWithIncludes(SHADERS_DIRECTORY / (infos.names[1] + ".frag"), names);
		infos.program->reload(vertContent, fragContent);
	}

}

void addLightGizmo(Mesh& mesh, const World::Light& light){
	static const uint circeSubdivs = 64;
	static const float arrowScale = 80.0f;

	// Generate angles if not already done.
	static std::vector<glm::vec2> coordinates;
	if(coordinates.size() != circeSubdivs){
		coordinates.resize(circeSubdivs);
		for(uint a = 0u; a < circeSubdivs; ++a){
			const float angle = (float)a / (float)(circeSubdivs-1) * 360.0f / 180.0f * glm::pi<float>();
			coordinates[a][0] = std::cos(angle);
			coordinates[a][1] = std::sin(angle);
		}
	}

	const uint firstVertexIndex = mesh.positions.size();

	// Always generate a small cross.
	for(uint i = 0; i < 3; ++i){
		const uint iIndex = mesh.positions.size();
		glm::vec3 offset(0.0f);
		offset[i] = 10.0f;
		mesh.positions.push_back(-offset);
		mesh.positions.push_back( offset);
		mesh.indices.push_back(iIndex );
		mesh.indices.push_back(iIndex + 1);
		mesh.indices.push_back(iIndex );
	}

	switch(light.type){
		case World::Light::DIRECTIONAL:
		{
			// Arrow.
			const uint iIndex = mesh.positions.size();
			const float arrowEdgesLength = 0.8f * arrowScale;
			const float arrowEdgesSpread = 0.2f * arrowScale;
			mesh.positions.emplace_back(0.0f, 0.0f, arrowScale);
			mesh.positions.emplace_back(0.0f, 0.0f, 0.0f);
			mesh.positions.emplace_back( arrowEdgesSpread, 0.0f, arrowEdgesLength);
			mesh.positions.emplace_back(-arrowEdgesSpread, 0.0f, arrowEdgesLength);
			mesh.positions.emplace_back(0.0f,  arrowEdgesSpread, arrowEdgesLength);
			mesh.positions.emplace_back(0.0f, -arrowEdgesSpread, arrowEdgesLength);
			for(uint i = 1; i < 6; ++i){
				mesh.indices.push_back(iIndex);
				mesh.indices.push_back(iIndex + i);
				mesh.indices.push_back(iIndex);
			}
			break;
		}
		case World::Light::SPOT:
		{
			// Cone
			const uint iIndex = mesh.positions.size();
			mesh.positions.emplace_back(0.0f, 0.0f, 0.0f);
			mesh.positions.emplace_back(-light.radius.x, -light.radius.y, light.radius.z);
			mesh.positions.emplace_back( light.radius.x, -light.radius.y, light.radius.z);
			mesh.positions.emplace_back( light.radius.x,  light.radius.y, light.radius.z);
			mesh.positions.emplace_back(-light.radius.x,  light.radius.y, light.radius.z);

			for(uint i = 1; i < 5; ++i){
				mesh.indices.push_back(iIndex);
				mesh.indices.push_back(iIndex + i);
				mesh.indices.push_back(iIndex);
			}
			for(uint i = 1; i < 5; ++i){
				const uint iNext = i == 4 ? 1 : (i+1);
				mesh.indices.push_back(iIndex + i);
				mesh.indices.push_back(iIndex + iNext);
				mesh.indices.push_back(iIndex + i);
			}
			break;
		}
		case World::Light::POINT:
		{
			// Three circles of radius 1, along the three axis.
			const uint totalVertCount = 3 * circeSubdivs;
			mesh.positions.reserve(mesh.positions.size() + totalVertCount);
			mesh.indices.reserve(mesh.indices.size() + 3 * totalVertCount);

			for(int i = 0; i < 3; ++i){
				const uint xIndex = (i + 1) % 3;
				const uint yIndex = (i + 2) % 3;
				const uint iIndex = mesh.positions.size();
				for(uint a = 0u; a < circeSubdivs; ++a){
					glm::vec3& p = mesh.positions.emplace_back(0.0f);
					p[xIndex] = coordinates[a][0] * light.radius[xIndex];
					p[yIndex] = coordinates[a][1] * light.radius[yIndex];

					// Degenerate triangle for line rendering.
					if(a != 0){
						mesh.indices.push_back(iIndex + a);
						mesh.indices.push_back(iIndex + a - 1);
						mesh.indices.push_back(iIndex + a);
					}
				}
			}
			break;
		}
		default:
			break;
	}

	// Fill colors.
	const uint vertexFinalCount = mesh.positions.size() - firstVertexIndex;
	mesh.colors.insert(mesh.colors.end(), vertexFinalCount, light.color);
	// Apply light frame.
	for(uint i = 0; i < vertexFinalCount; ++i){
		glm::vec3& p = mesh.positions[firstVertexIndex + i];
		p = glm::vec3(light.frame * glm::vec4(p, 1.0f));
	}
}

void adjustCameraToBoundingBox(ControllableCamera& camera, const BoundingBox& bbox){
	// Center the camera.
	const glm::vec3 center = bbox.getCentroid();
	const glm::vec3 extent = bbox.getSize();
	// Keep the camera off the object.
	const float maxExtent = glm::max(extent[0], glm::max(extent[1], extent[2]));
	// Handle case where the object is a flat quad (leaves, decals...).
	glm::vec3 offset = std::abs(extent[0]) < 1.0f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
	camera.pose(center + maxExtent * offset, center, glm::vec3(0.0, 1.0f, 0.0f));
}

enum SelectionFilter {
	SCENE = 1 << 0,
	MESH = 1 << 1,
	INSTANCE = 1 << 2,
	TEXTURE = 1 << 3,
	OBJECT = MESH | INSTANCE,
	ALL = SCENE | MESH | INSTANCE | TEXTURE
};

void deselect(FrameData& _frame, SelectionState& _state, SelectionFilter _filter){
	if(_filter & SCENE){
		_state.item = -1;
	}
	if(_filter & MESH){
		_frame.selectedMesh = -1;
		_state.mesh = -1;
	}
	if(_filter & INSTANCE){
		_frame.selectedInstance = -1;
		_state.instance = -1;
	}
	if(_filter & TEXTURE){
		_frame.selectedTextureArray = -1;
		_frame.selectedTextureLayer = -1;
		_state.texture = -1;
	}

}

void loadEngineTextures(const GameFiles& gameFiles, Texture& fogXYTexture, Texture& fogZTexture){
	if(gameFiles.texturesPath.empty()){
		return;
	}

	fogXYTexture.clean();
	Image& imgXY = fogXYTexture.images.emplace_back();
	imgXY.load(gameFiles.texturesPath / "commons" / "fog_xy.png");
	fogXYTexture.width = imgXY.width;
	fogXYTexture.height = imgXY.height;
	fogXYTexture.shape = TextureShape::D2;
	fogXYTexture.depth = 1;
	fogXYTexture.levels = 1;
	fogXYTexture.upload(Layout::RGBA8, false);

	fogZTexture.clean();
	Image& imgZ = fogZTexture.images.emplace_back();
	imgZ.load(gameFiles.texturesPath / "commons" / "fog_z.png");
	fogZTexture.width = imgZ.width;
	fogZTexture.height = imgZ.height;
	fogZTexture.shape = TextureShape::D2;
	fogZTexture.depth = 1;
	fogZTexture.levels = 1;
	fogZTexture.upload(Layout::RGBA8, false);
}


uint roundUp(uint a, uint step){
	return std::floor(int(a) - 1) / int(step) + 1;
}

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
	const std::string iniPath = (APP_RESOURCE_DIRECTORY / "imgui.ini").string();

	Window window("eXperience112 viewer", config, allowEscapeQuit);
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

	ControllableCamera camera(ControllableCamera::Mode::FPS);
	camera.speed() = 100.0f;
	camera.projection(config.screenResolution[0] / config.screenResolution[1], glm::pi<float>() * 0.4f, 10.f, 10000.0f);
	camera.pose(glm::vec3(0.0f, 0.0f, 100.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	camera.ratio(config.screenResolution[0] / config.screenResolution[1]);

	// Rendering

	std::vector<ProgramInfos> programPool;

	programPool.push_back(loadProgram("texture_passthrough", "texture_debug"));
	Program* textureQuad = programPool.back().program;

	programPool.push_back(loadProgram("object_color", "object_color"));
	Program* coloredDebugDraw = programPool.back().program;

	programPool.push_back(loadProgram("object_instanced_gbuffer", "object_instanced_gbuffer"));
	Program* gbufferInstancedObject = programPool.back().program;

	programPool.push_back(loadProgram("object_instanced_forward", "object_instanced_forward"));
	Program* forwardInstancedObject = programPool.back().program;

	programPool.push_back(loadProgram("object_instanced_shadow", "object_instanced_shadow"));
	Program* shadowInstancedObject = programPool.back().program;

	programPool.push_back(loadProgram("object_instanced_debug", "object_instanced_debug"));
	Program* debugInstancedObject = programPool.back().program;

	programPool.push_back(loadProgram("object_instanced_selection", "object_instanced_selection"));
	Program* selectionObject = programPool.back().program;

	programPool.push_back(loadProgram("draw_arguments_all"));
	Program* drawArgsCompute = programPool.back().program;

	programPool.push_back(loadProgram("lights_clustering"));
	Program* clustersCompute = programPool.back().program;

	programPool.push_back(loadProgram("lighting_gbuffer"));
	Program* lightingCompute = programPool.back().program;


	UniformBuffer<FrameData> frameInfos(1, 64);
	UniformBuffer<FrameData> shadowInfos(1, 2);
	const glm::uvec2 renderRes(config.resolutionRatio * config.screenResolution);

	// Gbuffer
	Texture sceneColor("sceneColor"), sceneNormal("sceneNormal"), sceneDepth("sceneDepth");
	Texture::setupRendertarget(sceneColor, Layout::RGBA8, renderRes[0], renderRes[1]);
	Texture::setupRendertarget(sceneNormal, Layout::RGBA16F, renderRes[0], renderRes[1]);
	Texture::setupRendertarget(sceneDepth, Layout::DEPTH_COMPONENT32F, renderRes[0], renderRes[1]);

	// Lit result
	Texture sceneLit("sceneLit");
	// No HDR for now
	Texture::setupRendertarget(sceneLit, Layout::RGBA16F, renderRes[0], renderRes[1]);

	Texture textureView("textureViewer");
	Texture::setupRendertarget(textureView, Layout::RGBA8, 512, 512);
	GPU::clearTexture(textureView, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

	Texture selectionColor("selection");
	Texture::setupRendertarget(selectionColor, Layout::RG8, renderRes[0], renderRes[1]);

	Texture shadowMaps("shadowMaps");
	Texture::setupRendertarget(shadowMaps, Layout::DEPTH_COMPONENT32F, 512, 512, 1, TextureShape::Array2D, 16);

	glm::ivec2 clusterDims(CLUSTER_XY_SIZE, CLUSTER_Z_COUNT);

	Texture lightClusters("lightClusters");
	lightClusters.width = roundUp(renderRes[0], clusterDims.x);
	lightClusters.height = roundUp(renderRes[1], clusterDims.x);
	lightClusters.depth = clusterDims.y;
	lightClusters.shape = TextureShape::D3;
	lightClusters.levels = 1;
	GPU::setupTexture(lightClusters, Layout::RGBA32UI, false);

	std::unique_ptr<Buffer> drawCommands = nullptr;
	std::unique_ptr<Buffer> drawInstances = nullptr;

	// Data storage.
	Scene scene;

	// GUi state
	Mesh boundingBox("bbox");
	Mesh debugLights("lights");
	Mesh debugZones("zones");
	enum class ViewerMode {
		MODEL, AREA, WORLD
	};
	ViewerMode viewMode = ViewerMode::MODEL;
	float zoomPct = 100.f;
	glm::vec2 centerPct(50.f, 50.0f);
	int shadingMode = MODE_SHADING_LIGHT;
	int albedoMode = MODE_ALBEDO_TEXTURE;
	SelectionState selected;
	bool showDecals = true;
	bool showTransparents = true;
	bool showOpaques = true;
	bool showFog = true;
	bool freezeCulling = false;
	bool showWireframe = false;
	bool showLights = false;
	bool showZones = false;

	AmbientEffects effects;
	uint currentShadowcastingLight = 0u;
	uint currentShadowMapLayer = 0u;

	Texture fogXYTexture("fogXYMap");
	Texture fogZTexture("fogZMap");
	loadEngineTextures(gameFiles, fogXYTexture, fogZTexture);

#ifdef DEBUG
	bool showDemoWindow = false;
#endif
	bool firstFrame = true;
	bool updateInstanceBoundingBox = false;
	bool scrollToItem = false;

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
			for(ProgramInfos& infos : programPool){
				reload(infos);
			}
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

		const ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable;
		const ImGuiSelectableFlags selectableTableFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap;

		if(ImGui::BeginMainMenuBar()){

			if(ImGui::BeginMenu("File")){

				if(ImGui::MenuItem("Load...")){
					fs::path newInstallPath;
					if(Window::showDirectoryPicker(fs::path(""), newInstallPath)){
						gameFiles = GameFiles( newInstallPath);
						loadEngineTextures(gameFiles, fogXYTexture, fogZTexture);
						scene = Scene();
						deselect(frameInfos[0], selected, SelectionFilter::ALL);
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

				struct TabSettings {
					const char* title;
					const char* names;
					const std::vector<fs::path>* files;
					ViewerMode mode;
					ControllableCamera::Mode camera;
					void (Scene::*load) (const fs::path& filePath, const GameFiles& );
				};

				const std::vector<TabSettings> tabSettings = {
					{ "Models", "models", &gameFiles.modelsList, ViewerMode::MODEL, ControllableCamera::Mode::TurnTable, &Scene::loadFile},
					{ "Areas", "areas", &gameFiles.areasList, ViewerMode::AREA, ControllableCamera::Mode::TurnTable, &Scene::loadFile},
					{ "Worlds", "worlds", &gameFiles.worldsList, ViewerMode::WORLD, ControllableCamera::Mode::FPS, &Scene::load},
				};

				for(const TabSettings& tab : tabSettings){

					if(ImGui::BeginTabItem(tab.title)){
						if(viewMode != tab.mode || firstFrame){
							deselect(frameInfos[0], selected, SelectionFilter::ALL);
							viewMode = tab.mode;
							camera.mode(tab.camera);
						}

						const unsigned int itemsCount = (uint)(*tab.files).size();
						ImGui::Text("Found %u %s", itemsCount, tab.names);

						static ImGuiTextFilter itemFilter;
						itemFilter.Draw();

						if(ImGui::BeginTable("Table", 2, tableFlags)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Directory", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							for(int row = 0; row < (int)itemsCount; row++){

								const fs::path& itemPath = (*tab.files)[row];
								const std::string itemName = itemPath.filename().string();
								if(!itemFilter.PassFilter(itemName.c_str())){
									continue;
								}

								ImGui::PushID(row);
								ImGui::TableNextColumn();

								// Log two levels of hierarchy.
								const fs::path& parentPath = itemPath.parent_path();
								std::string itemParent = parentPath.parent_path().filename().string();
								itemParent += "/" + parentPath.filename().string();

								if(ImGui::Selectable(itemName.c_str(), selected.item == row, selectableTableFlags)){
									if(selected.item != row){
										selected.item = row;

										(scene.*tab.load)(itemPath, gameFiles);
										// Allocate commands buffer.
										const size_t meshCount = scene.meshInfos->size();
										const size_t instanceCount = scene.instanceInfos->size();
										drawCommands = std::make_unique<Buffer>(meshCount * sizeof(GPU::DrawCommand), BufferType::INDIRECT);
										drawInstances = std::make_unique<Buffer>(instanceCount * sizeof(uint), BufferType::STORAGE);

										uint shadowCount = 0u;
										currentShadowcastingLight = 0u;
										currentShadowMapLayer = 0u;
										for(const World::Light& light : scene.world.lights()){
											if(light.shadow){
												++shadowCount;
											}
										}
										shadowCount = std::max(shadowCount, 1u);
										shadowMaps.resize(shadowMaps.width, shadowMaps.height, shadowCount);

										// Center camera
										if(scene.world.cameras().empty()){
											adjustCameraToBoundingBox(camera, scene.computeBoundingBox());
										} else {
											// If a scene camera is available, place ourselves right next to it.
											const World::Camera& refCam =  scene.world.cameras()[0];
											const glm::vec3 pos = glm::vec3(refCam.frame[3]) - glm::vec3(0.0f, 50.0f, 0.0f);
											const glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
											glm::vec3 front = refCam.frame * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
											glm::vec3 right = glm::normalize(glm::cross(glm::normalize(front), up));
											front = glm::normalize(glm::cross(up, right));
											camera.pose(pos, pos + front, up);
										}
										deselect(frameInfos[0], selected, (SelectionFilter)(OBJECT | TEXTURE));

										debugLights.clean();
										debugZones.clean();
										effects = AmbientEffects();
										// Build light debug visualisation.
										if(!scene.world.lights().empty()){
											for(const World::Light& light : scene.world.lights()){
												addLightGizmo(debugLights, light);
											}
											debugLights.upload();
										}
										// Build zones debug visualisation.
										if(!scene.world.zones().empty()){
											for(const World::Zone& zone : scene.world.zones()){
												const uint indexShift = (uint)debugZones.positions.size();
												// Build box.
												const auto corners = zone.bbox.getCorners();
												const std::vector<glm::vec3> colors(corners.size(), 3.0f*zone.ambientColor);
												debugZones.positions.insert(debugZones.positions.end(), corners.begin(), corners.end());
												debugZones.colors.insert(debugZones.colors.end(), colors.begin(), colors.end());
												// Setup degenerate triangles for each line of a octahedron.
												for(const uint ind : boxIndices){
													debugZones.indices.push_back(indexShift + ind);
												}

											}
											debugZones.upload();
										}
									}
								}
								ImGui::TableNextColumn();
								ImGui::Text("%s", itemParent.c_str());

								ImGui::PopID();
							}
							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();

		if(ImGui::Begin("Inspector")){
			if(selected.item >= 0){

				ImGui::Text("%s: %lu vertices, %lu faces",
							scene.world.name().c_str(),
							scene.globalMesh.positions.size(),
							scene.globalMesh.indices.size() / 3 );
				ImGui::SameLine();
				if(ImGui::SmallButton("Deselect")){
					deselect(frameInfos[0], selected, OBJECT);
					boundingBox.clean();
				}

				ImVec2 winSize = ImGui::GetContentRegionAvail();
				winSize.y *= 0.9;
				winSize.y -= ImGui::GetTextLineHeightWithSpacing(); // For search field.


				const std::string meshesTabName = "Meshes (" + std::to_string(scene.meshDebugInfos.size()) + ")###MeshesTab";
				const std::string texturesTabName = "Textures (" + std::to_string(scene.textureDebugInfos.size()) + ")###TexturesTab";
				const std::string lightsTabName = "Lights (" + std::to_string(scene.world.lights().size()) + ")###LightsTab";
				const std::string camerasTabName = "Cameras (" + std::to_string(scene.world.cameras().size()) + ")###CamerasTab";

				if(ImGui::BeginTabBar("InspectorTabbar")){

					if(ImGui::BeginTabItem(meshesTabName.c_str())){

						static ImGuiTextFilter meshFilter;
						meshFilter.Draw();

						if(ImGui::BeginTable("#MeshList", 3, tableFlags, winSize)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							const int rowCount = (int)scene.meshDebugInfos.size();
							for(int row = 0; row < rowCount; ++row){

								const Scene::MeshInfos& meshInfos = (*scene.meshInfos)[row];
								const Scene::MeshCPUInfos& meshDebugInfos = scene.meshDebugInfos[row];
								if(!meshFilter.PassFilter(meshDebugInfos.name.c_str())){
									continue;
								}

								ImGui::TableNextColumn();
								ImGui::PushID(row);
								if(ImGui::Selectable(meshDebugInfos.name.c_str(), selected.mesh == row, selectableTableFlags)){
									if(selected.mesh != row){
										selected.mesh = row;
										frameInfos[0].selectedMesh = row;

										boundingBox.clean();

										// Generate a mesh with bounding boxes of all instances.
										for(uint iid = 0u; iid < meshInfos.instanceCount; ++iid){
											const Scene::InstanceCPUInfos& debugInfos = scene.instanceDebugInfos[meshInfos.firstInstanceIndex + iid];
											const auto corners = debugInfos.bbox.getCorners();
											const uint indexShift = (uint)boundingBox.positions.size();
											boundingBox.positions.insert(boundingBox.positions.end(), corners.begin(), corners.end());
											// Setup degenerate triangles for each line of a cube.
											for(const uint ind : boxIndices){
												boundingBox.indices.push_back(indexShift + ind);
											}

										}
										boundingBox.colors.resize(boundingBox.positions.size(), glm::vec3(1.0f, 0.0f, 0.0f));
										boundingBox.upload();
										deselect(frameInfos[0], selected, INSTANCE);
										adjustCameraToBoundingBox(camera, boundingBox.computeBoundingBox());
									}
								}
								if(scrollToItem && (selected.mesh == row)){
									ImGui::ScrollToItem();
								}

								ImGui::TableNextColumn();
								ImGui::Text("%u", meshInfos.indexCount / 3u);
								ImGui::TableNextColumn();
								ImGui::Text("%u", meshInfos.materialIndex);

								ImGui::PopID();
							}

							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}

					if(ImGui::BeginTabItem(texturesTabName.c_str())){

						static ImGuiTextFilter textureFilter;
						textureFilter.Draw();

						if(ImGui::BeginTable("#TextureList", 3, tableFlags, winSize)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							int rowCount = (int)scene.textureDebugInfos.size();
							for(int row = 0; row < rowCount; ++row){

								const Scene::TextureCPUInfos& debugInfos = scene.textureDebugInfos[row];
								const Texture& arrayTex = scene.textures[debugInfos.data.index];
								if(!textureFilter.PassFilter(debugInfos.name.c_str())){
									continue;
								}

								ImGui::TableNextColumn();
								ImGui::PushID(row);

								if(ImGui::Selectable(debugInfos.name.c_str(), selected.texture == row, selectableTableFlags)){
									selected.texture = row;
									frameInfos[0].selectedTextureArray = debugInfos.data.index;
									frameInfos[0].selectedTextureLayer = debugInfos.data.layer;
									zoomPct = 100.f;
									centerPct = glm::vec2(50.f, 50.0f);
									textureView.resize(arrayTex.width, arrayTex.height);
								}

								ImGui::TableNextColumn();
								ImGui::Text("%ux%u", arrayTex.width, arrayTex.height);
								ImGui::TableNextColumn();
								static const std::unordered_map<Image::Compression, const char*> compressionNames = {
									{ Image::Compression::NONE, "BGRA8" },
									{ Image::Compression::BC1, "BC1/DXT1" },
									{ Image::Compression::BC2, "BC2/DXT3" },
									{ Image::Compression::BC3, "BC3/DXT5" },
								};
								ImGui::Text("%s", compressionNames.at(arrayTex.images[0].compressedFormat));
								ImGui::PopID();
							}
							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}

					if(ImGui::BeginTabItem(lightsTabName.c_str())){

						static ImGuiTextFilter lightFilter;
						lightFilter.Draw();

						if(ImGui::BeginTable("#LightsList", 3, tableFlags, winSize)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							const int rowCount = (int)scene.world.lights().size();
							for(int row = 0; row < rowCount; ++row){

								const World::Light& light = scene.world.lights()[row];
								if(!lightFilter.PassFilter(light.name.c_str())){
									continue;
								}

								ImGui::TableNextColumn();
								ImGui::PushID(row);

								if(ImGui::Selectable(light.name.c_str())){
									const glm::vec3 camCenter = glm::vec3(light.frame[3]);
									const glm::vec3 camPos = camCenter - glm::vec3(light.radius.x, 0.0f, 0.0f);
									camera.pose(camPos, camCenter, glm::vec3(0.0f, 1.0f, 0.0f));
									camera.fov(45.0f * glm::pi<float>() / 180.0f);
								}
								ImGui::TableNextColumn();
								static const std::unordered_map<World::Light::Type, const char*> lightTypeNames = {
									{ World::Light::POINT, "Point" },
									{ World::Light::SPOT, "Spot" },
									{ World::Light::DIRECTIONAL, "Directional" },
								};
								ImGui::Text("%s", lightTypeNames.at(light.type));
								ImGui::TableNextColumn();
								glm::vec3 tmpColor = light.color;
								ImGui::ColorEdit3("##LightColor", &tmpColor[0], ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs);
								ImGui::PopID();
							}
							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}

					if(ImGui::BeginTabItem(camerasTabName.c_str())){

						static ImGuiTextFilter cameraFilter;
						cameraFilter.Draw();

						if(ImGui::BeginTable("#CamerasList", 1, tableFlags, winSize)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							const int rowCount = (int)scene.world.cameras().size();
							for(int row = 0; row < rowCount; ++row){

								const World::Camera& cam = scene.world.cameras()[row];
								if(!cameraFilter.PassFilter(cam.name.c_str())){
									continue;
								}

								ImGui::TableNextColumn();
								ImGui::PushID(row);

								if(ImGui::Selectable(cam.name.c_str())){
									const glm::vec4 camPos = cam.frame * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
									const glm::vec4 camCenter = cam.frame * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
									const glm::vec4 camAbove = cam.frame * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
									camera.pose(glm::vec3(camPos), glm::vec3(camCenter), glm::normalize(glm::vec3(camAbove - camPos)));
									camera.fov(cam.fov);
								}
								ImGui::PopID();
							}
							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}
					ImGui::EndTabBar();
				}
			}
		}
		ImGui::End();

		const std::string instancesTabName = "Instances (" + std::to_string(scene.instanceDebugInfos.size()) + ")###Instances";
		if(ImGui::Begin(instancesTabName.c_str())){
			if(selected.item >= 0){
				ImVec2 winSize = ImGui::GetContentRegionAvail();
				if(ImGui::BeginTable("#InstanceList", 1, tableFlags, winSize)){
					// Header
					ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
					ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
					ImGui::TableHeadersRow();

					// Two modes, depending on if a mesh is selected above or not.
					int startRow = 0;
					int rowCount = (int)scene.instanceDebugInfos.size();
					if(selected.mesh >= 0){
					   startRow = (int)(*scene.meshInfos)[selected.mesh].firstInstanceIndex;
					   rowCount = (int)(*scene.meshInfos)[selected.mesh].instanceCount;
					}

					ImGuiListClipper clipper;
					clipper.Begin(rowCount);

					while(clipper.Step()){
						for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row){
							ImGui::TableNextColumn();
							ImGui::PushID(row);

							const int fullInstanceIndex = startRow + row;
							const Scene::InstanceCPUInfos& debugInfos = scene.instanceDebugInfos[fullInstanceIndex];

							if(ImGui::Selectable(debugInfos.name.c_str(), selected.instance == fullInstanceIndex, selectableTableFlags)){
								if(selected.instance != fullInstanceIndex){
									selected.instance = fullInstanceIndex;
									updateInstanceBoundingBox = true;
									const BoundingBox& bbox = scene.instanceDebugInfos[selected.instance].bbox;
									adjustCameraToBoundingBox(camera, bbox);
								}
							}

							if(scrollToItem && (selected.instance == fullInstanceIndex)){
								ImGui::ScrollToItem();
							}
							ImGui::PopID();
						}
					}
				    ImGui::EndTable();
			   }
			}
		}
		ImGui::End();

		if(ImGui::Begin("Texture")){
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

			const glm::vec2 texCenter = centerPct / 100.f;
			const glm::vec2 texScale(100.0f / std::max(zoomPct, 1.f));
			const glm::vec2 miniUV = texCenter - texScale * 0.5f;
			const glm::vec2 maxiUV = texCenter + texScale * 0.5f;

			ImGui::ImageButton(textureView, ImVec2(winSize.x, winSize.y), miniUV, maxiUV, 0, ImVec4(1.0f, 0.0f, 1.0f, 1.0f));

		}
		ImGui::End();

		if(ImGui::Begin("Settings")) {
			int ratioPercentage = int(std::round(config.resolutionRatio * 100.0f));

			const uint w = sceneLit.width;
			const uint h = sceneLit.height;
			ImGui::Text("Rendering at %ux%upx", w, h);
			if(ImGui::InputInt("Rendering ratio %", &ratioPercentage, 10, 25)) {
				ratioPercentage = glm::clamp(ratioPercentage, 10, 200);

				const float newRatio = (float)ratioPercentage / 100.0f;

				glm::vec2 renderRes = (glm::vec2(w, h) / config.resolutionRatio) * newRatio;

				config.resolutionRatio = newRatio;
				sceneColor.resize(renderRes);
				sceneNormal.resize(renderRes);
				sceneDepth.resize(renderRes);
				sceneLit.resize(renderRes);
				lightClusters.resize(roundUp(renderRes[0], clusterDims.x), roundUp(renderRes[1], clusterDims.x), clusterDims.y);
				camera.ratio(renderRes[0]/renderRes[1]);
			}
			if(ImGui::SliderInt2("Cluster params", &clusterDims[0], 2, 128)){
				lightClusters.resize(roundUp(sceneLit.width, clusterDims.x), roundUp(sceneLit.height, clusterDims.x), clusterDims.y);
			}

			ImGui::Text("Shading"); ImGui::SameLine();
			ImGui::RadioButton("None", &shadingMode, MODE_SHADING_NONE); ImGui::SameLine();
			ImGui::RadioButton("Light", &shadingMode, MODE_SHADING_LIGHT);

			ImGui::Text("Albedo"); ImGui::SameLine();
			ImGui::RadioButton("Color", &albedoMode, MODE_ALBEDO_UNIFORM); ImGui::SameLine();
			ImGui::RadioButton("Normal", &albedoMode, MODE_ALBEDO_NORMAL); ImGui::SameLine();
			ImGui::RadioButton("Texture", &albedoMode, MODE_ALBEDO_TEXTURE);

			if(ImGui::ArrowButton("VisibilityArrow", ImGuiDir_Down)){
				ImGui::OpenPopup("visibilityPopup");
			}

			if(ImGui::BeginPopup("visibilityPopup")){
				ImGui::Checkbox("Opaques",  &showOpaques);
				ImGui::Checkbox("Decals", &showDecals);
				ImGui::Checkbox("Transparents", &showTransparents);
				ImGui::Checkbox("Fog", &showFog);
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			ImGui::Text("Visibility");
			ImGui::SameLine();
			ImGui::Checkbox("Wireframe", &showWireframe);
			ImGui::SameLine();
			ImGui::Checkbox("Lights", &showLights);
			ImGui::SameLine();
			ImGui::Checkbox("Zones", &showZones);

			ImGui::Checkbox("Freeze culling", &freezeCulling);
			ImGui::SameLine();
			if(ImGui::Button("Reset camera")){
				camera.reset();
				adjustCameraToBoundingBox(camera, scene.computeBoundingBox());
			}
			ImGui::Separator();
			camera.interface();
		}
		ImGui::End();

		glm::vec4 mainViewport(0.0f);

		if(ImGui::Begin("Main view", nullptr)){
			// Adjust the texture display to the window size.
			ImVec2 winSize = ImGui::GetContentRegionAvail();
			winSize.x = std::max(winSize.x, 2.f);
			winSize.y = std::max(winSize.y, 2.f);
			mainViewport.x = ImGui::GetWindowPos().x + ImGui::GetCursorPos().x;
			mainViewport.y = ImGui::GetWindowPos().y + ImGui::GetCursorPos().y;
			mainViewport.z = winSize.x;
			mainViewport.w = winSize.y;

			ImGui::ImageButton(sceneLit, ImVec2(winSize.x, winSize.y), ImVec2(0.0,0.0), ImVec2(1.0,1.0), 0);
			if(ImGui::IsItemHovered()) {
				ImGui::SetNextFrameWantCaptureMouse(false);
				ImGui::SetNextFrameWantCaptureKeyboard(false);
			}

			// If the aspect ratio changed, trigger a resize.
			const float ratioCurr = float(sceneLit.width) / float(sceneLit.height);
			const float ratioWin = winSize.x / winSize.y;
			// \todo Derive a more robust threshold.
			if(std::abs(ratioWin - ratioCurr) > 0.01f){
				const glm::vec2 renderRes = config.resolutionRatio * glm::vec2(winSize.x, winSize.y);
				sceneColor.resize(renderRes);
				sceneNormal.resize(renderRes);
				sceneDepth.resize(renderRes);
				sceneLit.resize(renderRes);
				lightClusters.resize(roundUp(renderRes[0], clusterDims.x), roundUp(renderRes[1], clusterDims.x), clusterDims.y);
				camera.ratio(renderRes[0]/renderRes[1]);
			}
		}
		ImGui::End();

#ifdef DEBUG
		if(ImGui::Begin("Debug view", nullptr)){
			// Adjust the texture display to the window size.
			ImVec2 winSize = ImGui::GetContentRegionAvail();
			winSize.x = std::max(winSize.x, 2.f);
			winSize.y = std::max(winSize.y, 2.f);

			ImGui::ImageButton(selectionColor, ImVec2(winSize.x, winSize.y), ImVec2(0.0,0.0), ImVec2(1.0,1.0), 0);
		}
		ImGui::End();
		
		if(showDemoWindow){
			ImGui::ShowDemoWindow();
		}
#endif
		/// Rendering
		// Update ambient effects.
		AmbientEffects newEffects = effects;
		float minDist = FLT_MAX;
		for(const World::Zone& zone : scene.world.zones()){
			float dist = zone.bbox.distance(camera.position());
			if(dist < minDist){
				minDist = dist;
				newEffects.color = zone.ambientColor;
				newEffects.fogParams = zone.fogParams;
				newEffects.fogColor = zone.fogColor;
				newEffects.fogDensity = zone.fogDensity;
			}
		}
		if(!showFog){
			newEffects.fogDensity = 0.0f;
			newEffects.fogParams = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
		}
		// Interpolate between current and new
		effects = AmbientEffects::mix(effects, newEffects, 0.1f);

		// Camera.
		const glm::mat4 vp		   = camera.projection() * camera.view();
		frameInfos[0].v = camera.view();
		frameInfos[0].p = camera.projection();
		frameInfos[0].vp = vp;
		// Only update the culling VP if needed.
		if(!freezeCulling){
			frameInfos[0].vpCulling = vp;
		}
		frameInfos[0].iv = glm::inverse(frameInfos[0].v);
		frameInfos[0].ip = glm::inverse(frameInfos[0].p);
		frameInfos[0].nvp = glm::transpose(glm::inverse(frameInfos[0].vp));
		frameInfos[0].resolution = glm::vec4(sceneLit.width, sceneLit.height, 0u, 0u);

		frameInfos[0].ambientColor = effects.color;
		frameInfos[0].fogParams = effects.fogParams;
		frameInfos[0].fogColor = effects.fogColor;
		frameInfos[0].fogDensity = effects.fogDensity;

		frameInfos[0].color = glm::vec4(1.0f);
		frameInfos[0].camPos = glm::vec4(camera.position(), 1.0f);
		const glm::vec2 nearFar = camera.clippingPlanes();
		frameInfos[0].camPlanes = glm::vec4(nearFar.x, nearFar.y / nearFar.x,
											(nearFar.y - nearFar.x)/(nearFar.x*nearFar.y),
											1.0f/nearFar.y );

		frameInfos[0].albedoMode = albedoMode;
		frameInfos[0].shadingMode = shadingMode;
		frameInfos[0].lightsCount = uint(scene.world.lights().size());
		frameInfos[0].clustersSize = glm::uvec4(lightClusters.width, lightClusters.height, lightClusters.depth, clusterDims.x);
		const float logRatio = float(clusterDims.y) / std::log(nearFar.y / nearFar.x);
		frameInfos[0].clustersParams = glm::vec4(logRatio, std::log(nearFar.x) * logRatio, 0.0f, 0.0f);

		frameInfos.upload();

		if(selected.item >= 0){
			// Bruteforce shadow map once per frame.
			const uint lightsCount = scene.world.lights().size();
			
			if(currentShadowcastingLight < lightsCount && currentShadowMapLayer < shadowMaps.depth){
				// Find the next shadow casting light.
				for(; currentShadowcastingLight < lightsCount; ++currentShadowcastingLight){
					if(scene.world.lights()[currentShadowcastingLight].shadow){
						break;
					}
				}

				const Scene::LightInfos& infos = (*scene.lightInfos)[currentShadowcastingLight];

				// Shadow map projection
				shadowInfos[0].vp = infos.vp;
				shadowInfos[0].vpCulling = infos.vp;
				shadowInfos.upload();

				// Draw commands for the shadow maps.
				drawArgsCompute->use();
				drawArgsCompute->buffer(shadowInfos, 0);
				drawArgsCompute->buffer(*scene.meshInfos, 1);
				drawArgsCompute->buffer(*scene.instanceInfos, 2);
				drawArgsCompute->buffer(*drawCommands, 3);
				drawArgsCompute->buffer(*drawInstances, 4);
				GPU::dispatch((uint)scene.meshInfos->size(), 1, 1);

				GPU::bindFramebuffer(currentShadowMapLayer, 0, 0.0f, LoadOperation::DONTCARE, LoadOperation::DONTCARE, &shadowMaps, nullptr, nullptr, nullptr, nullptr);
				GPU::setViewport(shadowMaps);

				GPU::setPolygonState(PolygonMode::FILL);
				GPU::setCullState(true);
				GPU::setDepthState(true, TestFunction::GEQUAL, true);
				GPU::setBlendState(false);
				GPU::setColorState(false, false, false, false);

				shadowInstancedObject->use();
				shadowInstancedObject->buffer(shadowInfos, 0);
				shadowInstancedObject->buffer(*scene.meshInfos, 1);
				shadowInstancedObject->buffer(*scene.instanceInfos, 2);
				shadowInstancedObject->buffer(*scene.materialInfos, 3);
				shadowInstancedObject->buffer(*drawInstances, 4);

				for(uint mid = 0; mid < scene.meshInfos->size(); ++mid){
					const uint materialIndex = (*scene.meshInfos)[mid].materialIndex;
					if((*scene.materialInfos)[materialIndex].type != Object::Material::OPAQUE){
						continue;
					}
					GPU::drawIndirectMesh(scene.globalMesh, *drawCommands, mid);
				}
				++currentShadowMapLayer;
				++currentShadowcastingLight;
			}

			// Populate drawCommands and drawInstancesby using a compute shader that performs culling.
			{
				drawArgsCompute->use();
				drawArgsCompute->buffer(frameInfos, 0);
				drawArgsCompute->buffer(*scene.meshInfos, 1);
				drawArgsCompute->buffer(*scene.instanceInfos, 2);
				drawArgsCompute->buffer(*drawCommands, 3);
				drawArgsCompute->buffer(*drawInstances, 4);
				GPU::dispatch((uint)scene.meshInfos->size(), 1, 1);

				clustersCompute->use();
				clustersCompute->buffer(frameInfos, 0);
				clustersCompute->buffer(*scene.lightInfos, 1);
				clustersCompute->texture(lightClusters, 0);
				// We need one thread per cluster cell.
				const glm::uvec3 groupSize = clustersCompute->size();
				const uint cw = roundUp(lightClusters.width, groupSize.x);
				const uint ch = roundUp(lightClusters.height, groupSize.y);
				const uint cd = roundUp(lightClusters.depth, groupSize.z);
				GPU::dispatch(cw, ch, cd);
			}

			{
				// Determine clearing color by finding the closest area.
				glm::vec4 clearColor = effects.fogColor;
				clearColor[3] = 0.0f;

				GPU::bind(sceneColor, sceneNormal, sceneDepth, clearColor, 0.0f, LoadOperation::DONTCARE);
				GPU::setViewport(sceneColor);

				GPU::setPolygonState(PolygonMode::FILL);
				GPU::setCullState(true);
				GPU::setDepthState(true, TestFunction::GEQUAL, true);
				GPU::setBlendState(false);
				GPU::setColorState(true, true, true, true);

				gbufferInstancedObject->use();
				gbufferInstancedObject->buffer(frameInfos, 0);
				gbufferInstancedObject->buffer(*scene.meshInfos, 1);
				gbufferInstancedObject->buffer(*scene.instanceInfos, 2);
				gbufferInstancedObject->buffer(*scene.materialInfos, 3);
				gbufferInstancedObject->buffer(*drawInstances, 4);

				if(showOpaques){
					for(uint mid = 0; mid < scene.meshInfos->size(); ++mid){
						const uint materialIndex = (*scene.meshInfos)[mid].materialIndex;
						if((*scene.materialInfos)[materialIndex].type != Object::Material::OPAQUE){
							continue;
						}
						GPU::drawIndirectMesh(scene.globalMesh, *drawCommands, mid);
					}
				}

				// Render decals on top with specific blending mode.
				if(showDecals){
					// Decal textures seem to have no alpha channel at all. White is used for background. Use min blending.
					// Alternative possibility: src * dst, but this makes some decals appear as black squares.
					GPU::setDepthState(true, TestFunction::GEQUAL, false);
					GPU::setCullState(true);
					GPU::setBlendState(true, BlendEquation::MIN, BlendFunction::ONE, BlendFunction::ONE);
					for(uint mid = 0; mid < scene.meshInfos->size(); ++mid){
						const uint materialIndex = (*scene.meshInfos)[mid].materialIndex;
						if((*scene.materialInfos)[materialIndex].type != Object::Material::DECAL){
							continue;
						}
						GPU::drawIndirectMesh(scene.globalMesh, *drawCommands, mid);
					}
				}
			}

			// Lighting
			{
				lightingCompute->use();
				lightingCompute->buffer(frameInfos, 0);
				lightingCompute->buffer(*scene.lightInfos, 1);
				lightingCompute->buffer(*scene.materialInfos, 2);

				lightingCompute->texture(sceneColor, 0);
				lightingCompute->texture(sceneNormal, 1);
				lightingCompute->texture(sceneDepth, 2);
				lightingCompute->texture(sceneLit, 3);

				lightingCompute->texture(fogXYTexture, 4);
				lightingCompute->texture(fogZTexture, 5);
				lightingCompute->texture(lightClusters, 6);
				lightingCompute->texture(shadowMaps, 7);

				const glm::uvec3 groupSize = clustersCompute->size();
				const uint cw = roundUp(sceneLit.width, groupSize.x);
				const uint ch = roundUp(sceneLit.height, groupSize.y);
				GPU::dispatch(cw, ch, 1u);
			}

			{
				GPU::bind(sceneLit, sceneDepth, LoadOperation::LOAD, LoadOperation::LOAD, LoadOperation::DONTCARE);
				GPU::setViewport(sceneLit);

				if(showTransparents){
					// Alternative possibility: real alpha blend and object sorting.
					GPU::setDepthState(true, TestFunction::GEQUAL, false);
					GPU::setCullState(false);
					GPU::setBlendState(true, BlendEquation::ADD, BlendFunction::SRC_ALPHA, BlendFunction::ONE_MINUS_SRC_ALPHA);
					GPU::setColorState(true, true, true, false);

					forwardInstancedObject->use();
					forwardInstancedObject->buffer(frameInfos, 0);
					forwardInstancedObject->buffer(*scene.meshInfos, 1);
					forwardInstancedObject->buffer(*scene.instanceInfos, 2);
					forwardInstancedObject->buffer(*scene.materialInfos, 3);
					forwardInstancedObject->buffer(*drawInstances, 4);
					forwardInstancedObject->buffer(*scene.lightInfos, 5);
					forwardInstancedObject->texture(fogXYTexture, 0);
					forwardInstancedObject->texture(fogZTexture, 1);
					forwardInstancedObject->texture(lightClusters, 2);
					forwardInstancedObject->texture(shadowMaps, 3);


					for(uint mid = 0; mid < scene.meshInfos->size(); ++mid){
						const uint materialIndex = (*scene.meshInfos)[mid].materialIndex;
						if((*scene.materialInfos)[materialIndex].type != Object::Material::TRANSPARENT){
							continue;
						}
						GPU::drawIndirectMesh(scene.globalMesh, *drawCommands, mid);
					}
				}

				// Debug view.

				GPU::setPolygonState( PolygonMode::LINE );
				GPU::setCullState( false );
				GPU::setDepthState( true, TestFunction::GEQUAL, false );
				GPU::setBlendState( false );
				GPU::setColorState(true, true, true, true);

				if(showWireframe){
					debugInstancedObject->use();
					debugInstancedObject->buffer(frameInfos, 0);
					debugInstancedObject->buffer(*scene.meshInfos, 1);
					debugInstancedObject->buffer(*scene.instanceInfos, 2);
					debugInstancedObject->buffer(*scene.materialInfos, 3);
					debugInstancedObject->buffer(*drawInstances, 4);

					for(uint mid = 0; mid < scene.meshInfos->size(); ++mid){
						GPU::drawIndirectMesh(scene.globalMesh, *drawCommands, mid);
					}
				}

				GPU::bind(sceneLit, sceneDepth, LoadOperation::LOAD, LoadOperation::LOAD, LoadOperation::DONTCARE);
				GPU::setViewport(sceneLit);
				coloredDebugDraw->use();
				coloredDebugDraw->buffer( frameInfos, 0 );

				if((selected.mesh >= 0 || selected.instance >= 0) && !boundingBox.indices.empty()){
					GPU::drawMesh( boundingBox );
				}
				if(showLights && !debugLights.indices.empty()){
					GPU::drawMesh( debugLights );
				}
				if(showZones && !debugZones.indices.empty()){
					GPU::drawMesh( debugZones );
				}
			}

			if(Input::manager().released(Input::Mouse::Right)){

				// Compute mouse coordinates.
				glm::vec2 mousePos = Input::manager().mouse();
				// Convert to pixels
				const glm::vec2 winSize = Input::manager().size();
				mousePos *= winSize / Input::manager().density();
				// Scale to main viewport.
				mousePos = (mousePos - glm::vec2(mainViewport.x, mainViewport.y)) / glm::vec2(mainViewport.z, mainViewport.w);
				// Check that we are in the viewport.
				if(glm::all(glm::lessThan(mousePos, glm::vec2(1.0f))) && glm::all(glm::greaterThan(mousePos, glm::vec2(0.0f)))){

					// Render to selection ID texture
					{
						selectionColor.resize(sceneLit.width, sceneLit.height);
						GPU::setViewport(selectionColor);
						GPU::bind(selectionColor, sceneDepth, glm::vec4(0.0f), LoadOperation::LOAD, LoadOperation::DONTCARE);

						GPU::setPolygonState(PolygonMode::FILL);
						GPU::setCullState(true, Faces::BACK);
						GPU::setDepthState(true, TestFunction::EQUAL, false);
						GPU::setBlendState(false);

						selectionObject->use();
						selectionObject->buffer(frameInfos, 0 );
						selectionObject->buffer(*scene.meshInfos, 1);
						selectionObject->buffer(*scene.instanceInfos, 2);
						selectionObject->buffer(*scene.materialInfos, 3);
						selectionObject->buffer(*drawInstances, 4);

						for(uint mid = 0; mid < scene.meshInfos->size(); ++mid){
							GPU::drawIndirectMesh(scene.globalMesh, *drawCommands, mid);
						}
					}

					const glm::vec2 texSize(selectionColor.width, selectionColor.height);
					glm::uvec2 readbackCoords = mousePos * texSize;
					readbackCoords = glm::min(readbackCoords, glm::uvec2(texSize) - 2u);
					GPU::downloadTextureAsync( selectionColor, readbackCoords, glm::uvec2(2u), 1, [&selected, &updateInstanceBoundingBox, &scene](const Texture& result){
						const auto& pixels = result.images[0].pixels;

						uint index = uint(pixels[0]) + (uint(pixels[1] ) << 8u);
						if(index != 0){
							selected.instance = index-1;
							selected.mesh = scene.instanceDebugInfos[selected.instance].meshIndex;
							updateInstanceBoundingBox = true;
						}
					});
				}
			}


		} else {
			GPU::clearTexture(sceneLit, glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
		}

		if(selected.texture >= 0){
			GPU::setViewport(textureView);
			GPU::bind(textureView, glm::vec4(1.0f, 0.0f, 0.5f, 1.0f));
			GPU::setDepthState(false);
			GPU::setCullState(true);
			GPU::setPolygonState(PolygonMode::FILL);
			GPU::setBlendState(true, BlendEquation::ADD, BlendFunction::SRC_ALPHA, BlendFunction::ONE_MINUS_SRC_ALPHA);
			textureQuad->use();
			textureQuad->buffer(frameInfos, 0);
			GPU::drawQuad();
		}

		scrollToItem = false;
		if(updateInstanceBoundingBox){
			frameInfos[0].selectedInstance = selected.instance;
			frameInfos[0].selectedMesh = selected.mesh;
			// Generate a mesh with bounding boxes of the instances.
			boundingBox.clean();
			const auto corners = scene.instanceDebugInfos[selected.instance].bbox.getCorners();
			boundingBox.positions = corners;
			// Setup degenerate triangles for each line of a cube.
			boundingBox.indices = boxIndices;
			boundingBox.colors.resize(boundingBox.positions.size(), glm::vec3(1.0f, 0.0f, 0.0f));
			boundingBox.upload();
			updateInstanceBoundingBox = false;
			scrollToItem = true;
		}

		window.bind(glm::vec4(0.2f, 0.2f, 0.2f, 1.0f), LoadOperation::DONTCARE, LoadOperation::DONTCARE);
		
		firstFrame = false;
	}

	scene.clean();
	for(ProgramInfos& infos : programPool){
		infos.program->clean();
		delete infos.program;
	}

	return 0;
}
