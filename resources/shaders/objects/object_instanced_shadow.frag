#extension GL_EXT_nonuniform_qualifier : enable

#include "../engine/samplers.glsl"
#include "../engine/engine.glsl"

layout(location = 0) in INTERFACE {
	vec4 uv;
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

layout(set = 3, binding = 0) uniform texture2DArray textures[]; ///< Color textures.

layout(location = 0) out vec4 fragColor; ///< Color.

/** Texture each face. */
void main(){
	MaterialInfos material =  materialInfos[meshInfos[DrawIndex].materialIndex];

	TextureInfos albedoMap = material.color;
	float alpha = texture(sampler2DArray(textures[albedoMap.index], sRepeatLinearLinear), vec3(In.uv.xy, albedoMap.layer)).a;

	// Alpha test.
	if(alpha < 0.5){
		discard;
	}

}
