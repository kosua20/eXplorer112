#pragma once

#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/Common.hpp"
#include "core/Geometry.hpp"
#include "core/WorldParser.hpp"

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
	fs::path materialsPath;

	std::vector<fs::path> worldsList;
	std::vector<fs::path> modelsList;
	std::vector<fs::path> texturesList;
	std::vector<fs::path> templatesList;
	std::vector<fs::path> areasList;
	std::vector<fs::path> materialsList;

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
		TextureInfos color;
		TextureInfos normal;
		uint type;
		uint pad0, pad1, pad2;
	};

	struct LightInfos {
		glm::mat4 vp;
		glm::vec4 positionAndMaxRadius;
		glm::vec4 colorAndType;
		glm::vec4 axisAndRadiusX;
		glm::vec4 axisAndRadiusY;
		glm::vec4 axisAndRadiusZ;
		uint materialIndex;
		uint shadow;
		uint pad1, pad2;
	};

	// CPU data.
	struct MeshCPUInfos {
		std::string name;
		BoundingBox bbox;
	};
	struct InstanceCPUInfos {
		std::string name;
		BoundingBox bbox;
		uint meshIndex;
	};
	struct TextureCPUInfos {
		std::string name;
		TextureInfos data;
	};

public:

	void clean();

	void loadFile(const fs::path& filePath, const GameFiles& files);

	void load(const fs::path& worldPath, const GameFiles& files);

	BoundingBox computeBoundingBox() const;

private:

	struct TextureArrayInfos {
		uint width = 0;
		uint height = 0;
		Image::Compression format = Image::Compression::NONE;
		std::vector<uint> textures;
	};

	void upload(const World& world, const GameFiles& files);

	uint retrieveTexture(const std::string& textureName, const GameFiles& files, std::vector<Texture>& textures2D) const;
	
	TextureInfos storeTexture(const Texture& tex, uint tid, std::vector<TextureArrayInfos>& arraysToCreate) const;

public:

	World world;

	Mesh globalMesh{"None"};
	std::vector<Texture> textures;

	std::unique_ptr<StructuredBuffer<MeshInfos>> meshInfos;
	std::unique_ptr<StructuredBuffer<MeshInstanceInfos>> instanceInfos;
	std::unique_ptr<StructuredBuffer<MaterialInfos>> materialInfos;
	std::unique_ptr<StructuredBuffer<LightInfos>> lightInfos;

	std::vector<MeshCPUInfos> meshDebugInfos;
	std::vector<InstanceCPUInfos> instanceDebugInfos;
	std::vector<TextureCPUInfos> textureDebugInfos;

};
