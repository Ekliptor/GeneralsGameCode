$input v_texcoord0, v_texcoord1, v_fog

#include <bgfx_shader.sh>

SAMPLER2D(s_texture,  0);
SAMPLER2D(s_texture1, 1);

// Phase 5n — cutout threshold in component 0; zero disables. See fs_triangle.sc
// for the naming rationale (avoids bgfx's predefined u_alphaRef4).
uniform vec4 u_cutoutRef;

uniform vec4 u_fogColor;  // Phase 5o

void main()
{
	vec4 base    = texture2D(s_texture,  v_texcoord0);
	vec4 overlay = texture2D(s_texture1, v_texcoord1);
	vec4 c = base * overlay;
	if (c.a < u_cutoutRef.x) discard;
	c.rgb = mix(u_fogColor.rgb, c.rgb, v_fog);
	gl_FragColor = c;
}
