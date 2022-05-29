#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <unordered_set>

struct Color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

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
		std::string name;
	};
	std::vector<Set> faceSets;
	std::string materials;
};

struct ObjOffsets {
	uint32_t v = 0u;
	uint32_t t = 0u;
	uint32_t n = 0u;
};


using TexturesList = std::unordered_set<std::string>;

void writeMtlToStream(const Obj& obj, std::ofstream& mtlFile);

void writeObjToStream(const Obj& obj, std::ofstream& objFile, ObjOffsets & offsets, const glm::mat4& frame);

void writeObjToStream(const Obj& obj, std::ofstream& objFile);
