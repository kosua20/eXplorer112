#extension GL_EXT_nonuniform_qualifier : enable

#include "samplers.glsl"
#include "engine.glsl"

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

	// Albedo
	TextureInfos albedoMap = material.color;
	vec4 albedo = texture(sampler2DArray(textures[albedoMap.index], sRepeatLinearLinear), vec3(In.uv.xy, albedoMap.layer));

	if(engine.albedoMode == MODE_ALBEDO_UNIFORM || engine.albedoMode == MODE_ALBEDO_NORMAL){
		albedo.rgb = vec3(1.0);
	}

	// Hack to avoid ugly overlaps.
	if(all(greaterThan(albedo.rgb, vec3(0.90)))){
		discard;
	}
	fragColor = vec4(albedo.rgb, 1.0);
}
