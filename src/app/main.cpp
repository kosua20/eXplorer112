
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

int main(int argc, char ** argv) {
	// First, init/parse/load configuration.
	ViewerConfig config(std::vector<std::string>(argv, argv + argc));
	if(config.showHelp()) {
		return 0;
	}
	

	const fs::path inputPath = config.path;
	const fs::path modelsPath = inputPath / "models";
	const fs::path texturesPath = inputPath / "textures";
	const fs::path templatesPath = inputPath / "templates";
	const fs::path zonesPath = inputPath / "zones";
	const fs::path worldsPath = zonesPath / "world";

	std::vector<fs::path> modelsList;
	System::listAllFilesOfType(modelsPath, ".dff", modelsList);

	std::vector<fs::path> templatesList;
	System::listAllFilesOfType(templatesPath, ".template", templatesList);

	std::vector<fs::path> worldsList;
	System::listAllFilesOfType(worldsPath, ".world", worldsList);

	std::vector<fs::path> texturesList;
	System::listAllFilesOfType(modelsPath, ".dds", texturesList);
	System::listAllFilesOfType(modelsPath, ".tga", texturesList);
	System::listAllFilesOfType(texturesPath, ".dds", texturesList);
	System::listAllFilesOfType(texturesPath, ".tga", texturesList);

	Window window("eXperience112 viewer", config);

	ControllableCamera camera;
	camera.speed() = 20.0f;
	camera.projection(config.screenResolution[0] / config.screenResolution[1], glm::pi<float>() * 0.4f, 1.f, 1000.0f);
	camera.pose(glm::vec3(0.0f, 0.0f, 100.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	camera.ratio(config.screenResolution[0] / config.screenResolution[1]);

	const double dt = 1.0/120;
	double timer = Input::getTime();
	double remainingTime = 0;

	
	Program* defaultQuad = loadProgram("passthrough", "passthrough");
	Program* defaultObject = loadProgram("object_basic_texture", "object_basic_texture");

	Obj obj;
	TexturesList usedTextures;
	Dff::load(config.path / "models/humans/l_nichols/pilote/lnichols_pilote.dff", obj, usedTextures);
	fixObjectForRendering(obj);

	std::vector<Mesh> meshes;
	std::vector<Texture> textures;
	for(const Obj::Set& set : obj.faceSets){

		{
			textures.emplace_back("tex");
			Texture& tex = textures.back();

			const std::string textureName = set.texture;
			for(const fs::path& texturePath : texturesList){
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
			tex.width = tex.images[0].width;
			tex.height = tex.images[0].height;
			tex.depth = tex.levels = 1;
			tex.shape = TextureShape::D2;
			tex.upload(Layout::SRGB8_ALPHA8, false);
		}

		{
			meshes.emplace_back("mesh");
			Mesh& mesh = meshes.back();

			const size_t vertCount = set.faces.size() * 3;
			const bool hasUV = (vertCount != 0) && (set.faces[0].t0 != Obj::Set::Face::INVALID);
			const bool hasNormals = (vertCount != 0) && (set.faces[0].n0 != Obj::Set::Face::INVALID);

			mesh.positions.resize(vertCount);
			mesh.indices.resize(vertCount);
			if(hasUV){
				mesh.texcoords.resize(vertCount);
			}
			if(hasNormals){
				mesh.normals.resize(vertCount);
			}

			unsigned int ind = 0;
			for(const Obj::Set::Face& f : set.faces){
				mesh.positions[ind]   = obj.positions[f.v0];
				mesh.positions[ind+1] = obj.positions[f.v1];
				mesh.positions[ind+2] = obj.positions[f.v2];
				mesh.indices[ind]   = ind;
				mesh.indices[ind+1] = ind+1;
				mesh.indices[ind+2] = ind+2;
				if(hasUV){
					mesh.texcoords[ind]   = obj.uvs[f.t0];
					mesh.texcoords[ind+1] = obj.uvs[f.t1];
					mesh.texcoords[ind+2] = obj.uvs[f.t2];
				}
				if(hasNormals){
					mesh.normals[ind]   = glm::normalize(obj.normals[f.n0]);
					mesh.normals[ind+1] = glm::normalize(obj.normals[f.n1]);
					mesh.normals[ind+2] = glm::normalize(obj.normals[f.n2]);
				}
				ind += 3;

			}
			mesh.upload();
		}

	}

	struct FrameData {
		glm::mat4 mvp;
	};
	UniformBuffer<FrameData> ubo(1, 64);

	glm::vec2 renderingRes = config.resolutionRatio * config.screenResolution;
	Framebuffer fb(renderingRes[0], renderingRes[1], {Layout::RGBA8, Layout::DEPTH_COMPONENT32F}, "sceneFb");

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

		// Begin GUI setup.
		if(ImGui::Begin("Viewer")) {
			int ratioPercentage = std::round(config.resolutionRatio * 100.0f);

			if(ImGui::InputInt("Rendering res. %", &ratioPercentage, 10, 25)) {
				ratioPercentage = glm::clamp(ratioPercentage, 10, 200);

				const float newRatio = (float)ratioPercentage / 100.0f;
				glm::vec2 renderRes(fb.width(), fb.height());
				renderRes = (renderRes / config.resolutionRatio) * newRatio;

				config.resolutionRatio = newRatio;
				fb.resize(renderRes[0], renderRes[1]);
				camera.ratio(renderRes[0]/renderRes[1]);
			}
		}
		ImGui::End();

		/// Rendering.
		const glm::mat4 mvp		   = camera.projection() * camera.view();

		ubo.at(0).mvp = mvp;
		ubo.upload();

		fb.bind(glm::vec4(0.25f, 0.25f, 0.25f, 1.0f), 1.0f, Framebuffer::Operation::DONTCARE);
		GPU::setViewport(0, 0, fb.width(), fb.height());

		GPU::setDepthState(true, TestFunction::LESS, true);
		GPU::setBlendState(false);
		GPU::setCullState(true);

		defaultObject->use();
		defaultObject->buffer(ubo, 0);

		for(unsigned int i = 0; i < meshes.size(); ++i){
			defaultObject->texture(textures[i], 0);
			GPU::drawMesh(meshes[i]);
		}

		Framebuffer::backbuffer()->bind(glm::vec4(0.2f, 0.2f, 0.2f, 1.0f), Framebuffer::Operation::DONTCARE, Framebuffer::Operation::DONTCARE);

		if(ImGui::Begin("Main view", nullptr, ImGuiWindowFlags_NoBringToFrontOnFocus)){
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

	}
	defaultQuad->clean();
	defaultObject->clean();
	delete defaultQuad;
	delete defaultObject;

	return 0;
}
