$input a_position, a_texcoord0, a_texcoord1
$output v_texcoord0, v_texcoord1, v_fog

#include <bgfx_shader.sh>

uniform vec4 u_fogRange;  // Phase 5o

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_texcoord0 = a_texcoord0;
	v_texcoord1 = a_texcoord1;

	vec4  vp    = mul(u_modelView, vec4(a_position, 1.0));
	float d     = abs(vp.z);
	float range = u_fogRange.y - u_fogRange.x;
	float raw   = (u_fogRange.y - d) / max(range, 0.001);
	v_fog       = mix(1.0, clamp(raw, 0.0, 1.0), u_fogRange.z);
}
