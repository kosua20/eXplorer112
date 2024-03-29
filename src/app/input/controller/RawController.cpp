#include "input/controller/RawController.hpp"
#include <GLFW/glfw3.h>

bool RawController::activate(int id) {
	reset();

	_id   = id;
	_name = std::string(glfwGetJoystickName(_id));
	_guid = std::string(glfwGetJoystickGUID(_id));
	Log::info("Input: Raw joystick named %s.", _name.c_str());

	return true;
}

void RawController::deactivate() {
	_id = -1;
}

void RawController::update() {
	// Update buttons flags.
	const float * rawAxes			 = glfwGetJoystickAxes(_id, &_rawAxesCount);
	const unsigned char * rawButtons = glfwGetJoystickButtons(_id, &_rawButtonsCount);

	if(_rawAxesCount != int(allAxes.size())) {
		allAxes.resize(_rawAxesCount);
	}
	if(_rawButtonsCount != int(allButtons.size())) {
		allButtons.resize(_rawButtonsCount);
	}

	for(int aid = 0; aid < _rawAxesCount; ++aid) {
		allAxes[aid] = rawAxes[aid];
	}

	for(int bid = 0; bid < _rawButtonsCount; ++bid) {
		const bool pressed = (rawButtons[bid] == GLFW_PRESS);
		if(pressed) {
			if(allButtons[bid].pressed) {
				// Already pressed.
				allButtons[bid].first = false;
			} else {
				allButtons[bid].pressed = true;
				allButtons[bid].first   = true;
			}
		} else {
			allButtons[bid].pressed = false;
			allButtons[bid].first   = false;
		}
	}
}
