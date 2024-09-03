#extension GL_EXT_nonuniform_qualifier : enable

#include "../engine/samplers.glsl"
#include "../engine/engine.glsl"

layout(location = 0) in INTERFACE {
	vec2 uv; ///< UV coordinates.
} In ;

layout(set = 3, binding = 0) uniform texture2DArray textures[]; ///< Color textures.

layout(location = 0) out vec4 fragColor; ///< Color.

void main(){
	
	fragColor = textureLod(sampler2DArray(textures[engine.selectedTextureArray], sClampLinear), vec3(In.uv, engine.selectedTextureLayer), 0.0);
}
