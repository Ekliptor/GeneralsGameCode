$input v_fog

#include <bgfx_shader.sh>

uniform vec4 u_solidColor;
uniform vec4 u_fogColor;  // Phase 5o — rgb = fog color; alpha unused

void main()
{
	vec4 c = u_solidColor;
	c.rgb = mix(u_fogColor.rgb, c.rgb, v_fog);
	gl_FragColor = c;
}
