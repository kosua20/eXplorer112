#pragma once

#include "core/System.hpp"
#include "core/Geometry.hpp"

namespace Area {

bool parseBool(const char* v, bool fallback = false);
int parseInt(const char* v, int fallback = 0);
float parseFloat(const char* v, float fallback = 0.0f);
glm::vec2 parseVec2(const char* v, const glm::vec2& fallback = glm::vec2(0.0f));
glm::vec3 parseVec3(const char* v, const glm::vec3& fallback = glm::vec3(0.0f));
glm::vec4 parseVec4(const char* v, const glm::vec4& fallback = glm::vec4(0.0f));

bool load(const fs::path& path, Object& outObject);

}
