#pragma once
#include "core/System.hpp"
#include "core/Geometry.hpp"

namespace Dff {

bool parse(const fs::path& path, Model& context);

void convertToObj(Model& model, Obj& outObject, const std::string& baseName);

}
