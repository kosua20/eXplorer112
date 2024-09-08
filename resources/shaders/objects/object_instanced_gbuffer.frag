#extension GL_EXT_nonuniform_qualifier : enable

#include "../engine/samplers.glsl"
#include "../engine/engine.glsl"

layout(location = 0) in INTERFACE {
	mat4 tbn; ///< Normal to view matrix.
	vec4 uvAndHeat;
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
layout(location = 1) out vec4 fragNormal; ///< Color.
layout(location = 2) out float fragHeat; ///< Color.


/** Texture each face. */
void main(){
	MaterialInfos material =  materialInfos[meshInfos[DrawIndex].materialIndex];
	vec2 uv = In.uvAndHeat.xy;
	// Build normal using TBN matrix and normal map.
	TextureInfos normalMap = material.normal;
	// Flip the up of the local frame for back facing fragments.
	mat3 tbn = mat3(In.tbn);
	// Compute the normal at the fragment using the tangent space matrix and the normal read in the normal map.
	vec4 normalAndR = texture( sampler2DArray(textures[normalMap.index], sRepeatLinearLinear), vec3(uv, normalMap.layer));
	float gloss = normalAndR.a;
	vec3 normal = normalize(normalAndR.xyz * 2.0 - 1.0);
	normal = normalize(tbn * normal);

	// Albedo
	TextureInfos albedoMap = material.color;
	vec4 albedo = texture(sampler2DArray(textures[albedoMap.index], sRepeatLinearLinear), vec3(uv, albedoMap.layer));
	// Gamma conversion
	//albedo.rgb = gammaToLinear(albedo.rgb);
	
	// Alpha test.
	if(albedo.a < 0.5){
		discard;
	}

	if(engine.albedoMode == MODE_ALBEDO_UNIFORM){
		albedo = vec4(engine.color.rgb, 1.0);
	} else if(engine.albedoMode == MODE_ALBEDO_NORMAL){
		albedo = vec4(0.5 * normal + 0.5, 1.0);
	}
	// For now output in worldspace.
	fragColor = vec4(albedo.rgb, 1.0);
	fragNormal = vec4(normal, gloss);

	// Compute heat.
	vec3 normalVS = transpose(mat3(engine.iv)) * tbn[2].xyz;
	float heat = In.uvAndHeat.z;
	fragHeat = clamp(heat * pow(-normalVS.z, 4.0), 0.01, 0.99);
}
