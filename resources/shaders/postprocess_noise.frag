#include "engine/samplers.glsl"
#include "engine/engine.glsl"


layout(location = 0) in INTERFACE {
	vec2 uv; ///< UV coordinates.
} In ;

layout(set = 2, binding = 0) uniform texture2D screenTexture;
layout(set = 2, binding = 1) uniform texture2D bloomTexture;
layout(set = 2, binding = 2) uniform texture2D grainNoiseTexture;
layout(set = 2, binding = 3) uniform texture2DArray nightNoisePulseTexture;
layout(set = 2, binding = 4) uniform texture2D heatMapTexture;
layout(set = 2, binding = 5) uniform texture2D heatLookupTexture;

layout(location = 0) out vec4 fragColor; ///< Color.

/** Just pass the input image as-is, potentially performing up/down scaling. */
void main(){
	bool wantsBloom = (engine.postprocessMode & MODE_POSTPROCESS_BLOOM) != 0u;
	bool wantsGrain = (engine.postprocessMode & MODE_POSTPROCESS_GRAIN) != 0u;
	bool wantsNight = (engine.postprocessMode & MODE_POSTPROCESS_NIGHT) != 0u;
	bool wantsBAndW = (engine.postprocessMode & MODE_POSTPROCESS_BANDW) != 0u;
	bool wantsJitter  = (engine.postprocessMode & MODE_POSTPROCESS_JITTER) != 0u;
	bool wantsHeat  = (engine.postprocessMode & MODE_POSTPROCESS_HEAT) != 0u;
	bool wantsUnderwater = (engine.postprocessMode & MODE_POSTPROCESS_UNDERWATER) != 0u;

	vec2 initialUV = In.uv;
	if(wantsJitter){
		vec2 uv1 = vec2(engine.randomX, In.uv.y * 8.0);
		float noise = textureLod(sampler2D(grainNoiseTexture, sRepeatLinear), uv1, 0.0).x - 0.5;
		noise *= abs(In.uv.x*2.0-1.0)*abs(In.uv.y*2.0-1.0);
		float s = In.uv.x + noise * 0.5;
		float t = In.uv.y + engine.randomY * engine.randomZ;
		if( t > 1.0 ) t -= 1.0;
		initialUV = vec2(s,t);
	}

	if(wantsUnderwater){
		// TODO: maybe use noisevolume instead?
		vec3 distortion =  textureLod(sampler2D(grainNoiseTexture, sRepeatLinear), 0.5 * In.uv, 0.0).rgb * 2.0 - 1.0;
		initialUV = In.uv + 0.01 * distortion.xy;
	}

	vec3 baseColor = textureLod(sampler2D(screenTexture, sClampLinear), initialUV, 0.0).rgb;


	// Bloom
	if(wantsBloom){
		vec3 bloomColor = textureLod(sampler2D(bloomTexture, sClampLinear), initialUV, 0.0).rgb;
		baseColor += 0.5 * bloomColor;
	}

	if(wantsUnderwater){
		baseColor *= vec3(0.5, 0.55, 0.8);
	}

	if(wantsHeat){
		vec3 fLuminance = vec3(0.2125, 0.7154, 0.0721);
		vec3 lightMap = baseColor;
		vec3 thermalMap = textureLod(sampler2D(heatMapTexture, sClampLinear), initialUV, 0.0).rgb;
		float l = dot(lightMap, fLuminance) * 0.25 + thermalMap.r * 0.85;
		float c = clamp(l, 0.01, 0.99);
		vec3 thermal = textureLod(sampler2D(heatLookupTexture, sClampLinear), vec2(c, 0.5), 0.0).rgb;
		baseColor = thermal;
	}
	if(wantsBAndW){
		vec3 fLuminance = vec3(0.2125, 0.7154, 0.0721);
		baseColor = vec3(dot(baseColor, fLuminance));
	}

	if(wantsNight){
		vec3 fcol1 = vec3(0.66, 0.18, 0.22);
		vec3 fcol2 = vec3(-0.02, 0.05, 0.04);
		vec2 uv1 = vec2(gl_FragCoord.xy) / vec2(textureSize(nightNoisePulseTexture, 0).xy);

		vec3 nightColor = vec3(dot(baseColor, fcol1));
		vec3 noisePulse = textureLod(sampler2DArray(nightNoisePulseTexture, sRepeatLinear), vec3(uv1, (engine.frameIndex/4) % 3u), 0.0).rgb;

		baseColor = 4.0 * (nightColor + fcol2);
		baseColor *= noisePulse;
	}


	if(wantsGrain){
		vec2 uv1 = vec2(gl_FragCoord.xy) / vec2(textureSize(grainNoiseTexture, 0).xy);
		uv1 += vec2(engine.randomX, engine.randomY);
		vec3 noise = textureLod(sampler2D(grainNoiseTexture, sRepeatLinear), uv1, 0.0).rgb - 0.5;
		baseColor += noise * noise * noise;
	}

	fragColor.rgb = baseColor;
	fragColor.a = 1.0f;
}
