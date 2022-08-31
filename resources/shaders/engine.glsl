
layout(set = 0, binding = 0) uniform EngineData {
	mat4 v;
	mat4 vp; ///< The transformation matrix.
	mat4 vpCulling; ///< The transformation matrix.
	mat4 ip; ///< Inverse
	mat4 nvp; ///< Normal transformation matrix
	vec4 resolution;
	vec4 color;
	vec4 camPos;
	vec4 camPlanes; // n, f/n, (f-n)/(f*n), 1/f

	vec4 ambientColor;
	vec4 fogColor;
	vec4 fogParams;
	float fogDensity;

	// Shading settings.
	uint shadingMode;
	uint albedoMode;

	// Clustering
	uint  lightsCount;
	uvec4 clustersSize; // w, h, d, tile size in w
	vec4  clustersParams;

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

const uint NO_MATERIAL = 0xFFFF;
const uint NO_SHADOW = 0xFFFF;

struct MeshInfos {
	vec4 bboxMin;
	vec4 bboxMax;
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

struct LightInfos {
	mat4 vp;
	vec4 positionAndMaxRadius;
	vec4 colorAndType;
	vec4 axisAndRadiusX;
	vec4 axisAndRadiusY;
	vec4 axisAndRadiusZ;
	uint materialIndex;
	uint shadow;
	uint pad1, pad2;
};

struct DrawCommand {
	uint indexCount;
	uint instanceCount;
	uint firstIndex;
	int  vertexOffset;
	uint firstInstance;
};


uvec3 clusterCellFromScreenspace(vec3 fragCoord){
	float invViewDepth = fragCoord.z * engine.camPlanes.z + engine.camPlanes.w;
	float zRange = -log(invViewDepth) * engine.clustersParams.x - engine.clustersParams.y;
	uint zSlice = uint(floor(zRange));
	uvec2 xyCell = uvec2(fragCoord.xy - 0.5) / engine.clustersSize.w;
	return uvec3(xyCell.x, engine.clustersSize.y - 1 - xyCell.y, zSlice);
}
