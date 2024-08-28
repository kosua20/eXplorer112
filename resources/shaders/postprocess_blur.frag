#include "samplers.glsl"

layout(location = 0) in INTERFACE {
	vec2 uv; ///< UV coordinates.
} In ;

layout(set = 0, binding = 0) uniform ShaderData {
	vec2 delta;
} data;

layout(set = 2, binding = 0) uniform texture2D screenTexture; ///< Image to blur

layout(location = 0) out vec4 fragColor; ///< Color.

/** Just pass the input image as-is, potentially performing up/down scaling. */
void main(){

	vec3 c0 = textureLod(sampler2D(screenTexture, sClampLinear), In.uv, 0.0).rgb;
	vec3 cm = textureLod(sampler2D(screenTexture, sClampLinear), In.uv - 1.5f * data.delta, 0.0).rgb;
	vec3 cp = textureLod(sampler2D(screenTexture, sClampLinear), In.uv + 1.5f * data.delta, 0.0).rgb;

	vec3 color = 0.5 * c0 + 0.25 * cm + 0.25 * cp;
	fragColor.rgb = color;
	fragColor.a = 1.0;
}
