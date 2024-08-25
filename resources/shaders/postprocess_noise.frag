#include "samplers.glsl"

layout(location = 0) in INTERFACE {
	vec2 uv; ///< UV coordinates.
} In ;

layout(set = 2, binding = 0) uniform texture2D screenTexture; ///< Image to output.
layout(set = 2, binding = 1) uniform texture2D bloomTexture; ///< Image to output.
layout(set = 2, binding = 2) uniform texture2D noiseTexture; ///< Image to output.

layout(location = 0) out vec4 fragColor; ///< Color.

/** Just pass the input image as-is, potentially performing up/down scaling. */
void main(){
	vec3 baseColor = textureLod(sampler2D(screenTexture, sClampLinear), In.uv, 0.0).rgb;
	vec3 bloomColor = textureLod(sampler2D(bloomTexture, sClampLinear), In.uv, 0.0).rgb;
	vec3 noiseColor = textureLod(sampler2D(noiseTexture, sRepeatLinear), In.uv * 10.0, 0.0).rgb;

	fragColor.rgb = noiseColor * (baseColor + 0.5 * bloomColor);
	fragColor.a = 1.0f;
}
