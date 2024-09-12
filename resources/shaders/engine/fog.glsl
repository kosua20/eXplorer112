
// Blend mode: src alpha * src color + (1 - src alpha) * dst color
vec4 applyFog(vec3 screenPos, vec3 worldPos, vec3 camToPos){

	uvec3 clusterCoords = clusterCellFromScreenspace(screenPos);
	uint flags = imageLoad(fogClusters, ivec3(clusterCoords)).x;

	vec4 fogParams = vec4(0.0);
	float fogDensity = 0.0;
	vec3 fogColor = vec3(0.0);
	vec4 totalFog = vec4(0.0);
	// For now, just pick the first we're inside.
	{
		for(uint j = 0; j < 32; ++j){
			if((flags & (1u << j)) == 0){
				continue;
			}
			uint zoneIndex = j;
			if(zoneIndex >= engine.zonesCount){
				continue;
			}
			ZoneInfos zone = zoneInfos[zoneIndex];
			if(all(greaterThanEqual(worldPos, zone.bboxMin.xyz)) && all(lessThanEqual(worldPos, zone.bboxMax.xyz))){
				fogParams = zone.fogParams;
				fogColor = zone.fogColorAndDensity.xyz;
				fogDensity = zone.fogColorAndDensity.w;

				// Height fog.
				vec4 hFogParams = fogParams;
				float heightFog = hFogParams.z * (hFogParams.x - worldPos.y) / hFogParams.y;
				heightFog = clamp(heightFog, 0.0, 1.0); // COLOR outputs are clamped in shader models 1_4 and 2_0.

				// Depth fog.
				vec3 viewDirFog = fogDensity * ( camToPos ) / (2000.0);
				viewDirFog = viewDirFog * 0.5 + 0.5;

				// Sampler for both is Linear and Clamp, force mip 0.
				float fogAttenXY = textureLod(sampler2D(fogXYMap, sClampLinear), viewDirFog.xy, 0.0).x;
				float fogAttenZ  = textureLod(sampler2D(fogZMap, sClampLinear), viewDirFog.zz, 0.0).x;
				float depthFog = 1.0 - (fogAttenXY * fogAttenZ);

				// Final fog compositing.
				float fogFactor = clamp(heightFog + depthFog, 0.0, 1.0);
				totalFog = mix(totalFog, vec4(fogColor, 1.0), fogFactor.xxxx);
			}
		}
	}


	return totalFog;
}
