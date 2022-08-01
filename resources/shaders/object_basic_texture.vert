#include "engine.glsl"

// Attributes
layout(location = 0) in vec3 v;///< Position.
layout(location = 1) in vec3 n;///< Normal.
layout(location = 2) in vec2 uv;///< UV.

layout(location = 0) out INTERFACE {
	vec3 n; ///< Normal
	vec2 uv; ///< Texture coordinates.
} Out;

/** Apply the MVP transformation to the input vertex. */
void main(){
	// We multiply the coordinates by the MVP matrix, and ouput the result.
	gl_Position = engine.vp * vec4(v, 1.0);
	Out.uv = uv;
	Out.n = n; // For now no transformation
}
