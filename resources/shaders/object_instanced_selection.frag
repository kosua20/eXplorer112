#extension GL_EXT_nonuniform_qualifier : enable

#include "samplers.glsl"
#include "engine.glsl"

layout(location = 0) in INTERFACE {
	flat uint index;
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

void main(){
	// Avoid 0.
	uint index = In.index + 1u;
	fragColor = vec4(index & 0xFF, (index >> 8) & 0xFF, 0.0f, 0.0f) / 255.0f;
}
