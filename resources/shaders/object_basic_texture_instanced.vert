#extension GL_ARB_shader_draw_parameters : enable

#include "engine.glsl"

// Attributes
layout(location = 0) in vec3 v;///< Position.
layout(location = 1) in vec3 n;///< Normal.
layout(location = 2) in vec2 uv;///< UV.

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

/** Apply the MVP transformation to the input vertex. */
void main(){
	//MeshInfos mesh = meshInfos[0];
	//uint instanceIndex = mesh.firstInstanceIndex + gl_InstanceIndex;
	//MeshInstanceInfos instance = instanceInfos[instanceIndex];

	gl_Position = engine.vp /* instance.frame */ * vec4(v, 1.0);
	Out.uv = uv;
	Out.n = n; // For now no transformation
}
