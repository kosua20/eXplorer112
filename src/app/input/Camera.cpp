#include "input/Camera.hpp"
#include "core/Bounds.hpp"
#include <sstream>

Camera::Camera() {
	updateView();
	updateProjection();
}

void Camera::pose(const glm::vec3 & position, const glm::vec3 & center, const glm::vec3 & up) {
	_eye					= position;
	_center					= center;
	_up						= glm::normalize(up);
	const glm::vec3 viewDir = glm::normalize(_center - _eye);
	_right					= glm::cross(viewDir, _up);
	_up						= glm::cross(_right, viewDir);
	updateView();
}

void Camera::projection(float ratio, float fov, float near, float far) {
	_clippingPlanes = glm::vec2(near, far);
	_ratio			= ratio;
	_fov			= fov;
	updateProjection();
}

void Camera::frustum(float near, float far) {
	_clippingPlanes = glm::vec2(near, far);
	updateProjection();
}

void Camera::ratio(float ratio) {
	_ratio = ratio;
	updateProjection();
}

void Camera::fov(float fov) {
	_fov = fov;
	updateProjection();
}

void Camera::pixelShifts(glm::vec3 & corner, glm::vec3 & dx, glm::vec3 & dy) const {
	const float heightScale = std::tan(0.5f * _fov);
	const float widthScale  = _ratio * heightScale;
	const float imageDist   = glm::distance(_eye, _center);
	corner					= _center + imageDist * (-widthScale * _right + heightScale * _up);
	dx						= 2.0f * widthScale * imageDist * _right;
	dy						= -2.0f * heightScale * imageDist * _up;
}

void Camera::updateProjection() {
	// Perspective projection.
	_projection = Frustum::perspective(_fov, _ratio, _clippingPlanes[1], _clippingPlanes[0]);
}

void Camera::updateView() {
	_view = glm::lookAt(_eye, _center, _up);
}

void Camera::apply(const Camera & camera) {
	const glm::vec2 & planes = camera.clippingPlanes();
	this->pose(camera.position(), camera.center(), camera.up());
	this->projection(camera.ratio(), camera.fov(), planes[0], planes[1]);
}

