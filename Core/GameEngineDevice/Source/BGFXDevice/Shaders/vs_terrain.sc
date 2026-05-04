$input  a_position, a_color0, a_texcoord0, a_texcoord1
$output v_color0, v_texcoord0, v_texcoord1, v_fog, v_cloudUV, v_lightmapUV

#include <bgfx_shader.sh>

// Phase D5/D6 — single-pass terrain shader.
//
// The DX8 multi-pass terrain renderer (TerrainShader2Stage in
// W3DShaderManager.cpp) walks the same vertex buffer 2-3 times:
//   pass 0: base diffuse atlas, UV0, modulate with vertex diffuse
//   pass 1: alpha-edge atlas (same TerrainTexture in the heightmap path),
//           UV1, alpha-blended over pass 0 using SRC_ALPHA / INV_SRC_ALPHA
//   pass 2: cloud × lightmap multiplied over the framebuffer (D6 — folded
//           into the fragment as a final multiply)
//
// bgfx submits state-per-view, not state-per-submit (see memory
// `bgfx_setviewtransform_per_view`), so per-pass framebuffer-blend changes
// would need extra view IDs. Folding all passes into one shader keeps every
// terrain submit on kView3D as an opaque draw and matches modern GPU best
// practice. Output alpha is forced to 1.0 — terrain is opaque.
//
// Cloud/lightmap UVs (D6): The DX8 path uses
// D3DTSS_TCI_CAMERASPACEPOSITION with a texture matrix of
// view_inverse * scale * translate. That collapses algebraically to
// world_pos.xy * STRETCH_FACTOR + (xOff, yOff): the camera-space-position
// then view-inverse trick just recovers the world-space XY position.
// Cloud animates via xOff/yOff; lightmap is static-world.
//
// Vertex inputs:
//   a_position  : world-space terrain vertex (pre-multiplied by Transform).
//   a_color0    : prelit diffuse + dynamic light contribution (PRELIT_DIFFUSE
//                 vertex material puts lighting in the diffuse channel).
//                 Alpha channel = per-vertex alpha-edge blend mask
//                 (see memory: terrain_vertex_alpha_is_edge_mask).
//   a_texcoord0 : UV into the base diffuse region of the atlas.
//   a_texcoord1 : UV into the alpha-edge region of the same atlas.

uniform vec4 u_fogRange;
uniform vec4 u_cloudParams;   // .xy = (xOff, yOff), .z = STRETCH_FACTOR

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_color0    = a_color0;
	v_texcoord0 = a_texcoord0;
	v_texcoord1 = a_texcoord1;

	// World-space XY → cloud / lightmap UV.
	vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
	v_cloudUV    = worldPos.xy * u_cloudParams.z + u_cloudParams.xy;
	v_lightmapUV = worldPos.xy * u_cloudParams.z;

	vec4  vp    = mul(u_modelView, vec4(a_position, 1.0));
	float d     = abs(vp.z);
	float range = u_fogRange.y - u_fogRange.x;
	float raw   = (u_fogRange.y - d) / max(range, 0.001);
	v_fog       = mix(1.0, clamp(raw, 0.0, 1.0), u_fogRange.z);
}
