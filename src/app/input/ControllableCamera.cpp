#include "input/ControllableCamera.hpp"
#include "input/Input.hpp"
#include <imgui/imgui.h>

#include <sstream>

ControllableCamera::ControllableCamera(Mode mode) : _mode(mode) {
	reset();
}

void ControllableCamera::reset() {
	_eye	= glm::vec3(0.0, 0.0, 1.0);
	_center = glm::vec3(0.0, 0.0, 0.0);
	_up		= glm::vec3(0.0, 1.0, 0.0);
	_right  = glm::vec3(1.0, 0.0, 0.0);
	_view   = glm::lookAt(_eye, _center, _up);
	_radius = 1.0;
	_angles = glm::vec2(glm::half_pi<float>(), 0.0f);
}

void ControllableCamera::pose(const glm::vec3 & position, const glm::vec3 & center, const glm::vec3 & up) {
	Camera::pose(position, center, up);
	_radius = glm::length(_eye - _center);
	// Update angles.
	const glm::vec3 dir = glm::normalize(_eye - _center);
	const float axisH   = std::atan2(dir[2], dir[0]);
	const float axisV   = std::asin(dir[1]);
	_angles				= glm::vec2(axisH, axisV);
}

void ControllableCamera::update() {
	if(Input::manager().triggered(Input::Key::R)) {
		reset();
	}
	if(Input::manager().triggered(Input::Key::F)) {
		_mode = Mode::FPS;
	}
	if(Input::manager().triggered(Input::Key::G) || Input::manager().controllerDisconnected()) {
		_mode   = Mode::TurnTable;
		_radius = glm::length(_eye - _center);
	}
	if(Input::manager().triggered(Input::Key::J) || Input::manager().controllerConnected()) {
		if(Input::manager().controllerAvailable()) {
			_mode = Mode::Joystick;
		} else {
			Log::warning("Input: No joystick connected.");
		}
	}
}

void ControllableCamera::physics(double frameTime) {

	if(_mode == Mode::Joystick) {
		updateUsingJoystick(frameTime);
	} else if(_mode == Mode::FPS) {
		updateUsingKeyboard(frameTime);
	} else if(_mode == Mode::TurnTable) {
		updateUsingTurnTable(frameTime);
	}

	updateView();
}

void ControllableCamera::updateUsingJoystick(double frameTime) {
	// Check that the controller is available.
	if(!Input::manager().controllerAvailable()) {
		// Else do nothing.
		return;
	}

	Controller & joystick = *Input::manager().controller();
	// Handle buttons
	// Reset camera when pressing the Circle button.
	if(joystick.pressed(Controller::ButtonB)) {
		_eye	= glm::vec3(0.0, 0.0, 1.0);
		_center = glm::vec3(0.0, 0.0, 0.0);
		_up		= glm::vec3(0.0, 1.0, 0.0);
		_right  = glm::vec3(1.0, 0.0, 0.0);
		return;
	}

	// Special actions to restore the camera orientation.
	// Restore the up vector.
	if(joystick.pressed(Controller::BumperL1)) {
		_up = glm::vec3(0.0f, 1.0f, 0.0f);
	}
	// Look at the center of the scene
	if(joystick.pressed(Controller::BumperR1)) {
		_center[0] = _center[1] = _center[2] = 0.0f;
	}

	// The Up and Down boutons are configured to register each press only once
	// to avoid increasing/decreasing the speed for as long as the button is pressed.
	if(joystick.triggered(Controller::ButtonUp, true)) {
		_speed *= 2.0f;
	}

	if(joystick.triggered(Controller::ButtonDown, true)) {
		_speed *= 0.5f;
	}

	// Handle axis
	// Left stick to move
	// We need the direction of the camera, normalized.
	glm::vec3 look = normalize(_center - _eye);
	// Require a minimum deplacement between starting to register the move.
	const float axisForward	= joystick.axis(Controller::PadLeftY);
	const float axisLateral	= joystick.axis(Controller::PadLeftX);
	const float axisUp		   = joystick.axis(Controller::TriggerL2);
	const float axisDown	   = joystick.axis(Controller::TriggerR2);
	const float axisVertical   = joystick.axis(Controller::PadRightY);
	const float axisHorizontal = joystick.axis(Controller::PadRightX);

	if(axisForward * axisForward + axisLateral * axisLateral > 0.02f) {
		// Update the camera position.
		_eye = _eye - axisForward * float(frameTime) * _speed * look;
		_eye = _eye + axisLateral * float(frameTime) * _speed * _right;
	}

	// L2 and R2 triggers are used to move up and down. They can be read like axis.
	if(axisUp > -0.9) {
		_eye = _eye - (axisUp + 1.0f) * 0.5f * float(frameTime) * _speed * _up;
	}
	if(axisDown > -0.9) {
		_eye = _eye + (axisDown + 1.0f) * 0.5f * float(frameTime) * _speed * _up;
	}

	// Update center (eye-center stays constant).
	_center = _eye + look;

	// Right stick to look around.
	if(axisVertical * axisVertical + axisHorizontal * axisHorizontal > 0.02f) {
		_center = _center - axisVertical * float(frameTime) * _angularSpeed * _up;
		_center = _center + axisHorizontal * float(frameTime) * _angularSpeed * _right;
	}
	// Renormalize the look vector.
	look = normalize(_center - _eye);
	// Recompute right as the cross product of look and up.
	_right = normalize(cross(look, _up));
	// Recompute up as the cross product of  right and look.
	_up = normalize(cross(_right, look));
}

