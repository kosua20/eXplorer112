
#include "../engine/engine.glsl"

// Attributes
layout(location = 0) in vec3 v;///< Position.
layout(location = 1) in vec3 materialAndType;///< Material and billboard type.
layout(location = 2) in vec2 uv;///< UV.
layout(location = 3) in vec3 centerPos;///< Center world pos.
layout(location = 4) in vec3 cosSinAngle;///< Center world pos.
layout(location = 5) in vec3 color; ///< .

layout(location = 0) out INTERFACE {
	vec3 color;
	vec2 uv;
	flat uint material;
} Out;

/** Apply the MVP transformation to the input vertex. */
void main(){

	vec4 pos = vec4(v, 1.0);
	uint billboardType = uint(materialAndType.y);
	if(billboardType != BILLBOARD_WORLD){
		mat4 view = engine.v;
		vec3 right = mix( vec3(view[0][0], view[1][0], view[2][0]), vec3(1.0,0.0, 0.0), bvec3(billboardType == BILLBOARD_AROUND_X));
		vec3 up = mix( vec3(view[0][1], view[1][1], view[2][1]), vec3(0.0, 1.0, 0.0), bvec3(billboardType == BILLBOARD_AROUND_Y));
		vec2 localPos = vec2(cosSinAngle.x * pos.x + cosSinAngle.y * pos.y, -cosSinAngle.y * pos.x + cosSinAngle.x * pos.y);
		pos.xyz = centerPos.xyz + localPos.x * right + localPos.y * up;
	}

	gl_Position = engine.vp * pos;
	Out.uv.xy = uv;
	Out.color = color;
	Out.material = uint(materialAndType.x);
}
