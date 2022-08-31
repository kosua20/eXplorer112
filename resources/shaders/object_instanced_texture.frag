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
	vec3 diffuse = vec3(1.0);
	vec3 specular = vec3(0.0);

	if(engine.shadingMode == MODE_SHADING_LIGHT){
		vec3 v = normalize(In.viewDir.xyz);
		// Ambient. TODO: fetch based on position relative to list of areas, area index passed from the vertex shader, or reading a 3D texture containing indices.
		ambient  = engine.ambientColor.rgb;
		diffuse  = vec3(0.0);
		specular = vec3(0.0);

		uvec3 clusterCoords = clusterCellFromScreenspace(gl_FragCoord.xyz);
		uvec4 flags = imageLoad(lightClusters, ivec3(clusterCoords));
		uint count = 0;
		for(uint i = 0; i < 4; ++i){
			// Early exit :D
			if(flags[i] == 0){
				continue;
			}

			for(uint j = 0; j < 32; ++j){
				if((flags[i] & (1u << j)) == 0){
					continue;
				}
				uint lightIndex = 32 * i + j;
				if(lightIndex >= engine.lightsCount){
					break;
				}
				LightInfos light = lightInfos[lightIndex];
				vec3 lightPos = light.positionAndMaxRadius.xyz;
				float lightRad = light.positionAndMaxRadius.w;
				vec3 lightColor = light.colorAndType.xyz;
				uint lightType = uint(light.colorAndType.w);

				vec3 l = vec3(0.0);

				float attenuation = 12000.0;
				if(lightType == 1 || lightType == 2){ // Point & Spot
					l = (lightPos - In.worldPos.xyz);
					// Attenuation along the local axes (pre-divided by the radii)
					vec3 attenAxes;
					attenAxes.x = dot(light.axisAndRadiusX.xyz, l);
					attenAxes.y = dot(light.axisAndRadiusY.xyz, l);
					attenAxes.z = dot(light.axisAndRadiusZ.xyz, l);

					// Compute attenuation (based on game shader)
					attenuation = 1.0 - clamp(dot(attenAxes, attenAxes), 0.0, 1.0);
					l = normalize(l);


					if((lightType == 2)){
						vec4 projectedPos = light.vp * vec4(In.worldPos.xyz, 1.0);
						projectedPos.xy /= projectedPos.w;
						if(any(greaterThan(abs(projectedPos.xy), vec2(1.0))) || projectedPos.z >= 0.0){
							attenuation = 0.0;
						}

						if(light.materialIndex != NO_MATERIAL){
							MaterialInfos lightMaterial =  materialInfos[light.materialIndex];

							vec3 lightUV = vec3(projectedPos.xy * 0.5 + 0.5, lightMaterial.color.layer);
							vec4 lightPattern = texture(sampler2DArray(textures[lightMaterial.color.index], sRepeatLinearLinear), lightUV);
							lightColor *= lightPattern.rgb;

						}

					}

				} else if(lightType == 3){ // Directional
					attenuation = 1.0;
					l = normalize(-light.axisAndRadiusZ.xyz);
				}


				// Diffuse (based on game shader)
				float localDiffuse = dot(n, l) * 0.5 + 0.5;
				localDiffuse *= localDiffuse;
				diffuse += localDiffuse * attenuation * lightColor;
				// Specular (based on game shader)
				float localSpecular = pow(clamp(dot(reflect(v, n), -l), 0.0, 1.0), 4.0);
				localSpecular *= localDiffuse; // ?
				specular += localSpecular * attenuation * lightColor;
			}
		}
		// Modulate based on 'roughness' stored in alpha channel of normal map.
		specular *= normalAndR.a;
	}

	vec4 color = engine.color;
	if(engine.albedoMode == MODE_ALBEDO_TEXTURE){
		color = albedo;
	} else if(engine.albedoMode == MODE_ALBEDO_NORMAL){
		color.rgb = n * 0.5 + 0.5;
		color.a = 1.0;
	}

	fragColor = vec4((diffuse + ambient) * color.rgb + specular, color.a);
	// Height fog.
	vec4 hFogParams = engine.fogParams;
	float heightFog = hFogParams.z * (hFogParams.x-In.worldPos.y) / hFogParams.y;
	heightFog = clamp(heightFog, 0.0, 1.0);
	// Depth fog.
	vec3 viewDirFog = 0.5 * engine.fogDensity * ( -In.viewDir.xyz ) / (1000.0);
	viewDirFog = viewDirFog * 0.5 + 0.5;
	float fogAttenXY = textureLod(sampler2D(fogXYMap, sClampLinear), viewDirFog.xy, 0.0).r;
	float fogAttenZ  = textureLod(sampler2D( fogZMap, sClampLinear), viewDirFog.zz, 0.0).r;
	float depthFog = 1.0 - (fogAttenXY * fogAttenZ);
	// Final fog compositing.
	float fogFactor = clamp(heightFog + depthFog, 0.0, 1.0);
	fragColor.rgb = mix(fragColor.rgb, engine.fogColor.rgb, fogFactor);

}
