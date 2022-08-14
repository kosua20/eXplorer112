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

layout(set = 3, binding = 0) uniform texture2DArray textures[]; ///< Color textures.

layout(location = 0) out vec4 fragColor; ///< Color.

/** Texture each face. */
void main(){
	MaterialInfos material =  materialInfos[meshInfos[DrawIndex].materialIndex];


	vec4 normalAndR = texture( sampler2DArray(textures[normalMap.index], sRepeatLinearLinear), vec3(In.uv.xy, normalMap.layer));
	vec3 n = normalize(normalAndR.xyz * 2.0 - 1.0);
	// Albedo
	TextureInfos albedoMap = material.color;
	vec4 albedo = texture(sampler2DArray(textures[albedoMap.index], sRepeatLinearLinear), vec3(In.uv.xy, albedoMap.layer));
	// Make decals more visible
	if(material.type == MATERIAL_DECAL){
		albedo.rgb *= 0.5;
	}

	// Alpha test.
	if(albedo.a < 0.05){
		discard;
	}

	float shading = 1.0;
	if(engine.shadingMode == MODE_SHADING_LIGHT){
		vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0)); // World space for now
		shading = max(0.0, dot(n, lightDir));
		// Ambient
		shading += 0.1;
	}

	vec4 color = engine.color;
	if(engine.albedoMode == MODE_ALBEDO_TEXTURE){
		color = albedo;
	} else if(engine.albedoMode == MODE_ALBEDO_NORMAL){
		color.rgb = n * 0.5 + 0.5;
		color.a = 1.0;
	}


	fragColor = vec4(shading * color.rgb, color.a);
}
