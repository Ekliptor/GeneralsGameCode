$input v_color0, v_texcoord0, v_fog

#include <bgfx_shader.sh>

SAMPLER2D(s_texture, 0);

// Phase 5n — cutout threshold in component 0; zero disables. See fs_triangle.sc
// for the naming rationale (avoids bgfx's predefined u_alphaRef4).
uniform vec4 u_cutoutRef;

uniform vec4 u_fogColor;  // Phase 5o

void main()
{
	vec4 t = texture2D(s_texture, v_texcoord0);
	if (t.a < u_cutoutRef.x) discard;
	vec4 c = t * v_color0;
	c.rgb = mix(u_fogColor.rgb, c.rgb, v_fog);
	gl_FragColor = c;
}
