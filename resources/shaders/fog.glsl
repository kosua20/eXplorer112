

float applyFog(float worldHeight, vec3 viewDir){
	// TODO: fog density is not exactly right.
	// Blend mode: src alpha * src color + (1 - src alpha) * dst color
	// For the back floor, parameters
	// * fogParams: 0 0 1.5 2000
	// * hfogParams: 10 20 0.5 0
	// * fogColor: 0.11765 0.09804 0.01961 1.0
	// src color = fog color
	// uv0
	// uv1
	// uv2
	// src alpha = ( 0.5 >= colorMap(uv0).a ? 0 : sat(fogXY(uv1).z * (-fogZ(uv2).z) + v0.w+1))


	// Height fog.
	vec4 hFogParams = engine.fogParams;
	float heightFog = hFogParams.z * (hFogParams.x - worldHeight) / hFogParams.y;
	heightFog = 0.0 * clamp(heightFog, 0.0, 1.0);

	// Depth fog.
	vec3 viewDirFog = 0.5 * engine.fogDensity * ( -viewDir ) / (1000.0);
	viewDirFog = viewDirFog * 0.5 + 0.5;
	float fogAttenXY = textureLod(sampler2D(fogXYMap, sClampLinear), viewDirFog.xy, 0.0).r;
	float fogAttenZ  = textureLod(sampler2D( fogZMap, sClampLinear), viewDirFog.zz, 0.0).r;
	float depthFog = 1.0 - (fogAttenXY * fogAttenZ);

	// Final fog compositing.
	float fogFactor = clamp(heightFog + depthFog, 0.0, 1.0);
	return fogFactor;
}
