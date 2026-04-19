$input  a_position, a_normal, a_color0, a_texcoord0
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>

// xyz = light direction (world space, pointing FROM light TO scene)
// w   = ambient term
uniform vec4 u_lightDir;
// rgb = light color, a unused
uniform vec4 u_lightColor;

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));

	vec3 worldNormal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
	float ndotl      = max(dot(worldNormal, -normalize(u_lightDir.xyz)), 0.0);
	vec3  lighting   = u_lightColor.rgb * ndotl + vec3_splat(u_lightDir.w);

	v_color0    = vec4(a_color0.rgb * lighting, a_color0.a);
	v_texcoord0 = a_texcoord0;
}
