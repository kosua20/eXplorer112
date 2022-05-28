#pragma once

#include "core/System.hpp"
#include "core/Geometry.hpp"

namespace Area {

glm::vec3 parseVec3(const char* v);

bool load(const fs::path& path, Obj& outObject, TexturesList& usedTextures);

}
