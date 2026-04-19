$input  a_position
$output v_fog

#include <bgfx_shader.sh>

// Phase 5o — linear vertex fog. .x=start, .y=end, .z=enable (0 or 1).
uniform vec4 u_fogRange;

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));

	vec4  vp    = mul(u_modelView, vec4(a_position, 1.0));
	float d     = abs(vp.z);
	float range = u_fogRange.y - u_fogRange.x;
	float raw   = (u_fogRange.y - d) / max(range, 0.001);
	v_fog       = mix(1.0, clamp(raw, 0.0, 1.0), u_fogRange.z);
}