void ControllableCamera::updateUsingKeyboard(double frameTime) {
	// We need the direction of the camera, normalized.
	glm::vec3 look = normalize(_center - _eye);

	// Speed adjustment for fast motion mode.
	float localSpeed = _speed;
	if(Input::manager().pressed(Input::Key::LeftShift)) {
		localSpeed *= 5.0f;
	}

	// One step forward or backward.
	const glm::vec3 deltaLook = localSpeed * float(frameTime) * look;
	// One step laterally horizontal.
	const glm::vec3 deltaLateral = localSpeed * float(frameTime) * _right;
	// One step laterally vertical.
	const glm::vec3 deltaVertical = localSpeed * float(frameTime) * _up;

	if(Input::manager().pressed(Input::Key::W)) { // Forward
		_eye += deltaLook;
	}

	if(Input::manager().pressed(Input::Key::S)) { // Backward
		_eye -= deltaLook;
	}

	if(Input::manager().pressed(Input::Key::A)) { // Left
		_eye -= deltaLateral;
	}

	if(Input::manager().pressed(Input::Key::D)) { // Right
		_eye += deltaLateral;
	}

	if(Input::manager().pressed(Input::Key::Q)) { // Down
		_eye -= deltaVertical;
	}

	if(Input::manager().pressed(Input::Key::E)) { // Up
		_eye += deltaVertical;
	}

	const glm::vec2 delta = Input::manager().moved(Input::Mouse::Left);
	_angles += delta * float(frameTime) * _angularSpeed;
	_angles[1] = (std::max)(-1.57f, (std::min)(1.57f, _angles[1]));
	// Right stick to look around.
	const glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), glm::half_pi<float>() - _angles[0], glm::vec3(0.0, 1.0, 0.0));
	const glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), -_angles[1], glm::vec3(1.0, 0.0, 0.0));
	const glm::mat3 rot  = glm::mat3(rotY * rotX);

	look	= rot * glm::vec3(0.0, 0.0, -1.0);
	_center = _eye + look;
	//_up		= rot * glm::vec3(0.0, 1.0, 0.0);
	_right = normalize(cross(normalize(look), _up));
	//_right  = rot * glm::vec3(1.0, 0.0, 0.0);
}

