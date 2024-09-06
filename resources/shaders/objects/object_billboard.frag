#extension GL_EXT_nonuniform_qualifier : enable

#include "../engine/samplers.glsl"
#include "../engine/engine.glsl"

layout(location = 0) in INTERFACE {
	vec3 color;
	vec2 uv;
	flat uint material;
} In ;

layout(std140, set = 0, binding = 1) readonly buffer MaterialsInfos {
	MaterialInfos materialInfos[];
};

layout(set = 3, binding = 0) uniform texture2DArray textures[]; ///< Color textures.

layout(location = 0) out vec4 fragColor; ///< Color.

/** Texture each face. */
void main(){

	MaterialInfos material =  materialInfos[In.material];
	// Albedo
	TextureInfos albedoMap = material.color;
	vec4 albedo = texture(sampler2DArray(textures[albedoMap.index], sRepeatLinearLinear), vec3(In.uv.xy, albedoMap.layer));

	albedo.rgb *= In.color.xyz;

	if(engine.albedoMode == MODE_ALBEDO_UNIFORM || engine.albedoMode == MODE_ALBEDO_NORMAL){
		albedo.rgb = vec3(0.0);
	}

	fragColor = albedo;
}
