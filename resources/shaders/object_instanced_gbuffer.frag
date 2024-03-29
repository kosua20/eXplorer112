#extension GL_EXT_nonuniform_qualifier : enable

#include "samplers.glsl"
#include "engine.glsl"

layout(location = 0) in INTERFACE {
	mat4 tbn; ///< Normal to view matrix.
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
layout(location = 1) out vec4 fragNormal; ///< Color.



/** Texture each face. */
void main(){
	MaterialInfos material =  materialInfos[meshInfos[DrawIndex].materialIndex];

	// Build normal using TBN matrix and normal map.
	TextureInfos normalMap = material.normal;
	// Flip the up of the local frame for back facing fragments.
	mat3 tbn = mat3(In.tbn);
	// Compute the normal at the fragment using the tangent space matrix and the normal read in the normal map.
	vec4 normalAndR = texture( sampler2DArray(textures[normalMap.index], sRepeatLinearLinear), vec3(In.uv.xy, normalMap.layer));
	float gloss = normalAndR.a;
	vec3 normal = normalize(normalAndR.xyz * 2.0 - 1.0);
	normal = normalize(tbn * normal);

	// Albedo
	TextureInfos albedoMap = material.color;
	vec4 albedo = texture(sampler2DArray(textures[albedoMap.index], sRepeatLinearLinear), vec3(In.uv.xy, albedoMap.layer));
	// Gamma conversion
	albedo.rgb = pow(albedo.rgb, vec3(2.2));

	// Alpha test.
	if(albedo.a < 0.05){
		discard;
	}

	// Make decals more visible
	if(material.type == MATERIAL_DECAL){
		albedo.rgb *= 0.5;
		// Use the fact that the blending will be min to avoid writing to normal/glossiness buffer.
		normal = vec3(1000.0);
		gloss = 10000.0;
	}

	if(engine.albedoMode == MODE_ALBEDO_UNIFORM){
		albedo = vec4(engine.color.rgb, 1.0);
	} else if(engine.albedoMode == MODE_ALBEDO_NORMAL){
		albedo = vec4(0.5 * normal + 0.5, 1.0);
	}
	// For now output in worldspace.
	fragColor = vec4(albedo.rgb, 1.0);
	fragNormal = vec4(normal, gloss);
}
