
layout(set = 0, binding = 0) uniform EngineData {
	mat4 vp; ///< The transformation matrix.
	mat4 ivp; ///< Inverse
	vec4 color;
	int shadingMode;
	int albedoMode;
} engine;


// One bit for the shading
#define MODE_SHADING_NONE 0
#define MODE_SHADING_LIGHT 1
// Two bits for the albedo
#define MODE_ALBEDO_UNIFORM 0
#define MODE_ALBEDO_NORMAL 1
#define MODE_ALBEDO_TEXTURE 2

struct MeshInfos {
	vec4 bboxMin;
	vec4 bboxMax;
	uint indexCount;
	uint instanceCount;
	uint firstIndex;
	uint vertexOffset;
	uint firstInstanceIndex;
	uint pad0, pad1, pad2;
};

struct MeshInstanceInfos {
	mat4 frame;
};

struct DrawCommand {
	uint indexCount;
	uint instanceCount;
	uint firstIndex;
	int  vertexOffset;
	uint firstInstance;
	uint pad0, pad1, pad2;
};

