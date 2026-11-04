/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/nodes/shader/nodes/node_shader_remix_material.c
 *  \ingroup shdnodes
 *
 * BGE-Remix P9.2: the RTX Remix Material node. One input per field of an
 * RTX Remix material; wired to the Material Output (Blender Internal's
 * Output node's place: the node is the material's output, the Output
 * node keeps the albedo for stock 2.79), it is the material. The game draws
 * it with the physical shading of P8's Physical surface
 * (GPU_shade_remix_node), F12 with the same maths (RE_shade_physical, the
 * exec below), and the translator for Remix reads its inputs
 * (source/blender/remix/intern/remix_material.cc). Its unlinked values are
 * the material's physical values (BKE_material_physical_get).
 */

#include "node_shader_util.h"

#include "BKE_global.h"
#include "BKE_material_physical.h"

#include "GPU_material.h"

static bNodeSocketTemplate sh_node_remix_material_in[] = {
	{	SOCK_RGBA, 1, N_("Albedo"),               0.8f, 0.8f, 0.8f, 1.0f},
	{	SOCK_FLOAT, 1, N_("Opacity"),             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_FACTOR},
	{	SOCK_FLOAT, 1, N_("Roughness"),           0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_FACTOR},
	{	SOCK_FLOAT, 1, N_("Metallic"),            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_FACTOR},
	{	SOCK_RGBA, 1, N_("Normal"),               0.5f, 0.5f, 1.0f, 1.0f},
	{	SOCK_RGBA, 1, N_("Emissive Color"),       1.0f, 1.0f, 1.0f, 1.0f},
	{	SOCK_FLOAT, 1, N_("Emissive Intensity"),  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1000.0f, PROP_NONE},
	{	SOCK_FLOAT, 1, N_("Height"),              0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_FACTOR},
	{	SOCK_FLOAT, 1, N_("Displace In"),         0.05f, 0.0f, 0.0f, 0.0f, 0.0f, 10.0f, PROP_DISTANCE},
	{	SOCK_FLOAT, 1, N_("Displace Out"),        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 10.0f, PROP_DISTANCE},
	{	SOCK_FLOAT, 1, N_("Anisotropy"),          0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, PROP_FACTOR},
	{	SOCK_FLOAT, 1, N_("Thin Film"),           0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1500.0f, PROP_NONE},
	{	SOCK_FLOAT, 1, N_("IOR"),                 1.5f, 0.0f, 0.0f, 0.0f, 1.0f, 3.0f, PROP_NONE},
	{	SOCK_RGBA, 1, N_("Transmittance"),        1.0f, 1.0f, 1.0f, 1.0f},
	{	SOCK_FLOAT, 1, N_("Transmittance Distance"), 0.5f, 0.0f, 0.0f, 0.0f, 0.001f, 100.0f, PROP_DISTANCE},
	{	SOCK_FLOAT, 1, N_("Thin Wall Thickness"), 0.001f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_DISTANCE},
	{	SOCK_FLOAT, 1, N_("Subsurface"),          0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_FACTOR},
	{	SOCK_RGBA, 1, N_("Subsurface Color"),     0.8f, 0.8f, 0.8f, 1.0f},
	{	SOCK_VECTOR, 1, N_("Subsurface Radius"),  1.0f, 0.2f, 0.1f, 0.0f, 0.0f, 100.0f, PROP_NONE},
	{	-1, 0, ""	}
};

static bNodeSocketTemplate sh_node_remix_material_out[] = {
	{	SOCK_RGBA, 0, N_("Color")},
	{	SOCK_FLOAT, 0, N_("Alpha")},
	{	-1, 0, ""	}
};

static void node_shader_init_remix_material(bNodeTree *UNUSED(ntree), bNode *node)
{
	node->custom1 = 0;
	node->custom3 = 0.0f;  /* Bake Size: 1024 */
	node->custom4 = 0.5f;  /* the alpha test's cutoff */
}

/* the material's base colour and alpha kept at the node's (stock 2.79 and
 * the Classic views show them) */
static void node_shader_update_remix_material(bNodeTree *ntree, bNode *UNUSED(node))
{
	BKE_material_remix_node_sync(G.main, ntree);
}

/* ---------------------------------------------------------------------- */
/* F12 (Blender Internal): the node shaded as the game shades it
 * (RE_shade_physical: each lamp's GGX and Lambert, the world, the sky in
 * the RTX Remix pipeline), its emission added; glass keeps Blender
 * Internal's raytraced transparency (BKE_material_physical_render_begin) */

static float remix_value(bNodeStack **in, int i)
{
	float v;
	nodestack_get_vec(&v, SOCK_FLOAT, in[i]);
	return v;
}

