
#include "engine.glsl"

// Attributes
layout(location = 0) in vec3 v;///< Position.
layout(location = 2) in vec2 uv;///< UV.

layout(push_constant) uniform constants {
	uint DrawIndex;
};

layout(location = 0) out INTERFACE {
	vec4 uv; ///< Texture coordinates.
} Out;


layout(std140, set = 0, binding = 1) readonly buffer MeshesInfos {
	MeshInfos meshInfos[];
};

layout(std140, set = 0, binding = 2) readonly buffer InstancesInfos {
	MeshInstanceInfos instanceInfos[];
};

layout(set = 0, binding = 4) readonly buffer InstanceDrawInfos {
	uint drawInstanceInfos[];
};

/** Apply the MVP transformation to the input vertex. */
void main(){
	MeshInfos mesh = meshInfos[DrawIndex];
	uint instanceIndex = drawInstanceInfos[mesh.firstInstanceIndex + gl_InstanceIndex];

	MeshInstanceInfos instance = instanceInfos[instanceIndex];

	vec4 worldPos = instance.frame * vec4(v, 1.0);
	gl_Position = engine.vp * worldPos;
	Out.uv.xy = uv;

}
