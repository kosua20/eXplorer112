#pragma once

#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/Common.hpp"
#include "core/Geometry.hpp"

#include "resources/Texture.hpp"
#include "resources/Mesh.hpp"
#include "resources/Buffer.hpp"
#include "Common.hpp"

struct GameFiles {

	GameFiles();

	GameFiles(const fs::path& installPath);

	fs::path resourcesPath;
	fs::path modelsPath;
	fs::path texturesPath;
	fs::path templatesPath;
	fs::path zonesPath;
	fs::path worldsPath;

	std::vector<fs::path> worldsList;
	std::vector<fs::path> modelsList;
	std::vector<fs::path> texturesList;
	std::vector<fs::path> templatesList;
	std::vector<fs::path> areasList;

};

class Scene {
public:

	struct MeshInfos {
		glm::vec4 bboxMin;
		glm::vec4 bboxMax;
		uint indexCount;
		uint instanceCount;
		uint firstIndex;
		uint vertexOffset;
		uint firstInstanceIndex;
		uint materialIndex;
		uint pad0, pad1;
	};

	struct MeshInstanceInfos {
		glm::mat4 frame;
	};

	struct TextureInfos {
		uint index;
		uint layer;
		uint pad0, pad1;
	};

	struct MaterialInfos {
		TextureInfos texture;
	};

public:

	void clean();

	void loadFile(const fs::path& filePath, const GameFiles& files);

	void load(const fs::path& worldPath, const GameFiles& files);

public:

	Mesh globalMesh{"None"};
	std::vector<Texture> textures;

	std::unique_ptr<StructuredBuffer<MeshInfos>> meshInfosBuffer;
	std::unique_ptr<StructuredBuffer<MeshInstanceInfos>> meshInstanceInfosBuffer;
	std::unique_ptr<StructuredBuffer<MaterialInfos>> materialInfosBuffer;

};
