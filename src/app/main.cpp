
#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/Common.hpp"
#include "core/DFFParser.hpp"

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

void fixObjectForRendering(Obj& obj){
	// Flip UVs.
	for(glm::vec2& uv : obj.uvs){
		uv[1] = 1.f - uv[1];
	}
	// Decrement 1-based indices.
	for(Obj::Set& set : obj.faceSets){
		for(Obj::Set::Face& f : set.faces){
			f.v0 = f.v0 - 1;
			f.v1 = f.v1 - 1;
			f.v2 = f.v2 - 1;
			f.t0 = f.t0 == Obj::Set::Face::INVALID ? f.t0 : (f.t0 - 1);
			f.t1 = f.t1 == Obj::Set::Face::INVALID ? f.t1 : (f.t1 - 1);
			f.t2 = f.t2 == Obj::Set::Face::INVALID ? f.t2 : (f.t2 - 1);
			f.n0 = f.n0 == Obj::Set::Face::INVALID ? f.n0 : (f.n0 - 1);
			f.n1 = f.n1 == Obj::Set::Face::INVALID ? f.n1 : (f.n1 - 1);
			f.n2 = f.n2 == Obj::Set::Face::INVALID ? f.n2 : (f.n2 - 1);

		}
	}
}

struct GameFiles {

	GameFiles(){}

