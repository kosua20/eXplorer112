layout(location = 0) in INTERFACE {
	vec3 color; ///< Color
} In ; 


layout(location = 0) out vec4 fragColor; ///< Color.

/** Texture each face. */
void main(){
	fragColor = vec4(In.color, 1.0);
}
