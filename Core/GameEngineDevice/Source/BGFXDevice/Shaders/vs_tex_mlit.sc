$input  a_position, a_normal, a_color0, a_texcoord0
$output v_color0, v_texcoord0, v_fog

#include <bgfx_shader.sh>

// Phase 5l — N-slot directional lighting (N = 4, fixed).
//   u_lightDirArr[i].xyz = light direction (world space, pointing FROM light TO scene)
//   u_lightDirArr[0].w   = global ambient term (slots 1..3 .w are unused)
//   u_lightColorArr[i].rgb = color * intensity  (0,0,0 means slot is disabled)
//   u_lightColorArr[i].w   = unused
//
// Disabled slots must be uploaded as zero-color so the accumulation degenerates
// to a no-op (n·L multiplied by zero). The backend handles that zero-fill in
// Draw_Indexed / Draw; the shader doesn't branch on an enabled flag.
uniform vec4 u_lightDirArr[4];
uniform vec4 u_lightColorArr[4];
uniform vec4 u_fogRange;  // Phase 5o

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));

	vec3 worldNormal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);

	vec3 lighting = vec3_splat(u_lightDirArr[0].w);
	for (int i = 0; i < 4; ++i)
	{
		float ndotl = max(dot(worldNormal, -normalize(u_lightDirArr[i].xyz)), 0.0);
		lighting += u_lightColorArr[i].rgb * ndotl;
	}

	v_color0    = vec4(a_color0.rgb * lighting, a_color0.a);
	v_texcoord0 = a_texcoord0;

	vec4  vp    = mul(u_modelView, vec4(a_position, 1.0));
	float d     = abs(vp.z);
	float range = u_fogRange.y - u_fogRange.x;
	float raw   = (u_fogRange.y - d) / max(range, 0.001);
	v_fog       = mix(1.0, clamp(raw, 0.0, 1.0), u_fogRange.z);
}
