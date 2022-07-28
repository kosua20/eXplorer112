
#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/Common.hpp"

#include "system/Window.hpp"
#include "graphics/GPU.hpp"
#include "graphics/Framebuffer.hpp"
#include "input/Input.hpp"
#include "input/ControllableCamera.hpp"


int main(int argc, char ** argv) {
	// First, init/parse/load configuration.
	RenderingConfig config(std::vector<std::string>(argv, argv + argc));
	if(config.showHelp()) {
		return 0;
	}

	Window window("eXperience112 viewer", config);

	ControllableCamera camera;
	camera.projection(config.screenResolution[0] / config.screenResolution[1], glm::pi<float>() * 0.4f, 0.1f, 10.0f);
	camera.pose(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	camera.ratio(config.screenResolution[0] / config.screenResolution[1]);

	const double dt = 1.0/120;
	double timer = Input::getTime();
	double remainingTime = 0;

	while(window.nextFrame()) {

		// Handle window resize.
		if(Input::manager().resized()) {
			const uint width = uint(Input::manager().size()[0]);
			const uint height = uint(Input::manager().size()[1]);
			config.screenResolution[0] = float(width > 0 ? width : 1);
			config.screenResolution[1] = float(height > 0 ? height : 1);
			// Resize resources.
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
		if(ImGui::Begin("Test")) {
			ImGui::Text("Hello there!");
		}
		ImGui::End();

		/// Rendering.
		const glm::ivec2 screenSize = Input::manager().size();
		const glm::mat4 mvp		   = camera.projection() * camera.view();

		Framebuffer::backbuffer()->bind(glm::vec4(0.25f, 0.25f, 0.25f, 1.0f), 1.0f, Framebuffer::Operation::DONTCARE);
		GPU::setViewport(0, 0, screenSize[0], screenSize[1]);

		GPU::setDepthState(true, TestFunction::LESS, true);
		GPU::setBlendState(false);
		GPU::setCullState(false);

	}

	return 0;
}
