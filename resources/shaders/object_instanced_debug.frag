#extension GL_EXT_nonuniform_qualifier : enable

#include "samplers.glsl"
#include "engine.glsl"

layout(location = 0) out vec4 fragColor; ///< Color.

/** Texture each face. */
void main(){
	fragColor = vec4(engine.color.rgb, 1.0);
}
