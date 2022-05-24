#pragma once
#include <glm/glm.hpp>
#include <vector>

struct Color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

struct Triangle {
	uint16_t v1, v0, id, v2;
};

struct Object {

	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
	std::vector<std::vector<glm::vec2>> uvs;
	std::vector<Color> colors;
	std::vector<Triangle> faces;

};
