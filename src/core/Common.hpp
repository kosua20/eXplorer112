#pragma once

#ifdef _WIN32
#	define NOMINMAX
#endif

#include "core/Log.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/color_space.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/io.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/random.hpp>

#include <string>
#include <vector>
#include <algorithm>
#include <memory>

typedef unsigned char uchar;
typedef unsigned int  uint;
typedef unsigned long ulong;

#ifdef _WIN32
#	undef near
#	undef far
#	undef ERROR
#endif

#define STD_HASH(ENUM_NAME) \
template <> struct std::hash<ENUM_NAME> { \
	std::size_t operator()(const ENUM_NAME & t) const { return static_cast<std::underlying_type< ENUM_NAME >::type>(t); } \
};