static void node_shader_exec_remix_material(void *data, int UNUSED(thread), bNode *node, bNodeExecData *UNUSED(execdata),
                                            bNodeStack **in, bNodeStack **out)
{
	ShaderCallData *shcd = data;
	ShadeInput *shi;
	Material *ma;
	float col[4], base[3], emission[3], diff[3], spec[3], alpha;
	int i;

	if (!shcd || !shcd->shi || !shcd->shi->mat) {
		return;
	}
	shi = shcd->shi;
	ma = shi->mat;

	nodestack_get_vec(base, SOCK_VECTOR, in[SH_REMIX_IN_ALBEDO]);
	const float opacity = remix_value(in, SH_REMIX_IN_OPACITY);
	const float roughness = remix_value(in, SH_REMIX_IN_ROUGHNESS);
	const float metallic = remix_value(in, SH_REMIX_IN_METALLIC);
	const float intensity = remix_value(in, SH_REMIX_IN_EMISSIVE_INTENSITY);
	const float subsurface = remix_value(in, SH_REMIX_IN_SUBSURFACE);
	const bool glass = (node->custom1 & SH_NODE_REMIX_TRANSLUCENT) != 0;
	nodestack_get_vec(emission, SOCK_VECTOR, in[SH_REMIX_IN_EMISSIVE]);

	/* the normal map, in tangent space as a texture slot's */
	bNodeSocket *nor = BLI_findlink(&node->inputs, SH_REMIX_IN_NORMAL);
	if (nor && nor->link && (ma->mode_l & MA_NORMAP_TANG)) {
		float c[4], t[3], B[3], tv[3];
		const float *no = shi->nmapnorm;
		nodestack_get_vec(c, SOCK_RGBA, in[SH_REMIX_IN_NORMAL]);
		t[0] = -(2.0f * c[0] - 1.0f);
		t[1] = (node->custom1 & SH_NODE_REMIX_NORMAL_DIRECTX) ? -(2.0f * c[1] - 1.0f) : 2.0f * c[1] - 1.0f;
		t[2] = 2.0f * c[2] - 1.0f;
		cross_v3_v3v3(B, no, shi->nmaptang);
		mul_v3_fl(B, shi->nmaptang[3]);
		for (i = 0; i < 3; i++) {
			tv[i] = t[0] * shi->nmaptang[i] + t[1] * B[i] + t[2] * no[i];
		}
		normalize_v3_v3(shi->vn, tv);
	}
	else {
		copy_v3_v3(shi->vn, shi->vno);
	}

	if (ma->mode & MA_SHLESS) {
		copy_v3_v3(diff, base);
		zero_v3(spec);
	}
	else {
		RE_shade_physical(shi, base, metallic, roughness, 0.5f, glass ? 1.0f : 0.0f, subsurface, diff, spec);
		if (intensity > 0.0f) {
			madd_v3_v3fl(diff, emission, intensity);
		}
	}
	add_v3_v3v3(col, diff, spec);
	/* glass: Blender Internal's raytraced transparency sees through it */
	alpha = glass ? ma->alpha : opacity;
	col[3] = alpha;
	copy_v4_v4(out[0]->vec, col);
	out[1]->vec[0] = alpha;

	/* the material's own RTX Remix node is its output (Blender's Output node
	 * keeps the albedo for stock 2.79) and its passes */
	if (shcd->shr && BKE_material_remix_node_is_output(ma, node)) {
		ShadeResult *shr = shcd->shr;
		copy_v3_v3(shr->diff, diff);
		copy_v3_v3(shr->diffshad, diff);
		copy_v3_v3(shr->spec, spec);
		copy_v3_v3(shr->combined, col);
		shr->combined[3] = alpha;
		shr->alpha = alpha;
	}
}

/* ---------------------------------------------------------------------- */
/* the game (GLSL) */

static GPUNodeLink *remix_input(GPUMaterial *mat, GPUNodeStack *in)
{
	GPUNodeLink *result = NULL;
	if (in->link) {
		return in->link;
	}
	/* (GPU_uniform only as a parameter of GPU_link, as the Material node) */
	if (in->type == GPU_FLOAT) {
		GPU_link(mat, "set_value", GPU_uniform(in->vec), &result);
	}
	else if (in->type == GPU_VEC3) {
		GPU_link(mat, "set_rgb", GPU_uniform(in->vec), &result);
	}
	else {
		GPU_link(mat, "set_rgba", GPU_uniform(in->vec), &result);
	}
	return result;
}

static int gpu_shader_remix_material(GPUMaterial *mat, bNode *node, bNodeExecData *UNUSED(execdata), GPUNodeStack *in,
                                     GPUNodeStack *out)
{
	GPURemixLinks links;
	bNodeSocket *nor = BLI_findlink(&node->inputs, SH_REMIX_IN_NORMAL);

	links.albedo = remix_input(mat, &in[SH_REMIX_IN_ALBEDO]);
	links.opacity = remix_input(mat, &in[SH_REMIX_IN_OPACITY]);
	links.roughness = remix_input(mat, &in[SH_REMIX_IN_ROUGHNESS]);
	links.metallic = remix_input(mat, &in[SH_REMIX_IN_METALLIC]);
	links.emission = remix_input(mat, &in[SH_REMIX_IN_EMISSIVE]);
	links.emission_intensity = remix_input(mat, &in[SH_REMIX_IN_EMISSIVE_INTENSITY]);
	/* the normal only when something is wired in (else the surface's) */
	links.normal = (nor && nor->link && in[SH_REMIX_IN_NORMAL].link) ? in[SH_REMIX_IN_NORMAL].link : NULL;
	links.normal_directx = (node->custom1 & SH_NODE_REMIX_NORMAL_DIRECTX) != 0;

	GPU_shade_remix_node(mat, &links, node, &out[0].link, &out[1].link);
	return true;
}

void register_node_type_sh_remix_material(void)
{
	static bNodeType ntype;

	sh_node_type_base(&ntype, SH_NODE_REMIX_MATERIAL, "RTX Remix Material", NODE_CLASS_SHADER, 0);
	node_type_compatibility(&ntype, NODE_OLD_SHADING);
	node_type_socket_templates(&ntype, sh_node_remix_material_in, sh_node_remix_material_out);
	node_type_size_preset(&ntype, NODE_SIZE_LARGE);
	node_type_init(&ntype, node_shader_init_remix_material);
	node_type_update(&ntype, node_shader_update_remix_material, NULL);
	node_type_exec(&ntype, NULL, NULL, node_shader_exec_remix_material);
	node_type_gpu(&ntype, gpu_shader_remix_material);

	nodeRegisterType(&ntype);
}
