#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>

struct Color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

struct Triangle {
	uint16_t v1, v0, id, v2;
};

struct MorphSet {
	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
};

using TexSet = std::vector<glm::vec2>;

struct Material {
	std::string diffuseName;
	std::string normalName;
	glm::vec3 ambSpecDiff;
};

struct Geometry {

	std::vector<MorphSet> sets;
	std::vector<TexSet> uvs;

	std::vector<Color> colors;
	std::vector<Triangle> faces;

	std::vector<Material> materials;
	std::vector<int32_t> mappings;

};

struct Frame {
	glm::mat4 mat{1.f};
	int32_t parent = -1;
};

struct Model {
	struct Pair {
		unsigned int geometry;
		unsigned int frame;
	};

	std::vector<Geometry> geometries;
	std::vector<Frame> frames;
	std::vector<Pair> pairings;
};
