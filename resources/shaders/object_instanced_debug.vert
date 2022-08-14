
#include "engine.glsl"

// Attributes
layout(location = 0) in vec3 v;///< Position.
layout(location = 1) in vec3 n;///< Normal.
layout(location = 2) in vec2 uv;///< UV.

layout(push_constant) uniform constants {
	uint DrawIndex;
};

layout(location = 0) out INTERFACE {
	vec3 n; ///< Normal
	vec2 uv; ///< Texture coordinates.
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

	gl_Position = engine.vp * instance.frame * vec4(v, 1.0);
	Out.uv = uv;
	Out.n = inverse(transpose(mat3(instance.frame))) * n; // For now no transformation

	if(engine.selectedMesh >= 0 && DrawIndex != engine.selectedMesh){
		gl_Position = vec4(10000000.0);
	}
	if(engine.selectedInstance >= 0 && instanceIndex != engine.selectedInstance){
		gl_Position = vec4(10000000.0);
	}
}
