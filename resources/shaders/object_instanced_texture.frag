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

	vec4 albedo = engine.color;

	if(engine.albedoMode == MODE_ALBEDO_TEXTURE){
		MaterialInfos material =  materialInfos[meshInfos[DrawIndex].materialIndex];
		vec4 color = texture(sampler2DArray(textures[material.texture.index], sRepeatLinearLinear), vec3(In.uv,material.texture.layer) );
		if(color.a < 0.05){
			discard;
		}
		albedo = color;

		// Make decals more visible
		if(material.type == MATERIAL_DECAL){
			albedo.rgb *= 0.5;
		}

	} else if(engine.albedoMode == MODE_ALBEDO_NORMAL){
		albedo.rgb = normalize(In.n) * 0.5 + 0.5;
		albedo.a = 1.0;
	}


	fragColor = vec4(shading * albedo.rgb, albedo.a);
}