void ControllableCamera::updateUsingTurnTable(double frameTime) {
	// We need the direction of the camera, normalized.
	const glm::vec3 look = normalize(_center - _eye);

	// Speed adjustment for fast motion mode.
	float localSpeed = _speed;
	if(Input::manager().pressed(Input::Key::LeftShift)) {
		localSpeed *= 5.0f;
	}

	// One step forward or backward.
	const glm::vec3 deltaLook = localSpeed * float(frameTime) * look;
	// One step laterally horizontal.
	const glm::vec3 deltaLateral = localSpeed * float(frameTime) * _right;
	// One step laterally vertical.
	const glm::vec3 deltaVertical = localSpeed * float(frameTime) * _up;

	if(Input::manager().pressed(Input::Key::W)) { // Forward
		_center += deltaLook;
	}

	if(Input::manager().pressed(Input::Key::S)) { // Backward
		_center -= deltaLook;
	}

	if(Input::manager().pressed(Input::Key::A)) { // Left
		_center -= deltaLateral;
	}

	if(Input::manager().pressed(Input::Key::D)) { // Right
		_center += deltaLateral;
	}

	if(Input::manager().pressed(Input::Key::Q)) { // Down
		_center -= deltaVertical;
	}

	if(Input::manager().pressed(Input::Key::E)) { // Up
		_center += deltaVertical;
	}

	// Radius of the turntable.
	const float scroll = Input::manager().scroll()[1];
	_radius			   = (std::max)(0.0001f, _radius - scroll * float(frameTime) * _speed);

	// Angles update for the turntable.
	const glm::vec2 delta = Input::manager().moved(Input::Mouse::Left);
	_angles += delta * float(frameTime) * _angularSpeed;
	_angles[1] = (std::max)(-1.57f, (std::min)(1.57f, _angles[1]));

	// Compute new look direction.
	const glm::vec3 newLook = -glm::vec3(cos(_angles[1]) * cos(_angles[0]), sin(_angles[1]), cos(_angles[1]) * sin(_angles[0]));

	// Update the camera position around the center.
	_eye = _center - _radius * newLook;

	// Recompute right as the cross product of look and up.
	_right = normalize(cross(newLook, glm::vec3(0.0f, 1.0f, 0.0f)));
	// Recompute up as the cross product of  right and look.
	_up = normalize(cross(_right, newLook));
}

void ControllableCamera::interface(){
	ImGui::PushItemWidth(110);
	ImGui::Combo("Camera mode", reinterpret_cast<int *>(&_mode), "FPS\0Turntable\0Joystick\0\0", 3);

	ImGui::InputFloat("Camera speed", &_speed, 0.1f, 1.0f);
	ImGui::SameLine();
	// Display degrees fov.
	float guiFOV = _fov * 180.0f / glm::pi<float>();
	if(ImGui::InputFloat("Camera FOV", &guiFOV, 1.0f, 10.0f)) {
		fov( guiFOV * glm::pi<float>() / 180.0f);
	}

	if(ImGui::DragFloat2("Planes", static_cast<float *>(&_clippingPlanes[0]))) {
		updateProjection();
	}
	ImGui::PopItemWidth();

	if( ImGui::Button( "Copy camera", ImVec2( 104, 0 ) ) )
	{
		std::stringstream desc;
		desc << _eye[ 0 ] << " " << _eye[ 1 ] << " " << _eye[ 2 ] << "\n";
		desc << _center[ 0 ] << " " << _center[ 1 ] << " " << _center[ 2 ] << "\n";
		desc << _up[ 0 ] << " " << _up[ 1 ] << " " << _up[ 2 ] << "\n";
		desc << _fov << "\n";
		desc << _clippingPlanes[ 0 ] << " " <<  _clippingPlanes[1];
		const std::string camDesc = desc.str();
		ImGui::SetClipboardText( camDesc.c_str() );
	}
	ImGui::SameLine();
	if( ImGui::Button( "Paste camera", ImVec2( 104, 0 ) ) )
	{
		const std::string camDesc( ImGui::GetClipboardText() );
		std::stringstream desc( camDesc );
		glm::vec3 eyeN, centerN, upN;
		float fovN;
		glm::vec2 clippingPlanesN;
		float ratioN = ratio();

		desc >> eyeN[ 0 ] >> eyeN[ 1 ] >> eyeN[ 2 ];
		desc >> centerN[ 0 ] >> centerN[ 1 ] >> centerN[ 2 ];
		desc >> upN[ 0 ] >> upN[ 1 ] >> upN[ 2 ];
		desc >> fovN;
		desc >> clippingPlanesN[ 0 ] >> clippingPlanesN[ 1 ];

		pose( eyeN, centerN, upN );
		projection( ratioN, fovN, clippingPlanesN[ 0 ], clippingPlanesN[ 1 ] );
	
	}

}
