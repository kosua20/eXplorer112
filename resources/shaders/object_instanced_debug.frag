#extension GL_EXT_nonuniform_qualifier : enable

#include "samplers.glsl"
#include "engine.glsl"

layout(location = 0) in INTERFACE {
	vec3 n;
	vec2 uv;
} In ;

layout(push_constant) uniform constants {
	uint DrawIndex;
};

layout(std140, set = 0, binding = 1) readonly buffer MeshesInfos {
	MeshInfos meshInfos[];
};

layout(std140, set = 0, binding = 3) readonly buffer MaterialsInfos {
	MaterialInfos materialInfos[];
};

layout(location = 0) out vec4 fragColor; ///< Color.

/** Texture each face. */
void main(){

	fragColor = vec4(engine.color.rgb, 1.0);
}
