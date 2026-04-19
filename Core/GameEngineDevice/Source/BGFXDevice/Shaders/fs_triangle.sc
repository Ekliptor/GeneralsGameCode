$input v_texcoord0, v_fog

#include <bgfx_shader.sh>

SAMPLER2D(s_texture, 0);

// Phase 5n — cutout alpha-test threshold lives in component 0 of u_cutoutRef;
// zero means disabled (alpha is always non-negative so the discard never fires).
uniform vec4 u_cutoutRef;

// Phase 5o — linear vertex fog. rgb is the fog color; alpha unused.
uniform vec4 u_fogColor;

void main()
{
	vec4 c = texture2D(s_texture, v_texcoord0);
	if (c.a < u_cutoutRef.x) discard;
	c.rgb = mix(u_fogColor.rgb, c.rgb, v_fog);
	gl_FragColor = c;
}
