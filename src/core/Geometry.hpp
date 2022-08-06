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

struct Object {
	std::string name;
	
	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
	std::vector<glm::uvec3> colors;
	std::vector<glm::vec2> uvs;

	struct Set {

		struct Face {
			static const uint32_t INVALID = 0xFFFF;

			uint32_t v0 = 0, v1 = 0, v2 = 0;
			uint32_t t0 = INVALID, t1 = INVALID, t2 = INVALID;
			uint32_t n0 = INVALID, n1 = INVALID, n2 = INVALID;
			uint32_t c0 = INVALID, c1 = INVALID, c2 = INVALID;
		};

		std::vector<Face> faces;
		uint32_t material;
	};

	struct Material {
		// For now, only diffuse texture.
		std::string texture;
	};

	std::vector<Set> faceSets;
	std::vector<Material> materials;
};

struct ObjOffsets {
	uint32_t v = 0u;
	uint32_t t = 0u;
	uint32_t n = 0u;
};


using TexturesList = std::unordered_set<std::string>;

void writeMtlToStream(const Object& obj, std::ofstream& mtlFile);

void writeObjToStream(const Object& obj, std::ofstream& objFile, ObjOffsets & offsets, const glm::mat4& frame);

void writeObjToStream(const Object& obj, std::ofstream& objFile);
