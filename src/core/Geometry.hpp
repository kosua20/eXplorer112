#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <fstream>

struct Color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

namespace Dff {

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

}

struct Obj {
	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
	std::vector<glm::uvec3> colors;
	std::vector<glm::vec2> uvs;

	struct Set {

		struct Face {
			uint32_t v0 = 0, v1 = 0, v2 = 0;
			uint32_t t0 = 0xFFFF, t1 = 0xFFFF, t2 = 0xFFFF;
			uint32_t n0 = 0xFFFF, n1 = 0xFFFF, n2 = 0xFFFF;
		};

		std::vector<Face> faces;
		std::string material;
	};
	std::vector<Set> faceSets;
	std::string materials;
};

struct ObjOffsets {
	uint32_t v = 0u;
	uint32_t t = 0u;
	uint32_t n = 0u;
};

void writeMtlToStream(const Obj& obj, std::ofstream& mtlFile);

void writeObjToStream(const Obj& obj, std::ofstream& objFile, ObjOffsets & offsets, const glm::mat4& frame);

void writeObjToStream(const Obj& obj, std::ofstream& objFile);
