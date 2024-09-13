
// Blend mode: src alpha * src color + (1 - src alpha) * dst color
vec4 applyFog(vec3 screenPos, vec3 worldPos, vec3 camToPos){

	uvec3 clusterCoords = clusterCellFromScreenspace(screenPos);
	uint flags = imageLoad(fogClusters, ivec3(clusterCoords)).x;

	vec4 totalFog = vec4(0.0);

	for(uint j = 0; j < 16; ++j){
		if((flags & (1u << j)) == 0){
			continue;
		}
		uint zoneIndex = j;
		if(zoneIndex >= engine.zonesCount){
			continue;
		}

		ZoneInfos zone = zoneInfos[zoneIndex];

		// Distance to bounding box.
		vec3 zoneCenter = 0.5 * (zone.bboxMin.xyz + zone.bboxMax.xyz);
		vec3 zoneSize = zone.bboxMax.xyz - zone.bboxMin.xyz;
		vec3 distances = max(abs(worldPos - zoneCenter) - 0.5 * zoneSize, 0.0);
		float distanceToBox = length(distances);
		float ratio = 1.0 - clamp(distanceToBox / ZONE_INFLUENCE_MARGIN, 0.0, 1.0);

		if(ratio <= 0.f){
			continue;
		}
			
		// Height fog.
		vec4 hFogParams = zone.fogParams;
		float heightFog = hFogParams.z * (hFogParams.x - worldPos.y) / hFogParams.y;
		heightFog = clamp(heightFog, 0.0, 1.0); // COLOR outputs are clamped in shader models 1_4 and 2_0.

		// Depth fog.
		float fogDensity = zone.fogColorAndDensity.w;
		vec3 viewDirFog = fogDensity * ( camToPos ) / (2000.0);
		viewDirFog = viewDirFog * 0.5 + 0.5;

		// Sampler for both is Linear and Clamp, force mip 0.
		float fogAttenXY = textureLod(sampler2D(fogXYMap, sClampLinear), viewDirFog.xy, 0.0).x;
		float fogAttenZ  = textureLod(sampler2D(fogZMap, sClampLinear), viewDirFog.zz, 0.0).x;
		float depthFog = 1.0 - (fogAttenXY * fogAttenZ);

		// Final fog compositing.
		vec3 fogColor = zone.fogColorAndDensity.xyz;
		float fogFactor = ratio * clamp(heightFog + depthFog, 0.0, 1.0);
		// Accumulate.
		// sum(i, product(j < i, (1-aj) * (ai * ci));
		// sum(i, product(j < i, (1-aj) * ai);
		totalFog = mix(totalFog, vec4(fogColor, 1.0), fogFactor.xxxx);

	}

	return totalFog;
}
