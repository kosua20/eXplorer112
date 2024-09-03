
layout(set = 0, binding = 0) uniform EngineData {
	mat4 v;
	mat4 p; ///< The transformation matrix.
	mat4 vp; ///< The transformation matrix.
	mat4 vpCulling; ///< The transformation matrix.
	mat4 iv; ///< Normal transformation matrix
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
	uint postprocessMode;
	float randomX;
	float randomY;
	float randomZ;

	// Clustering
	uint  lightsCount;
	uvec4 clustersSize; // w, h, d, tile size in w
	vec4  clustersParams;

	// Selection data.
	int selectedMesh;
	int selectedInstance;
	int selectedTextureArray;
	int selectedTextureLayer;
	int skipCulling;
} engine;

#define MATERIAL_OPAQUE 0
#define MATERIAL_DECAL 1
#define MATERIAL_TRANSPARENT 2
#define MATERIAL_LIGHT 3

// One bit for the shading
#define MODE_SHADING_NONE 0
#define MODE_SHADING_LIGHT 1
// Two bits for the albedo
#define MODE_ALBEDO_UNIFORM 0
#define MODE_ALBEDO_NORMAL 1
#define MODE_ALBEDO_TEXTURE 2
// Bit field for postprocess
#define MODE_POSTPROCESS_BLOOM 1
#define MODE_POSTPROCESS_GRAIN 2

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


mat4 lookAt(vec3 eye, vec3 center, vec3 up){
	vec3 f = normalize(center - eye);
	vec3 s = normalize(cross(f, up));
	vec3 u = cross(s, f);

	mat4 Result = mat4(1.0);
	Result[0][0] = s.x;
	Result[1][0] = s.y;
	Result[2][0] = s.z;
	Result[0][1] = u.x;
	Result[1][1] = u.y;
	Result[2][1] = u.z;
	Result[0][2] =-f.x;
	Result[1][2] =-f.y;
	Result[2][2] =-f.z;
	Result[3][0] =-dot(s, eye);
	Result[3][1] =-dot(u, eye);
	Result[3][2] = dot(f, eye);
	return Result;
}

mat4 translate(mat4 m, vec3 v){
	mat4 Result = m;
	Result[3] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3];
	return Result;
}


vec3 gammaToLinear(vec3 gamma){
	return mix(gamma/12.92, pow((gamma + 0.055)/1.055, vec3(2.4)), lessThanEqual(gamma, vec3(0.04045)));
}

vec3 linearToGamma(vec3 linear){
	return mix(12.92 * linear, 1.055 * pow(linear, vec3(1.0/2.4)) - 0.055, lessThanEqual(linear, vec3(0.0031308)));
}

vec3 worldPositionFromDepth(float depth, vec2 pix){
	// Linearize depth -> in view space.
	float viewDepth =  -engine.p[3][2] / (depth + engine.p[2][2]);
	// Compute the x and y components in view space.
	vec2 ndcPos = 2.0 * (pix / engine.resolution.xy) - 1.0;
	vec3 viewPos = vec3(-ndcPos * viewDepth / vec2(engine.p[0][0], engine.p[1][1]) , viewDepth);
	return vec3(engine.iv * vec4(viewPos, 1.0));
}