$input v_color0, v_texcoord0, v_texcoord1, v_fog, v_cloudUV, v_lightmapUV

#include <bgfx_shader.sh>

SAMPLER2D(s_texture,  0);  // stage 0 — terrain diffuse atlas (UV0)
SAMPLER2D(s_texture1, 1);  // stage 1 — same atlas, UV1 selects alpha-edge region
SAMPLER2D(s_texture2, 2);  // stage 2 — cloud overlay (world-space UV, animated)
SAMPLER2D(s_texture3, 3);  // stage 3 — lightmap / noise (world-space UV, static)

uniform vec4 u_cutoutRef;  // .x = alpha-test threshold (0 = disabled)
uniform vec4 u_fogColor;

void main()
{
	vec4 base = texture2D(s_texture,  v_texcoord0);
	vec4 edge = texture2D(s_texture1, v_texcoord1);

	// DX8 multipass passes 0+1 folded:
	//   pass 0: framebuffer = base.rgb * vColor.rgb  (no blend)
	//   pass 1: framebuffer = mix(prev, edge.rgb * vColor.rgb, edge.a * vColor.a)
	// Critical: vColor.a is the per-vertex alpha-edge mask packed by
	// BaseHeightMapRenderObjClass::doTheLight (see memory:
	// terrain_vertex_alpha_is_edge_mask). Tiles with no edge have
	// vColor.a == 0 → mix factor 0 → base shows through.
	float blend = edge.a * v_color0.a;
	vec3  composite = mix(base.rgb, edge.rgb, blend);
	composite *= v_color0.rgb;

	// DX8 pass 2 fold: framebuffer *= cloud * lightmap.
	//   D3DRS_SRCBLEND = D3DBLEND_DESTCOLOR, D3DRS_DESTBLEND = D3DBLEND_ZERO
	//   stage 0: COLOROP = SELECTARG1 (cloud sample)
	//   stage 1: COLOROP = MODULATE with stage 0 (cloud × lightmap)
	// White-pixel placeholder is bound for absent variants
	// (ST_TERRAIN_BASE / NOISE1 / NOISE2) → identity multiply, no-op.
	vec3 cloud    = texture2D(s_texture2, v_cloudUV).rgb;
	vec3 lightmap = texture2D(s_texture3, v_lightmapUV).rgb;
	composite *= cloud * lightmap;

	vec4 c = vec4(composite, 1.0);
	if (c.a < u_cutoutRef.x) discard;
	c.rgb = mix(u_fogColor.rgb, c.rgb, v_fog);
	gl_FragColor = c;
}
