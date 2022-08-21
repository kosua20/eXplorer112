#extension GL_EXT_nonuniform_qualifier : enable

#include "samplers.glsl"
#include "engine.glsl"

layout(location = 0) in INTERFACE {
	mat4 tbn; ///< Normal to view matrix.
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

layout(set = 3, binding = 0) uniform texture2DArray textures[]; ///< Color textures.

layout(location = 0) out vec4 fragColor; ///< Color.

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
	albedo.rgb = pow(albedo.rgb, vec3(2.2));
	// Make decals more visible
	if(material.type == MATERIAL_DECAL){
		albedo.rgb *= 0.5;
	}

	// Alpha test.
	if(albedo.a < 0.05){
		discard;
	}

	vec3 ambient = vec3(0.0);
	float diffuse = 1.0;
	float specular = 0.0;

	if(engine.shadingMode == MODE_SHADING_LIGHT){
		vec3 l = normalize(vec3(1.0, 1.0, 1.0)); // World space for now
		vec3 v = normalize(In.viewDir.xyz);
		// Ambient. TODO: fetch based on position relative to list of areas, area index passed from the vertex shader, or reading a 3D texture containing indices.
		ambient = engine.ambientColor.rgb;
		// Diffuse (based on game shader)
		diffuse = dot(n, l) * 0.5 + 0.5;
		diffuse *= diffuse;
		// Specular (based on game shader)
		specular = pow(clamp(dot(reflect(v, n), -l), 0.0, 1.0), 4.0);
		specular *= normalAndR.a;
	}

	vec4 color = engine.color;
	if(engine.albedoMode == MODE_ALBEDO_TEXTURE){
		color = albedo;
	} else if(engine.albedoMode == MODE_ALBEDO_NORMAL){
		color.rgb = n * 0.5 + 0.5;
		color.a = 1.0;
	}


	fragColor = vec4(diffuse * color.rgb + specular, color.a);
}
