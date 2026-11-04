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
 * Output node), it is the material. The game draws it with the physical
 * shading of P8's Physical surface (GPU_shade_remix_node), F12 through
 * P8's physical translation into Blender Internal's settings (the exec
 * below), and the translator for Remix reads its inputs
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
/* F12 (Blender Internal): the material's settings as P8's physical
 * translation made them (BKE_material_physical_render_begin), with what is
 * wired into the node over them */

static void node_shader_exec_remix_material(void *data, int UNUSED(thread), bNode *node, bNodeExecData *UNUSED(execdata),
                                            bNodeStack **in, bNodeStack **out)
{
	ShaderCallData *shcd = data;
	ShadeInput *shi;
	ShadeResult shrnode;
	Material *ma;
	char linked[SH_REMIX_IN_COUNT] = {0};
	float col[4], emission[3], intensity;
	bNodeSocket *sock;
	int i, mode;

	if (!shcd || !shcd->shi || !shcd->shi->mat) {
		return;
	}
	shi = shcd->shi;
	ma = shi->mat;
	for (sock = node->inputs.first, i = 0; sock && i < SH_REMIX_IN_COUNT; sock = sock->next, ++i) {
		linked[i] = (sock->link != NULL);
	}

	/* copy all relevant material vars, as the Material node does */
	memcpy(&shi->r, &ma->r, 23 * sizeof(float));
	shi->har = ma->har;

	if (linked[SH_REMIX_IN_ALBEDO]) {
		nodestack_get_vec(&shi->r, SOCK_VECTOR, in[SH_REMIX_IN_ALBEDO]);
	}
	if (linked[SH_REMIX_IN_OPACITY]) {
		nodestack_get_vec(&shi->alpha, SOCK_FLOAT, in[SH_REMIX_IN_OPACITY]);
	}
	if (linked[SH_REMIX_IN_ROUGHNESS]) {
		float rough;
		nodestack_get_vec(&rough, SOCK_FLOAT, in[SH_REMIX_IN_ROUGHNESS]);
		shi->har = (int)BKE_material_roughness_to_hardness(rough);
	}
	/* the normal map, in tangent space as a texture slot's */
	if (linked[SH_REMIX_IN_NORMAL] && (ma->mode_l & MA_NORMAP_TANG)) {
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

	/* emission: the node's colour (Blender Internal would emit the diffuse
	 * colour), added after the lamps */
	nodestack_get_vec(emission, SOCK_VECTOR, in[SH_REMIX_IN_EMISSIVE]);
	nodestack_get_vec(&intensity, SOCK_FLOAT, in[SH_REMIX_IN_EMISSIVE_INTENSITY]);
	shi->emit = 0.0f;

	mode = shi->mode;
	if (ma->mode & MA_TRANSP) {
		shi->mode |= MA_TRANSP;
	}
	shi->nodes = 1;
	node_shader_lamp_loop(shi, &shrnode);  /* clears shrnode */
	shi->nodes = 0;
	shi->mode = mode;

	copy_v3_v3(col, shrnode.combined);
	if (intensity > 0.0f && !(ma->mode & MA_SHLESS)) {
		madd_v3_v3fl(col, emission, intensity);
	}
	col[3] = shrnode.alpha;
	copy_v4_v4(out[0]->vec, col);
	out[1]->vec[0] = shrnode.alpha;

	/* the material's passes are this node's */
	{
		float combined[4], alpha;
		copy_v4_v4(combined, shcd->shr->combined);
		alpha = shcd->shr->alpha;
		*(shcd->shr) = shrnode;
		copy_v4_v4(shcd->shr->combined, combined);
		shcd->shr->alpha = alpha;
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

	GPU_shade_remix_node(mat, &links, &out[0].link, &out[1].link);
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
