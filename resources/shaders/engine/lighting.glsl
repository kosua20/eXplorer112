
float shadow(vec3 lightSpacePosition, uint layer, float bias){
	if(any(greaterThan(abs(lightSpacePosition.xy-0.5), vec2(0.5)))){
		return 1.0;
	}
	float refDepth = textureLod(sampler2DArray(shadowMaps, sClampNear), vec3(lightSpacePosition.xy, layer), 0).r;
	float curDepth = lightSpacePosition.z;
	return float(curDepth > refDepth - bias);
}

float shadowPCF(vec3 lightSpacePosition, uint layer, float bias){
	// Avoid shadows when falling outside the shadow map.
	if(any(greaterThan(abs(lightSpacePosition.xy-0.5), vec2(0.5)))){
		return 1.0;
	}

	vec2 texSize = vec2(512.0);
	vec2 texelSize = 1.0 / texSize;

	float totalOcclusion = 0.0;
	float totalWeight = 0.0;

	// Read first and second moment from shadow map.
	for(int y = -1; y <= 1; y += 1){
		for(int x = -1; x <= 1; x += 1){

			vec2 coords = lightSpacePosition.xy + vec2(x,y) * texelSize;
			vec4 depths = textureGather(sampler2DArray(shadowMaps, sClampNear), vec3(coords, layer), 0);
			bvec4 valids = greaterThan(depths, vec4(0.0));
			float invWeight = (abs(x)+abs(y)) * 0.1 + 1.0;
			bvec4 visibles = greaterThanEqual(lightSpacePosition.zzzz, depths - invWeight * bias);
			vec4 occlusions = mix(vec4(1.0), vec4(visibles), valids);

			vec2 texelPos = coords * texSize - 0.5;
			vec2 texelFloor = floor(texelPos);
			vec2 texelFrac = texelPos - texelFloor;

			float mix0 = mix(occlusions.w, occlusions.z, texelFrac.x);
			float mix1 = mix(occlusions.x, occlusions.y, texelFrac.x);

			totalOcclusion += mix(mix0, mix1, texelFrac.y);
			totalWeight += 1.0;
		}
	}
	totalOcclusion /= totalWeight;
	return totalOcclusion;
}


void applyLighting(vec3 screenPos, vec3 worldPos, vec3 viewDir, vec3 n, float gloss, bool allowShadows, out vec3 diffuse, out vec3 specular){

	diffuse = vec3(0.0);
	specular = vec3(0.0);

	vec3 v = normalize(viewDir);
	uvec3 clusterCoords = clusterCellFromScreenspace(screenPos);
	uvec4 flags = imageLoad(lightClusters, ivec3(clusterCoords));

	for(uint i = 0; i < 4; ++i){
		// Early exit :D
		if(flags[i] == 0){
		//	continue;
		}

		for(uint j = 0; j < 32; ++j){
			if((flags[i] & (1u << j)) == 0){
				continue;
			}
			uint lightIndex = 32 * i + j;
			if(lightIndex >= engine.lightsCount){
				continue;
			}
			LightInfos light = lightInfos[lightIndex];
			vec3 lightPos = light.positionAndMaxRadius.xyz;
			float lightRad = light.positionAndMaxRadius.w;
			vec3 lightColor = light.colorAndType.xyz;
			uint lightType = uint(light.colorAndType.w);

			vec3 posToLight;
			if(lightType == 1 || lightType == 2){ // Point & Spot
				posToLight = (lightPos - worldPos);
			} else { // 3
				posToLight = normalize(-light.axisAndRadiusZ.xyz);
			}
			vec3 l = normalize(posToLight);
			float dotNL = dot(l, n);

			mat4 projMatrix = light.vp;
			uint lightShadowIndex = allowShadows ? light.shadow : NO_SHADOW;

			if(lightType == 1 && (lightShadowIndex != NO_SHADOW || light.materialIndex != NO_MATERIAL)){
				// Translate point in light centered space.
				vec3 localPos = worldPos - lightPos;
				vec3 localPosAbs = abs(localPos);
				float maxAbsCoord = max(localPosAbs.x, max(localPosAbs.y, localPosAbs.z));
				uint faceIndex = 0u;

				vec3 faceUp = vec3(0.0,1.0,0.0);
				vec3 faceCenter = vec3(0.0,0.0,0.0);

				if(localPosAbs.x == maxAbsCoord){
					faceIndex = localPos.x < 0.f ? 0 : 1;
				} else if(localPosAbs.y == maxAbsCoord){
					faceIndex = localPos.y < 0.f ? 2 : 3;
					faceUp = vec3(0.0,0.0,1.0);
				} else {
					faceIndex = localPos.z < 0.f ? 4 : 5;
				}
				faceCenter[faceIndex / 2] = faceIndex % 2 == 0 ? -1.f : 1.f;
				lightShadowIndex += faceIndex;
				mat4 localView = lookAt(vec3(0.0,0.0,0.0), faceCenter, faceUp);
				projMatrix = projMatrix * translate(localView, -lightPos);
			}

			vec4 projectedPos = projMatrix * vec4(worldPos, 1.0);
			projectedPos.xyz /= projectedPos.w;
			vec2 projectedUV = projectedPos.xy * 0.5 + 0.5;
			
			float attenuation = 1.0;
			if(lightShadowIndex != NO_SHADOW){
				
				float f = dotNL * 0.5f + 0.5f;
				float bias = 0.0005 * mix(10.0, 0.5, f);
				vec3 lightSpacePos = vec3(projectedUV, projectedPos.z);
				float shadowing = shadowPCF(lightSpacePos, lightShadowIndex, bias);
				attenuation *= shadowing;
			}


			if(lightType == 1 || lightType == 2){ // Point & Spot
				
				// Attenuation along the local axes (pre-divided by the radii)
				vec3 attenAxes;
				attenAxes.x = dot(light.axisAndRadiusX.xyz, posToLight);
				attenAxes.y = dot(light.axisAndRadiusY.xyz, posToLight);
				attenAxes.z = dot(light.axisAndRadiusZ.xyz, posToLight);

				// Compute attenuation (based on game shader)
				attenuation *= 1.0 - clamp(dot(attenAxes, attenAxes), 0.0, 1.0);
				
				if((lightType == 2)){
					if(any(greaterThan(abs(projectedPos.xy), vec2(1.0))) || projectedPos.w < 0.0 ){
						attenuation *= 0.0;
					}
				}

				if(light.materialIndex != NO_MATERIAL){
					MaterialInfos lightMaterial =  materialInfos[light.materialIndex];
					vec3 lightUV = vec3(projectedUV, lightMaterial.color.layer);
					vec4 lightPattern = texture(sampler2DArray(textures[lightMaterial.color.index], sRepeatLinearLinear), lightUV);
					lightColor *= lightPattern.rgb;
				}
			} 

			// Diffuse (based on game shader)
			float localDiffuse = dotNL * 0.5 + 0.5;
			localDiffuse *= localDiffuse;
			diffuse += localDiffuse * attenuation * lightColor;
			// Specular (based on game shader)
			float localSpecular = pow(clamp(dot(reflect(v, n), -l), 0.0, 1.0), 4.0);
			localSpecular *= localDiffuse; // ?
			specular += localSpecular * attenuation * lightColor;

		}

	}

	// Modulate based on 'glossiness' stored in alpha channel of normal map.
	specular *= gloss;
}
