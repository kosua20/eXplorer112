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

	float shading = 1.0;
	if(engine.shadingMode == MODE_SHADING_LIGHT){
		vec3 lightDir = normalize(vec3(1.0));
		shading = max(0.0, dot(normalize(In.n), lightDir));
		// Ambient
		shading += 0.1;
	}

	vec3 albedo = engine.color.rgb;
	if(engine.albedoMode == MODE_ALBEDO_TEXTURE){
		MaterialInfos material =  materialInfos[meshInfos[DrawIndex].materialIndex];
		vec4 color = texture(sampler2DArray(textures[material.texture.index], sRepeatLinearLinear), vec3(In.uv,material.texture.layer) );
		if(color.a < 0.05){
			discard;
		}
		albedo = color.rgb;
	} else if(engine.albedoMode == MODE_ALBEDO_NORMAL){
		albedo = normalize(In.n) * 0.5 + 0.5;
	}

	fragColor = vec4(shading * albedo, 1.0);
}
