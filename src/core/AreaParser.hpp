#pragma once

#include "core/System.hpp"
#include "core/Geometry.hpp"

namespace Area {

glm::vec2 parseVec2(const char* v);
glm::vec3 parseVec3(const char* v, const glm::vec3& fallback = glm::vec3(0.0f));

bool load(const fs::path& path, Obj& outObject, TexturesList& usedTextures);

}
