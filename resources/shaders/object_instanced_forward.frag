#extension GL_EXT_nonuniform_qualifier : enable

#include "samplers.glsl"
#include "engine.glsl"

layout(location = 0) in INTERFACE {
	mat4 tbn; ///< Normal to view matrix.
	vec4 worldPos;
	vec4 viewDir;
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

layout(std140, set = 0, binding = 5) readonly buffer LightsInfos {
	LightInfos lightInfos[];
};

layout(set = 2, binding = 0) uniform texture2D fogXYMap;
layout(set = 2, binding = 1) uniform texture2D fogZMap;
layout(set = 2, binding = 2, rgba32ui) uniform readonly uimage3D lightClusters;
layout(set = 2, binding = 3) uniform texture2DArray shadowMaps;
layout(set = 3, binding = 0) uniform texture2DArray textures[]; ///< Color textures.

layout(location = 0) out vec4 fragColor; ///< Color.

#include "lighting.glsl"
#include "fog.glsl"

/** Texture each face. */
void main(){
	MaterialInfos material =  materialInfos[meshInfos[DrawIndex].materialIndex];

	// Build normal using TBN matrix and normal map.
	TextureInfos normalMap = material.normal;
	// Flip the up of the local frame for back facing fragments.
	mat3 tbn = mat3(In.tbn);
	// Compute the normal at the fragment using the tangent space matrix and the normal read in the normal map.
	vec4 normalAndR = texture( sampler2DArray(textures[normalMap.index], sRepeatLinearLinear), vec3(In.uv.xy, normalMap.layer));
	vec3 n = normalize(normalAndR.xyz * 2.0 - 1.0);
	n = normalize(tbn * n);

	// Albedo
	TextureInfos albedoMap = material.color;
	vec4 albedo = texture(sampler2DArray(textures[albedoMap.index], sRepeatLinearLinear), vec3(In.uv.xy, albedoMap.layer));
	// Gamma conversion
	//albedo.rgb = gammaToLinear(albedo.rgb);

	// Override albedo.
	vec4 color = engine.color;
	if(engine.albedoMode == MODE_ALBEDO_TEXTURE){
		color = albedo;
	} else if(engine.albedoMode == MODE_ALBEDO_NORMAL){
		color.rgb = n * 0.5 + 0.5;
		color.a = 1.0;
	}

	vec3 ambient = vec3(0.0);
	vec3 diffuse = vec3(1.0);
	vec3 specular = vec3(0.0);

	if(engine.shadingMode == MODE_SHADING_LIGHT){
		// Ambient.
		ambient  = engine.ambientColor.rgb;
		diffuse  = vec3(0.0);
		specular = vec3(0.0);

		applyLighting(gl_FragCoord.xyz, In.worldPos.xyz, In.viewDir.xyz, n, normalAndR.a, diffuse, specular);
	}

	vec3 finalColor = (diffuse + ambient) * color.rgb + specular;

	// Fog
	float fogFactor = applyFog(In.worldPos.y, -In.viewDir.xyz);
	finalColor = mix(finalColor, engine.fogColor.rgb, fogFactor);

	fragColor = vec4(finalColor, color.a);

}
