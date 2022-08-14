
layout(set = 0, binding = 0) uniform EngineData {
	mat4 vp; ///< The transformation matrix.
	mat4 ivp; ///< Inverse
	vec4 color;
	// Shading settings.
	uint shadingMode;
	uint albedoMode;
	// Selection data.
	int selectedMesh;
	int selectedInstance;
	int selectedTextureArray;
	int selectedTextureLayer;
} engine;

#define MATERIAL_OPAQUE 0
#define MATERIAL_DECAL 1
#define MATERIAL_TRANSPARENT 2
// One bit for the shading
#define MODE_SHADING_NONE 0
#define MODE_SHADING_LIGHT 1
// Two bits for the albedo
#define MODE_ALBEDO_UNIFORM 0
#define MODE_ALBEDO_NORMAL 1
#define MODE_ALBEDO_TEXTURE 2

struct MeshInfos {
	uint indexCount;
	uint instanceCount;
	uint firstIndex;
	uint vertexOffset;
	uint firstInstanceIndex;
	uint materialIndex;
	uint pad0, pad1;
};

struct MeshInstanceInfos {
	mat4 frame;
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

struct DrawCommand {
	uint indexCount;
	uint instanceCount;
	uint firstIndex;
	int  vertexOffset;
	uint firstInstance;
};

