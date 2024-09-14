#include "engine/samplers.glsl"

layout(location = 0) in INTERFACE {
	vec2 uv; ///< UV coordinates.
} In ;


layout(set = 2, binding = 0, rgba16f) uniform readonly image2D screenTexture;

layout(location = 0) out vec4 fragColor; ///< Color.

/** Just pass the input image as-is, potentially performing up/down scaling. */
void main(){
	vec2 texSize = imageSize(screenTexture).xy;
	ivec2 coords = ivec2(floor(In.uv * texSize));
	fragColor = imageLoad(screenTexture, coords);
}
