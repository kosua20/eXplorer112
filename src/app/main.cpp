
#include "Scene.hpp"

#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/Common.hpp"

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


struct FrameData {
	glm::mat4 vp{1.0f};
	glm::mat4 vpCulling{1.0f};
	glm::mat4 ivp{1.0f};
	glm::vec4 color{1.0f};
	glm::vec4 camPos{1.0f};
	// Shading settings.
	uint shadingMode = 0;
	uint albedoMode = 0;
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

	ControllableCamera camera(ControllableCamera::Mode::FPS);
	camera.speed() = 100.0f;
	camera.projection(config.screenResolution[0] / config.screenResolution[1], glm::pi<float>() * 0.4f, 10.f, 10000.0f);
	camera.pose(glm::vec3(0.0f, 0.0f, 100.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	camera.ratio(config.screenResolution[0] / config.screenResolution[1]);

	// Rendering
	std::vector<Program*> programPool;

	Program* textureQuad = loadProgram("texture_passthrough", "texture_debug");
	programPool.push_back(textureQuad);

	Program* coloredDebugDraw = loadProgram("object_color", "object_color");
	programPool.push_back(coloredDebugDraw);

	Program* texturedInstancedObject = loadProgram("object_instanced_texture", "object_instanced_texture");
	programPool.push_back(texturedInstancedObject);

	Program* debugInstancedObject = loadProgram("object_instanced_debug", "object_instanced_debug");
	programPool.push_back(debugInstancedObject);

	Program* drawArgsCompute = loadProgram("draw_arguments_all");
	programPool.push_back(drawArgsCompute);


	UniformBuffer<FrameData> frameInfos(1, 64);
	glm::vec2 renderingRes = config.resolutionRatio * config.screenResolution;
	Framebuffer fb(uint(renderingRes[0]), uint(renderingRes[1]), {Layout::RGBA8, Layout::DEPTH_COMPONENT32F}, "sceneRender");
	Framebuffer textureFramebuffer(512, 512, {Layout::RGBA8}, "textureViewer");
	std::unique_ptr<Buffer> drawCommands = nullptr;
	std::unique_ptr<Buffer> drawInstances = nullptr;

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
	SelectionState selected;
	bool showDecals = true;
	bool showTransparents = true;
	bool showOpaques = true;
	bool freezeCulling = false;
	bool showWireframe = false;
#ifdef DEBUG
	bool showDemoWindow = false;
#endif
	bool firstFrame = true;

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

		const ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable;
		const ImGuiSelectableFlags selectableTableFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap;

		if(ImGui::BeginMainMenuBar()){

			if(ImGui::BeginMenu("File")){

				if(ImGui::MenuItem("Load...")){
					fs::path newInstallPath;
					if(Window::showDirectoryPicker(fs::path(""), newInstallPath)){
						gameFiles = GameFiles( newInstallPath);
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

						if(ImGui::BeginTable("Table", 2, tableFlags)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Directory", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							ImGuiListClipper clipper;
							clipper.Begin(itemsCount);
							while (clipper.Step()) {
								for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++){
									ImGui::PushID(row);
									ImGui::TableNextColumn();

									const fs::path& itemPath = (*tab.files)[row];
									const std::string itemName = itemPath.filename().string();
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
											// Center camera
											adjustCameraToBoundingBox(camera, scene.computeBoundingBox());
											deselect(frameInfos[0], selected, (SelectionFilter)(OBJECT | TEXTURE));
										}
									}
									ImGui::TableNextColumn();
									ImGui::Text("%s", itemParent.c_str());

									ImGui::PopID();
								}
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

				ImGui::Text("%lu meshes (%lu verts.), %lu instances, %lu textures, %lu cameras",
							scene.meshDebugInfos.size(),
							scene.globalMesh.positions.size(),
							scene.instanceDebugInfos.size(),
							scene.textureDebugInfos.size(),
							scene.world.cameras().size());

				if(ImGui::SmallButton("Deselect")){
					deselect(frameInfos[0], selected, OBJECT);
				}
				ImGui::SameLine();
				if(ImGui::SmallButton("Center to selection")){
					// Pick whatever bounding box is currently displayed.
					adjustCameraToBoundingBox(camera, boundingBox.computeBoundingBox());
				}

				ImVec2 winSize = ImGui::GetContentRegionAvail();
				winSize.y *= 0.9;
				if(ImGui::BeginTabBar("InspectorTabbar")){

					if(ImGui::BeginTabItem("Meshes")){
						if(ImGui::BeginTable("Meshes#MeshList", 3, tableFlags, winSize)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							const int rowCount = (int)scene.meshDebugInfos.size();
							ImGuiListClipper clipper;
							clipper.Begin(rowCount);

							while(clipper.Step()){
								for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row){
									ImGui::TableNextColumn();
									ImGui::PushID(row);

									const Scene::MeshInfos& meshInfos = (*scene.meshInfos)[row];
									const Scene::MeshCPUInfos& meshDebugInfos = scene.meshDebugInfos[row];

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
												const std::vector<uint> localIndices = { 0, 1, 0, 0, 2, 0, 1, 3, 1, 2, 3, 2, 4, 5, 4, 4, 6, 4, 5, 7, 5, 6, 7, 6, 1, 5, 1, 0, 4, 0, 2, 6, 2, 3, 7, 3};
												for(const uint ind : localIndices){
													boundingBox.indices.push_back(indexShift + ind);
												}

											}
											boundingBox.colors.resize(boundingBox.positions.size(), glm::vec3(1.0f, 0.0f, 0.0f));
											boundingBox.upload();
											deselect(frameInfos[0], selected, INSTANCE);
										}
									}

									ImGui::TableNextColumn();
									ImGui::Text("%u", meshInfos.indexCount / 3u);
									ImGui::TableNextColumn();
									ImGui::Text("%u", meshInfos.materialIndex);

									ImGui::PopID();
								}
							}
							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}

					if(ImGui::BeginTabItem("Textures")){
						if(ImGui::BeginTable("Textures#TextureList", 3, tableFlags, winSize)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							int rowCount = (int)scene.textureDebugInfos.size();
							ImGuiListClipper clipper;
							clipper.Begin(rowCount);

							while(clipper.Step()){
								for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row){
									ImGui::TableNextColumn();
									ImGui::PushID(row);

									const Scene::TextureCPUInfos& debugInfos = scene.textureDebugInfos[row];
									const Texture& arrayTex = scene.textures[debugInfos.data.index];

									if(ImGui::Selectable(debugInfos.name.c_str(), selected.texture == row, selectableTableFlags)){
										selected.texture = row;
										frameInfos[0].selectedTextureArray = debugInfos.data.index;
										frameInfos[0].selectedTextureLayer = debugInfos.data.layer;
										zoomPct = 100.f;
										centerPct = glm::vec2(50.f, 50.0f);
										textureFramebuffer.resize(arrayTex.width, arrayTex.height);
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
							}
							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}
					if(ImGui::BeginTabItem("Cameras")){
						if(ImGui::BeginTable("#CamerasList", 1, tableFlags, winSize)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							const int rowCount = (int)scene.world.cameras().size();
							for(int row = 0; row < rowCount; ++row){
								ImGui::TableNextColumn();
								ImGui::PushID(row);
								const World::Camera& cam = scene.world.cameras()[row];
								if(ImGui::Selectable(cam.name.c_str())){
									const glm::vec4 camPos = cam.frame * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
									const glm::vec4 camCenter = cam.frame * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
									const glm::vec4 camAbove = cam.frame * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
									camera.pose(glm::vec3(camPos), glm::vec3(camCenter), glm::normalize(glm::vec3(camAbove - camPos)));
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

		if(ImGui::Begin("Instances")){
			if(selected.item >= 0){
				ImVec2 winSize = ImGui::GetContentRegionAvail();
				if(ImGui::BeginTable("Instances#InstanceList", 1, tableFlags, winSize)){
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

							const Scene::InstanceCPUInfos& debugInfos = scene.instanceDebugInfos[startRow + row];

							if(ImGui::Selectable(debugInfos.name.c_str(), selected.instance == row, selectableTableFlags)){
								if(selected.instance != row){
									selected.instance = row;
									frameInfos[0].selectedInstance = startRow + row;
									// Generate a mesh with bounding boxes of the instances.
									boundingBox.clean();
									const auto corners = debugInfos.bbox.getCorners();
									boundingBox.positions = corners;
									// Setup degenerate triangles for each line of a cube.
									boundingBox.indices = { 0, 1, 0, 0, 2, 0, 1, 3, 1, 2, 3, 2, 4, 5, 4, 4, 6, 4, 5, 7, 5, 6, 7, 6, 1, 5, 1, 0, 4, 0, 2, 6, 2, 3, 7, 3};
									boundingBox.colors.resize(boundingBox.positions.size(), glm::vec3(1.0f, 0.0f, 0.0f));
									boundingBox.upload();
								}
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
			if(selected.texture >= 0){

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

				const Texture* tex = textureFramebuffer.texture();
				const glm::vec2 texCenter = centerPct / 100.f;
				const glm::vec2 texScale(100.0f / std::max(zoomPct, 1.f));
				const glm::vec2 miniUV = texCenter - texScale * 0.5f;
				const glm::vec2 maxiUV = texCenter + texScale * 0.5f;

				ImGui::ImageButton(*tex, ImVec2(winSize.x, winSize.y), miniUV, maxiUV, 0, ImVec4(1.0f, 0.0f, 1.0f, 1.0f));
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


			if(ImGui::ArrowButton("VisibilityArrow", ImGuiDir_Down)){
				ImGui::OpenPopup("visibilityPopup");
			}

			if(ImGui::BeginPopup("visibilityPopup")){
				ImGui::Checkbox("Opaques",  &showOpaques);
				ImGui::Checkbox("Decals", &showDecals);
				ImGui::Checkbox("Transparents", &showTransparents);
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			ImGui::Text("Visibility");
			ImGui::SameLine();
			ImGui::Checkbox("Wireframe", &showWireframe);
			ImGui::SameLine();
			ImGui::Checkbox("Freeze culling", &freezeCulling);
			ImGui::Separator();
			if(ImGui::Button("Reset camera")){
				camera.reset();
				adjustCameraToBoundingBox(camera, scene.computeBoundingBox());
			}
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

		frameInfos[0].vp = vp;
		// Only update the culling VP if needed.
		if(!freezeCulling){
			frameInfos[0].vpCulling = vp;
		}
		frameInfos[0].ivp = glm::transpose(glm::inverse(vp));
		frameInfos[0].color = glm::vec4(1.0f);
		frameInfos[0].camPos = glm::vec4(camera.position(), 1.0f);
		frameInfos[0].albedoMode = albedoMode;
		frameInfos[0].shadingMode = shadingMode;
		frameInfos.upload();
		fb.bind(glm::vec4(0.25f, 0.25f, 0.25f, 1.0f), 1.0f, LoadOperation::DONTCARE);

		if(selected.item >= 0){

			// Populate drawCommands and drawInstancesby using a compute shader that performs culling.
			drawArgsCompute->use();
			drawArgsCompute->buffer(frameInfos, 0);
			drawArgsCompute->buffer(*scene.meshInfos, 1);
			drawArgsCompute->buffer(*scene.instanceInfos, 2);
			drawArgsCompute->buffer(*drawCommands, 3);
			drawArgsCompute->buffer(*drawInstances, 4);
			GPU::dispatch((uint)scene.meshInfos->size(), 1, 1);

			fb.bind(glm::vec4(0.25f, 0.25f, 0.25f, 1.0f), 1.0f, LoadOperation::DONTCARE);
			fb.setViewport();

			if(selected.mesh >= 0){
				coloredDebugDraw->use();
				coloredDebugDraw->buffer(frameInfos, 0);
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
			GPU::setColorState(true, true, true, false);

			texturedInstancedObject->use();
			texturedInstancedObject->buffer(frameInfos, 0);
			texturedInstancedObject->buffer(*scene.meshInfos, 1);
			texturedInstancedObject->buffer(*scene.instanceInfos, 2);
			texturedInstancedObject->buffer(*scene.materialInfos, 3);
			texturedInstancedObject->buffer(*drawInstances, 4);

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
				GPU::setDepthState(true, TestFunction::LEQUAL, false);
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

			if(showTransparents){
				// Alternative possibility: real alpha blend and object sorting.
				GPU::setDepthState(true, TestFunction::LEQUAL, false);
				GPU::setCullState(false);
				GPU::setBlendState(true, BlendEquation::ADD, BlendFunction::ONE, BlendFunction::ONE);
				for(uint mid = 0; mid < scene.meshInfos->size(); ++mid){
					const uint materialIndex = (*scene.meshInfos)[mid].materialIndex;
					if((*scene.materialInfos)[materialIndex].type != Object::Material::TRANSPARENT){
						continue;
					}
					GPU::drawIndirectMesh(scene.globalMesh, *drawCommands, mid);
				}
			}

			// Debug view.
			if(showWireframe){

				GPU::setPolygonState(PolygonMode::LINE);
				GPU::setCullState(false);
				GPU::setDepthState(true, TestFunction::LESS, true);
				GPU::setBlendState(false);

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


		}

		if(selected.texture >= 0){
			textureFramebuffer.bind(glm::vec4(1.0f, 0.0f, 0.5f, 1.0f), LoadOperation::DONTCARE, LoadOperation::DONTCARE);
			textureFramebuffer.setViewport();
			GPU::setDepthState(false);
			GPU::setCullState(true);
			GPU::setPolygonState(PolygonMode::FILL);
			GPU::setBlendState(true, BlendEquation::ADD, BlendFunction::SRC_ALPHA, BlendFunction::ONE_MINUS_SRC_ALPHA);
			textureQuad->use();
			textureQuad->buffer(frameInfos, 0);
			GPU::drawQuad();
		}

		Framebuffer::backbuffer()->bind(glm::vec4(0.2f, 0.2f, 0.2f, 1.0f), LoadOperation::DONTCARE, LoadOperation::DONTCARE);
		firstFrame = false;
	}

	scene.clean();
	for(Program* program : programPool){
		program->clean();
		delete program;
	}

	return 0;
}
