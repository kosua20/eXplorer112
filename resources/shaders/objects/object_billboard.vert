
#include "../engine/engine.glsl"

// Attributes
layout(location = 0) in vec3 v;///< Position.
layout(location = 1) in vec3 n;///< Normal. material and 
layout(location = 2) in vec2 uv;///< UV.
layout(location = 5) in vec3 color; ///< Bitangent.

layout(location = 0) out INTERFACE {
	vec3 color;
	vec2 uv;
	flat uint material;
} Out;

/** Apply the MVP transformation to the input vertex. */
void main(){

	gl_Position = engine.vp * vec4(v, 1.0);
	Out.uv.xy = uv;
	Out.color = color;
	Out.material = uint(n.x);
}
