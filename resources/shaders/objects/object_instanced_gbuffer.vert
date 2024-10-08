
#include "../engine/engine.glsl"

// Attributes
layout(location = 0) in vec3 v;///< Position.
layout(location = 1) in vec3 n;///< Normal.
layout(location = 2) in vec2 uv;///< UV.
layout(location = 3) in vec3 tang; ///< Tangent.
layout(location = 4) in vec3 bitan; ///< Bitangent.

layout(push_constant) uniform constants {
	uint FirstDrawIndex;
};

layout(location = 0) out INTERFACE {
	mat4 tbn; ///< Normal to view matrix.
	vec4 uvAndHeat; ///< Texture coordinates.
	flat uint DrawIndex;
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

	uint DrawIndex = FirstDrawIndex;
#if !defined(DRAW_ID_FALLBACK)
	DrawIndex += uint(gl_DrawIDARB);
#endif
	
	MeshInfos mesh = meshInfos[DrawIndex];
	uint instanceIndex = drawInstanceInfos[mesh.firstInstanceIndex + gl_InstanceIndex];

	MeshInstanceInfos instance = instanceInfos[instanceIndex];

	vec4 worldPos = instance.frame * vec4(v, 1.0);
	gl_Position = engine.vp * worldPos;
	Out.uvAndHeat.xy = uv;
	Out.uvAndHeat.z = instance.heat;

	// Compute the TBN matrix (from tangent space to view space). Could be stored ahead of time or written by command generation shader if view matrix needs to be taken into account.
	mat3 nMat = inverse(transpose(mat3(instance.frame)));
	vec3 T = (nMat * tang);
	vec3 B = (nMat * bitan);
	vec3 N = (nMat * n);
	Out.tbn = mat4(mat3(T, B, N));
	Out.DrawIndex = DrawIndex;

}
