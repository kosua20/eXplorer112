#include "engine.glsl"

// Attributes
layout(location = 0) in vec3 v;///< Position.
layout(location = 5) in vec3 color;///< Color.

layout(location = 0) out INTERFACE {
	vec3 color; ///< Color
} Out;

/** Apply the MVP transformation to the input vertex. */
void main(){
	// We multiply the coordinates by the MVP matrix, and ouput the result.
	gl_Position = engine.vp * vec4(v, 1.0);
	Out.color = color;
}
