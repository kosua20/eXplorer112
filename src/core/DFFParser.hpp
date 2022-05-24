#pragma once
#include "core/System.hpp"
#include "core/Geometry.hpp"

namespace Dff {

struct Context {
	std::vector<Object> objects;
};

bool parse(const fs::path& path, Context& context);

}