	GameFiles(const fs::path& installPath){

		const fs::path resourcePath = installPath / "resources";
		modelsPath = resourcePath / "models";
		texturesPath = resourcePath / "textures";
		templatesPath = resourcePath / "templates";
		zonesPath = resourcePath / "zones";
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

struct ModelScene {
	Obj dff;
	std::vector<Mesh> meshes;
	std::vector<Texture> textures;
	std::vector<uint> meshToTextures;

	void clean(){
		for(Mesh& mesh : meshes){
			mesh.clean();
		}
		for(Texture& texture : textures){
			texture.clean();
		}
		meshes.clear();
		textures.clear();
		meshToTextures.clear();

		dff = Obj();
	}

	void load(const fs::path& dffPath, const GameFiles& files){

		TexturesList usedTextures;
		Dff::load(dffPath, dff, usedTextures);
		fixObjectForRendering(dff);

		const glm::vec2 defaultUV = glm::vec2(0.5f);
		const glm::vec3 defaultNormal = glm::vec3(0.0f, 0.0f, 1.0f);

		for(const Obj::Set& set : dff.faceSets){

			// Load geometry.
			{
				meshes.emplace_back(set.name);
				Mesh& mesh = meshes.back();

				const size_t vertCount = set.faces.size() * 3;

				mesh.positions.resize(vertCount);
				mesh.indices.resize(vertCount);
				mesh.texcoords.resize(vertCount);
				mesh.normals.resize(vertCount);

				unsigned int ind = 0;
				for(const Obj::Set::Face& f : set.faces){
					mesh.positions[ind]   = dff.positions[f.v0];
					mesh.positions[ind+1] = dff.positions[f.v1];
					mesh.positions[ind+2] = dff.positions[f.v2];
					mesh.indices[ind]   = ind;
					mesh.indices[ind+1] = ind+1;
					mesh.indices[ind+2] = ind+2;

					mesh.texcoords[ind+0] = (f.t0 != Obj::Set::Face::INVALID) ? dff.uvs[f.t0] : defaultUV;
					mesh.texcoords[ind+1] = (f.t1 != Obj::Set::Face::INVALID) ? dff.uvs[f.t1] : defaultUV;
					mesh.texcoords[ind+2] = (f.t2 != Obj::Set::Face::INVALID) ? dff.uvs[f.t2] : defaultUV;

					mesh.normals[ind+0] = (f.n0 != Obj::Set::Face::INVALID) ? dff.normals[f.n0] : defaultNormal;
					mesh.normals[ind+1] = (f.n1 != Obj::Set::Face::INVALID) ? dff.normals[f.n1] : defaultNormal;
					mesh.normals[ind+2] = (f.n2 != Obj::Set::Face::INVALID) ? dff.normals[f.n2] : defaultNormal;

					ind += 3;

				}
				mesh.computeBoundingBox();
				mesh.upload();
			}
			// Load texture if it has not already been loaded.
			{
				const std::string textureName = set.texture;
				const uint textureCount = (uint)textures.size();

				uint textureIndex = textureCount;
				// Look at existing textures first.
				for(uint tid = 0u; tid < textureCount; ++tid){
					if(textures[tid].name() == textureName){
						textureIndex = tid;
						break;
					}
				}

				// If not found, create a new texture.
				if(textureIndex == textureCount){

					textures.emplace_back(textureName.empty() ? "tex" : textureName);
					Texture& tex = textures.back();

					for(const fs::path& texturePath : files.texturesList){
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
					// TODO: handle compressed textures directly
					tex.upload(Layout::SRGB8_ALPHA8, false);
				}

				meshToTextures.emplace_back(textureIndex);

			}
		}
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
	const std::string iniPath = APP_RESOURCE_DIRECTORY / "imgui.ini";
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
	camera.speed() = 20.0f;
	camera.projection(config.screenResolution[0] / config.screenResolution[1], glm::pi<float>() * 0.4f, 1.f, 1000.0f);
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


	struct FrameData {
		glm::mat4 vp{1.0f};
		glm::mat4 ivp{1.0f};
		glm::vec4 color{1.0f};
		int shadingMode = 0;
		int albedoMode = 0;
	};
	UniformBuffer<FrameData> ubo(1, 64);
	glm::vec2 renderingRes = config.resolutionRatio * config.screenResolution;
	Framebuffer fb(renderingRes[0], renderingRes[1], {Layout::RGBA8, Layout::DEPTH_COMPONENT32F}, "sceneFb");

	// Data loading.

	ModelScene model;

	// GUi state
	Mesh boundingBox("bbox");
	enum class ViewerMode {
		MODEL, WORLD
	};
	ViewerMode viewMode = ViewerMode::MODEL;
	int shadingMode = MODE_SHADING_LIGHT;
	int albedoMode = MODE_ALBEDO_TEXTURE;
	int selectedModel = -1;
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
						model = ModelScene();
						selectedModel = -1;
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

			if (ImGui::BeginTabBar("#FilesTabBar")){

				if (ImGui::BeginTabItem("Models")){
					viewMode = ViewerMode::MODEL;

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
								std::string modelName = modelPath.filename();
								const std::string modelParent = modelPath.parent_path().filename();

								if(selectedModel == row){
									modelName = "* " + modelName;
								}

								if(ImGui::Selectable(modelName.c_str(), selectedModel == row, selectableTableFlags)){
									if(selectedModel != row){
										model.clean();
										selectedModel = row;
										model.load(modelPath, gameFiles);
										// Compute the bounding box.
										BoundingBox modelBox;
										for(Mesh& mesh : model.meshes){
											modelBox.merge(mesh.bbox);
										}
										// Center the camera.
										const glm::vec3 center = modelBox.getCentroid();
										const glm::vec3 extent = modelBox.getSize();
										const float maxExtent = glm::max(extent[0], glm::max(extent[1], extent[2]));
										camera.pose(center + glm::vec3(0.0, 0.0, maxExtent), center, glm::vec3(0.0, 1.0f, 0.0f));
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

				if(ImGui::BeginTabItem("Worlds")){
					viewMode = ViewerMode::WORLD;
					selectedModel = -1;
					
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
							std::string worldName = worldPath.filename();
							const std::string worldParent = worldPath.parent_path().filename();

							ImGui::Text("%s", worldName.c_str());
							ImGui::TableNextColumn();
							ImGui::Text("%s", worldParent.c_str());

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
			if(selectedModel >= 0){
				ImGui::Text("Model: %s", gameFiles.modelsList[selectedModel].filename().string().c_str());
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
					ImGui::TableSetupColumn("Vertices", ImGuiTableColumnFlags_None);
					ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_None);
					ImGui::TableHeadersRow();

					const int meshCount = (int)model.meshes.size();
					for(int row = 0; row < meshCount; ++row){
						ImGui::TableNextColumn();
						ImGui::PushID(row);

						const Mesh& mesh = model.meshes[row];
						std::string meshName = mesh.name();
						if(selectedMesh == row){
							meshName = "* " + meshName;
						}

						if(ImGui::Selectable(meshName.c_str(), selectedMesh == row, selectableTableFlags)){
							if(selectedMesh != row){
								selectedMesh = row;
								// Update bbox mesh.
								boundingBox.clean();
								boundingBox.positions = mesh.bbox.getCorners();
								boundingBox.colors.resize(boundingBox.positions.size(), glm::vec3(1.0f, 0.0f, 0.0f));
								// Setup degenerate triangles for each line of a cube.
								boundingBox.indices = {
									0, 1, 0, 0, 2, 0, 1, 3, 1, 2, 3, 2, 4, 5, 4, 4, 6, 4, 5, 7, 5, 6, 7, 6, 1, 5, 1, 0, 4, 0, 2, 6, 2, 3, 7, 3};
								boundingBox.upload();
							}
						}
						ImGui::TableNextColumn();
						ImGui::Text("%lu", mesh.metrics().vertices);
						ImGui::TableNextColumn();
						ImGui::Text("%lu", mesh.metrics().indices/3);

						ImGui::PopID();
					}

					ImGui::EndTable();
				}

				if(ImGui::BeginTable("#TextureList", 2, tableFlags, winSize)){
					// Header
					ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
					ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
					ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None);
					ImGui::TableHeadersRow();

					const int texCount = (int)model.textures.size();
					for(int row = 0; row < texCount; ++row){
						ImGui::TableNextColumn();
						ImGui::PushID(row);

						const Texture& tex = model.textures[row];
						std::string texName = tex.name();
						if(selectedTexture == row){
							texName = "* " + texName;
						}

						if(ImGui::Selectable(texName.c_str(), selectedTexture == row, selectableTableFlags)){
							selectedTexture = row;
						}

						ImGui::TableNextColumn();
						ImGui::Text("%ux%u", tex.width, tex.height);
						ImGui::PopID();
					}

					ImGui::EndTable();
				}
			}

		}
		ImGui::End();

		if(ImGui::Begin("Texture")){
			if(selectedTexture >= 0){
				// Adjust the texture display to the window size.
				ImVec2 winSize = ImGui::GetContentRegionAvail();
				winSize.x = std::max(winSize.x, 2.f);
				winSize.y = std::max(winSize.y, 2.f);
				ImGui::ImageButton(model.textures[selectedTexture], ImVec2(winSize.x, winSize.y), ImVec2(0.0,0.0), ImVec2(1.0,1.0), 0, ImVec4(1.0f, 0.0f, 1.0f, 1.0f));
			}
		}
		ImGui::End();


		if(ImGui::Begin("Settings")) {
			int ratioPercentage = std::round(config.resolutionRatio * 100.0f);

			ImGui::Text("Rendering at %ux%upx", fb.width(), fb.height());
			if(ImGui::InputInt("Rendering ratio %", &ratioPercentage, 10, 25)) {
				ratioPercentage = glm::clamp(ratioPercentage, 10, 200);

				const float newRatio = (float)ratioPercentage / 100.0f;
				glm::vec2 renderRes(fb.width(), fb.height());
				renderRes = (renderRes / config.resolutionRatio) * newRatio;

				config.resolutionRatio = newRatio;
				fb.resize(renderRes[0], renderRes[1]);
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

		ubo.at(0).vp = vp;
		ubo.at(0).ivp = glm::transpose(glm::inverse(vp));
		ubo.at(0).color = glm::vec4(1.0f);
		ubo.at(0).albedoMode = albedoMode;
		ubo.at(0).shadingMode = shadingMode;
		ubo.upload();

		fb.bind(glm::vec4(0.25f, 0.25f, 0.25f, 1.0f), 1.0f, LoadOperation::DONTCARE);
		GPU::setViewport(0, 0, fb.width(), fb.height());

		if(viewMode == ViewerMode::MODEL){

			GPU::setPolygonState(PolygonMode::FILL);
			GPU::setDepthState(true, TestFunction::LESS, true);
			GPU::setBlendState(false);
			GPU::setCullState(true);

			texturedObject->use();
			texturedObject->buffer(ubo, 0);

			for(unsigned int i = 0; i < model.meshes.size(); ++i){
				texturedObject->texture(model.textures[model.meshToTextures[i]], 0);
				GPU::drawMesh(model.meshes[i]);
			}

			if(showWireframe){
				// Temporarily force the mode.
				ubo.at(0).shadingMode = MODE_SHADING_NONE;
				ubo.at(0).albedoMode = MODE_ALBEDO_UNIFORM;
				ubo.upload();

				texturedObject->use();
				texturedObject->buffer(ubo, 0);
				GPU::setPolygonState(PolygonMode::LINE);
				for(unsigned int i = 0; i < model.meshes.size(); ++i){
					if(selectedMesh == -1 || selectedMesh == (int)i){
						GPU::drawMesh(model.meshes[i]);
					}
				}
			}

			if(selectedMesh >= 0){
				coloredObject->use();
				coloredObject->buffer(ubo, 0);
				GPU::setPolygonState(PolygonMode::LINE);
				GPU::setCullState(false);
				GPU::drawMesh(boundingBox);
			}
		}


		Framebuffer::backbuffer()->bind(glm::vec4(0.2f, 0.2f, 0.2f, 1.0f), LoadOperation::DONTCARE, LoadOperation::DONTCARE);

	}

	model.clean();
	for(Program* program : programPool){
		program->clean();
		delete program;
	}

	return 0;
}
