
// Blend mode: src alpha * src color + (1 - src alpha) * dst color
float applyFog(float worldHeight, vec3 camToPos){
	// Height fog.
	vec4 hFogParams = engine.fogParams;
	float heightFog = hFogParams.z * (hFogParams.x - worldHeight) / hFogParams.y;
	heightFog =  clamp(heightFog, 0.0, 1.0);

	// Depth fog.
	vec3 viewDirFog = engine.fogDensity * ( camToPos ) / (2000.0);
	viewDirFog = viewDirFog * 0.5 + 0.5;
	
	float fogAttenXY = textureLod(sampler2D(fogXYMap, sClampLinear), viewDirFog.xy, 0.0).x;
	float fogAttenZ  = textureLod(sampler2D(fogZMap, sClampLinear), viewDirFog.zz, 0.0).x;
	float depthFog = 1.0 - (fogAttenXY * fogAttenZ);

	// Final fog compositing.
	float fogFactor = clamp(heightFog + depthFog, 0.0, 1.0);
	return fogFactor;
}
