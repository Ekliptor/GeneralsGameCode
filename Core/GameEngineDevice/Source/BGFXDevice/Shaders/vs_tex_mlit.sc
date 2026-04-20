$input  a_position, a_normal, a_color0, a_texcoord0
$output v_color0, v_texcoord0, v_fog, v_specular

#include <bgfx_shader.sh>

// Phase 5l    — N-slot directional lighting (N = 4).
// Phase 5h.9  — point-light linear attenuation.
// Phase 5h.10 — spot-light cone mask.
// Phase 5h.11 — Blinn-Phong per-vertex specular highlights.
//
// Uniforms:
//   u_lightDirArr[i].xyz   = light direction (world, FROM light TO scene).
//   u_lightDirArr[0].w     = global ambient term (slots 1..3 .w unused).
//   u_lightColorArr[i].rgb = diffuse color * intensity (zero disables slot).
//   u_lightColorArr[i].w   = spot inner cos (only read when slot is a spot).
//   u_lightPosArr[i].xyz   = world-space position (point/spot lights).
//   u_lightPosArr[i].w     = attenuation range; 0 = directional, >0 = point/spot.
//   u_lightSpotArr[i].xyz  = spot direction (world, FROM light along cone).
//   u_lightSpotArr[i].w    = spot outer cos; <0 = not a spot.
//   u_lightSpecArr[i].rgb  = per-light specular color; zero disables specular.
//   u_materialSpec.rgb     = material specular reflectance; zero disables.
//   u_materialSpec.w       = Blinn-Phong shininess exponent (power).
//
// Camera position is read from bgfx's built-in `u_invView[3].xyz` (column
// 3 of the inverse view matrix is the view's world-space origin).
uniform vec4 u_lightDirArr[4];
uniform vec4 u_lightColorArr[4];
uniform vec4 u_lightPosArr[4];
uniform vec4 u_lightSpotArr[4];
uniform vec4 u_lightSpecArr[4];
uniform vec4 u_materialSpec;
uniform vec4 u_fogRange;

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));

	vec3 worldPos    = mul(u_model[0], vec4(a_position, 1.0)).xyz;
	vec3 worldNormal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
	vec3 viewDir     = normalize(u_invView[3].xyz - worldPos);

	vec3 lighting = vec3_splat(u_lightDirArr[0].w);
	vec3 specular = vec3_splat(0.0);
	for (int i = 0; i < 4; ++i)
	{
		vec3 dirLight = -normalize(u_lightDirArr[i].xyz);

		vec3  pointToLight = u_lightPosArr[i].xyz - worldPos;
		float pointDist    = length(pointToLight);
		vec3  pointDir     = pointToLight / max(pointDist, 0.001);

		float isPoint = step(0.0001, u_lightPosArr[i].w);
		vec3  L       = mix(dirLight, pointDir, isPoint);
		float pointAtten = clamp(1.0 - pointDist / max(u_lightPosArr[i].w, 0.001), 0.0, 1.0);
		float atten   = mix(1.0, pointAtten, isPoint);

		float isSpot    = step(0.0, u_lightSpotArr[i].w);
		vec3  spotDir   = normalize(u_lightSpotArr[i].xyz);
		float spotCos   = dot(spotDir, -L);
		float outerCos  = u_lightSpotArr[i].w;
		float innerCos  = u_lightColorArr[i].w;
		float spotMask  = smoothstep(outerCos, max(innerCos, outerCos + 0.001), spotCos);
		float coneMask  = mix(1.0, spotMask, isSpot);

		float ndotl = max(dot(worldNormal, L), 0.0);
		float common = atten * coneMask;
		lighting += u_lightColorArr[i].rgb * ndotl * common;

		// Phase 5h.11 — Blinn-Phong. Only compute when the slot contributes
		// any specular + diffuse (ndotl > 0 avoids the GPU doing pow() for
		// back-facing fragments whose highlight is behind the surface).
		vec3  H      = normalize(L + viewDir);
		float ndoth  = max(dot(worldNormal, H), 0.0);
		float specF  = pow(ndoth, max(u_materialSpec.w, 1.0)) * step(0.0001, ndotl);
		specular    += u_lightSpecArr[i].rgb * specF * common;
	}

	v_color0    = vec4(a_color0.rgb * lighting, a_color0.a);
	v_specular  = u_materialSpec.rgb * specular;
	v_texcoord0 = a_texcoord0;

	vec4  vp    = mul(u_modelView, vec4(a_position, 1.0));
	float d     = abs(vp.z);
	float range = u_fogRange.y - u_fogRange.x;
	float raw   = (u_fogRange.y - d) / max(range, 0.001);
	v_fog       = mix(1.0, clamp(raw, 0.0, 1.0), u_fogRange.z);
}
