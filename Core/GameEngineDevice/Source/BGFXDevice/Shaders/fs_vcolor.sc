$input v_color0, v_fog

#include <bgfx_shader.sh>

uniform vec4 u_fogColor;  // Phase 5o

void main()
{
	vec4 c = v_color0;
	c.rgb = mix(u_fogColor.rgb, c.rgb, v_fog);
	gl_FragColor = c;
}
