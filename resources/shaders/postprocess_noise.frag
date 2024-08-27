#include "samplers.glsl"

#include "engine.glsl"


layout(location = 0) in INTERFACE {
	vec2 uv; ///< UV coordinates.
} In ;

layout(set = 2, binding = 0) uniform texture2D screenTexture; ///< Image to output.
layout(set = 2, binding = 1) uniform texture2D bloomTexture; ///< Image to output.
layout(set = 2, binding = 2) uniform texture2D noiseTexture; ///< Image to output.

layout(location = 0) out vec4 fragColor; ///< Color.

/** Just pass the input image as-is, potentially performing up/down scaling. */
void main(){
	bool wantsBloom = (engine.postprocessMode &  MODE_POSTPROCESS_BLOOM) != 0u;
	bool wantsGrain = (engine.postprocessMode &  MODE_POSTPROCESS_GRAIN) != 0u;

	vec3 baseColor = textureLod(sampler2D(screenTexture, sClampLinear), In.uv, 0.0).rgb;
	vec3 bloomColor = wantsBloom ? textureLod(sampler2D(bloomTexture, sClampLinear), In.uv, 0.0).rgb : vec3(0.0);

	vec2 noiseUV = vec2(gl_FragCoord.xy) / vec2(textureSize(noiseTexture, 0).xy);
	noiseUV += vec2(engine.randomX, engine.randomY);
	vec3 noise = wantsGrain ? textureLod(sampler2D(noiseTexture, sRepeatLinear), noiseUV, 0.0).rgb - 0.5 : vec3(0.0);

	vec3 litColor = baseColor + 0.5 * bloomColor;
	fragColor.rgb = litColor + noise * noise * noise;
	fragColor.a = 1.0f;
}
