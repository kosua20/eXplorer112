#pragma once

#include "core/Common.hpp"


namespace GameCode {

void rotateCameraFrame(glm::mat4& mat, const glm::vec3& axis, float angle, uint flag);

glm::mat4 cameraRotationMatrix(float rotationX, float rotationY);

};
