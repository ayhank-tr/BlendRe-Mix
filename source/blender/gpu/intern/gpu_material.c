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
 * The Original Code is Copyright (C) 2006 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Brecht Van Lommel.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/gpu/intern/gpu_material.c
 *  \ingroup gpu
 *
 * Manages materials, lights and textures.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_group_types.h"
#include "DNA_lamp_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_world_types.h"
#include "DNA_texture_types.h"
#include "DNA_color_types.h"

#include "BLI_math.h"
#include "BLI_dynstr.h"
#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_noise.h"
#include "BLI_ghash.h"

#include "BKE_anim.h"
#include "BKE_colortools.h"
#include "BKE_global.h"
#include "BKE_image.h"
#include "BKE_main.h"
#include "BKE_node.h"
#include "BKE_scene.h"
#include "BKE_lamp_physical.h"
#include "BKE_material_physical.h"
#include "BKE_texture.h"
#include "BKE_group.h"

#include "IMB_imbuf_types.h"

#include "GPU_extensions.h"
#include "GPU_glew.h"
#include "GPU_framebuffer.h"
#include "GPU_material.h"
#include "GPU_shader.h"
#include "GPU_texture.h"

#include "gpu_codegen.h"

#ifdef WITH_OPENSUBDIV
#  include "BKE_DerivedMesh.h"
#endif

/* Structs */

typedef enum DynMatProperty {
	DYN_LAMP_CO = 1,
	DYN_LAMP_VEC = 2,
	DYN_LAMP_IMAT = 4,
	DYN_LAMP_PERSMAT = 8,
	DYN_LAMP_AREA = 16,  /* BGE-Remix P8.9 */
	DYN_LAMP_SHADOW = 32,  /* BGE-Remix P8.10 */
} DynMatProperty;

/* BGE-Remix P8.10: shadow buffers of every lamp type */
enum {
	GPU_LAMP_SHADOW_NONE = 0,
	GPU_LAMP_SHADOW_SPOT = 1,     /* one view */
	GPU_LAMP_SHADOW_CUBE = 2,     /* point and area lamps: a cube's six faces, three by two */
	GPU_LAMP_SHADOW_CASCADE = 3,  /* the sun: four cascades, two by two */
};

#define GPU_SHADOW_CASCADES 4
/* the tangent of half a cube face's view: a little over 1, so the faces
 * overlap and soft edges near a face's border find texels */
#define GPU_SHADOW_CUBE_TAN 1.1f
/* the largest shadow atlas side */
#define GPU_SHADOW_MAX_ATLAS 8192

static struct GPUWorld {
	float mistenabled;
	float mistype;
	float miststart;
	float mistdistance;
	float mistintensity;
	float mistcol[4];
	float horicol[3];
	float ambcol[4];
	float zencol[3];
} GPUWorld;

struct GPUMaterial {
	Scene *scene;
	Material *ma;

	/* material for mesh surface, worlds or something else.
	 * some code generation is done differently depending on the use case */
	int type;
	
	/* for creating the material */
	ListBase nodes;
	GPUNodeLink *outlink;

	/* for binding the material */
	GPUPass *pass;
	GPUVertexAttribs attribs;
	int builtins;
	int alpha, obcolalpha;
	int dynproperty;

	/* for passing uniforms */
	int viewmatloc, invviewmatloc;
	int obmatloc, invobmatloc;
	int localtoviewmatloc, invlocaltoviewmatloc;
	int obcolloc, obautobumpscaleloc;
	int cameratexcofacloc;

	int partscalarpropsloc;
	int partcoloc;
	int partvel;
	int partangvel;

	int objectinfoloc;
	int orcomatloc;            /* BGE-Remix P8.5 */
	float orcomat[4][4];
	bool orcomat_set;

	ListBase lamps;
	bool bound;

	bool is_opensubdiv;

	/* BGE-Remix P8: "Face Textures" (the face image, set per draw) */
	bool use_face_image;
	struct Image *face_image;
	float face_image_on;

	/* BGE-Remix P8.5: the texture slots that take Object coordinates from
	 * an object, and the objects' inverse matrices (refreshed on every
	 * bind; the slot is kept, not the object, which may be deleted) */
	int tot_texco_ob;
	MTex *texco_mtex[MAX_MTEX];
	float texco_obimat[MAX_MTEX][4][4];
};

struct GPULamp {
	Scene *scene;
	Object *ob;
	Object *par;
	Lamp *la;

	int type, mode, lay, hide;

	float dynenergy, dyncol[3];
	float energy, col[3];

	float co[3], vec[3];
	float dynco[3], dynvec[3];
	float obmat[4][4];
	float imat[4][4];
	float dynimat[4][4];

	float spotsi, spotbl, k;
	float spotvec[2];
	float dyndist, dynatt1, dynatt2;
	float dist, att1, att2;
	float coeff_const, coeff_lin, coeff_quad;
	float shadow_color[3];

	/* BGE-Remix P8.9: the light's size; area lamps: their corners (world and
	 * view space) and Blender Internal's size correction */
	float size_radius, area_x, area_y;
	float area_corner[4][3];
	float area_obmat[4][4];  /* the place with the object's scale (stretches the rectangle) */
	float dynarea[4][4];
	float areasize;
	float bleedbias;

	float bias, d, clipend;
	int size;

	/* BGE-Remix P8.10: shadows of every lamp type (GPU_LAMP_SHADOW_*): the
	 * views drawn into one atlas, the sun's cascades fitted to a camera, the
	 * values the shaders read (soft shadows from the light's size, slope
	 * bias, contact shadows) */
	int shadow_kind, tiles_x, tiles_y, nviews;
	float sun_angle;
	float view_viewmat[6][4][4], view_winmat[6][4][4];
	float cascade_persmat[GPU_SHADOW_CASCADES][4][4];  /* world -> cascade (uv, depth: 0..1) */
	float dyncascade[GPU_SHADOW_CASCADES][4][4];       /* camera view -> cascade */
	float cascade_scale[4];                            /* uv per world unit */
	float cascade_range[4];                            /* depth range, world units */
	float dyncube[4][4];                               /* camera view -> the lamp's axes */
	float shadow_params[4];                            /* light width (or spread), bias, 0, one texel */
	float shadow_clip[4];                              /* near, far, tangent of half the view, 0 */
	float contact[4];                                  /* distance, thickness, 0, bias */
	bool has_cam;
	float cam_viewinv[4][4], cam_winmat[4][4];
	GPUFrameBuffer *depthfb;                           /* Variance: the depth pass */
	bool dumped, bind_dumped;                          /* BGE_REMIX_SHADOW_DUMP */

	int falloff_type;
	struct CurveMapping *curfalloff;

	float winmat[4][4];
	float viewmat[4][4];
	float persmat[4][4];
	float dynpersmat[4][4];

	GPUFrameBuffer *fb;
	GPUFrameBuffer *blurfb;
	GPUTexture *tex;
	GPUTexture *depthtex;
	GPUTexture *blurtex;

	ListBase materials;

	/* BGE-Remix P8.3: the lamp's place in the shared lamp buffer */
	int slot;
};

/* Forward declaration so shade_light_textures() can use this, while still keeping the code somewhat organized */
static void texture_rgb_blend(
        GPUMaterial *mat, GPUNodeLink *tex, GPUNodeLink *out, GPUNodeLink *fact, GPUNodeLink *facg,
        int blendtype, GPUNodeLink **in);

/* BGE-Remix P8.3: the shared lamp and world buffers. Every lamp has a slot;
 * the lamp buffer holds each slot's GPULamp as words, so a material's lamp
 * values are read at their place in the struct (gpu_codegen.c writes the
 * reads, GPU_shared_uniform_expr). A material bind sends the slots of its
 * lamps that changed, and the world when it changed. */
#define GPU_SHARED_LAMP_BINDING 6
#define GPU_SHARED_WORLD_BINDING 7
#define GPU_LAMP_WORDS ((int)((sizeof(GPULamp) + 3) / 4))
#define GPU_WORLD_WORDS ((int)((sizeof(GPUWorld) + 3) / 4))

static struct {
	GPULamp **slots;           /* the lamp in each slot, or NULL */
	int len, cap;
	unsigned int lampbuf, worldbuf;
	int lampbuf_slots;         /* slots the lamp buffer holds */
	unsigned int *lamp_sent;   /* what the lamp buffer holds, per slot */
	unsigned char *lamp_valid; /* per slot: lamp_sent is right */
	unsigned int world_sent[GPU_WORLD_WORDS];
	bool world_valid;
} gpu_shared = {NULL};

static void gpu_shared_slot_add(GPULamp *lamp)
{
	int i;
	for (i = 0; i < gpu_shared.len; i++) {
		if (!gpu_shared.slots[i]) {
			break;
		}
	}
	if (i == gpu_shared.len) {
		if (gpu_shared.len == gpu_shared.cap) {
			gpu_shared.cap = max_ii(16, gpu_shared.cap * 2);
			gpu_shared.slots = MEM_recallocN(gpu_shared.slots, sizeof(GPULamp *) * gpu_shared.cap);
		}
		gpu_shared.len++;
	}
	gpu_shared.slots[i] = lamp;
	lamp->slot = i;
	if (gpu_shared.lamp_valid && i < gpu_shared.lampbuf_slots) {
		gpu_shared.lamp_valid[i] = 0;
	}
}

static void gpu_shared_slot_remove(GPULamp *lamp)
{
	if (lamp->slot >= 0 && lamp->slot < gpu_shared.len && gpu_shared.slots[lamp->slot] == lamp) {
		gpu_shared.slots[lamp->slot] = NULL;
		if (gpu_shared.lamp_valid && lamp->slot < gpu_shared.lampbuf_slots) {
			gpu_shared.lamp_valid[lamp->slot] = 0;
		}
	}
	lamp->slot = -1;
}

bool GPU_shared_uniform_expr(const float *ptr, int type, char *r_expr, size_t maxlen)
{
	const char *p = (const char *)ptr;
	const int count = type;  /* GPU_FLOAT 1, VEC2 2, VEC3 3, VEC4 4, MAT3 9, MAT4 16 */
	const char *array = NULL;
	int start = 0;

	if (!(count == 1 || count == 2 || count == 3 || count == 4 || count == 9 || count == 16) ||
	    !GLEW_VERSION_4_3)
	{
		return false;
	}
	for (int i = 0; i < gpu_shared.len && !array; i++) {
		const char *base = (const char *)gpu_shared.slots[i];
		if (base && p >= base && p + count * sizeof(float) <= base + sizeof(GPULamp)) {
			array = "bge_lw";
			start = i * GPU_LAMP_WORDS + (int)((p - base) / 4);
		}
	}
	if (!array) {
		const char *base = (const char *)&GPUWorld;
		if (p >= base && p + count * sizeof(float) <= base + sizeof(GPUWorld)) {
			array = "bge_ww";
			start = (int)((p - base) / 4);
		}
	}
	if (!array) {
		return false;
	}

	if (count == 1) {
		BLI_snprintf(r_expr, maxlen, "%s[%d]", array, start);
		return true;
	}
	DynStr *ds = BLI_dynstr_new();
	BLI_dynstr_appendf(ds, "%s(", (count == 9) ? "mat3" : (count == 16) ? "mat4" : (count == 2) ? "vec2" :
	                   (count == 3) ? "vec3" : "vec4");
	for (int k = 0; k < count; k++) {
		BLI_dynstr_appendf(ds, "%s%s[%d]", k ? ", " : "", array, start + k);
	}
	BLI_dynstr_append(ds, ")");
	char *str = BLI_dynstr_get_cstring(ds);
	BLI_dynstr_free(ds);
	const bool fits = strlen(str) < maxlen;
	if (fits) {
		BLI_strncpy(r_expr, str, maxlen);
	}
	MEM_freeN(str);
	return fits;
}

/* sends the lamps of this material that changed, and the world */
static void gpu_shared_buffers_bind(GPUMaterial *material)
{
	if (!GLEW_VERSION_4_3) {
		return;
	}
	const size_t lamp_bytes = sizeof(unsigned int) * GPU_LAMP_WORDS;

	if (!gpu_shared.worldbuf) {
		glGenBuffers(1, &gpu_shared.worldbuf);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpu_shared.worldbuf);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(gpu_shared.world_sent), NULL, GL_DYNAMIC_DRAW);
		gpu_shared.world_valid = false;
	}
	if (!gpu_shared.lampbuf || gpu_shared.lampbuf_slots < gpu_shared.len) {
		const int slots = max_ii(16, max_ii(gpu_shared.len, gpu_shared.lampbuf_slots * 2));
		if (!gpu_shared.lampbuf) {
			glGenBuffers(1, &gpu_shared.lampbuf);
		}
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpu_shared.lampbuf);
		glBufferData(GL_SHADER_STORAGE_BUFFER, lamp_bytes * slots, NULL, GL_DYNAMIC_DRAW);
		gpu_shared.lampbuf_slots = slots;
		gpu_shared.lamp_sent = MEM_reallocN_id(gpu_shared.lamp_sent, lamp_bytes * slots, "lamp words");
		if (gpu_shared.lamp_valid) {
			MEM_freeN(gpu_shared.lamp_valid);
		}
		gpu_shared.lamp_valid = MEM_callocN(slots, "lamp words valid");
	}

	for (LinkData *nlink = material->lamps.first; nlink; nlink = nlink->next) {
		GPULamp *lamp = nlink->data;
		const int slot = lamp->slot;
		if (slot < 0 || slot >= gpu_shared.lampbuf_slots) {
			continue;
		}
		unsigned int *sent = gpu_shared.lamp_sent + (size_t)slot * GPU_LAMP_WORDS;
		if (!gpu_shared.lamp_valid[slot] || memcmp(sent, lamp, sizeof(GPULamp)) != 0) {
			memcpy(sent, lamp, sizeof(GPULamp));
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpu_shared.lampbuf);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, (GLintptr)(lamp_bytes * slot), sizeof(GPULamp), lamp);
			gpu_shared.lamp_valid[slot] = 1;
		}
	}
	if (!gpu_shared.world_valid || memcmp(gpu_shared.world_sent, &GPUWorld, sizeof(GPUWorld)) != 0) {
		memcpy(gpu_shared.world_sent, &GPUWorld, sizeof(GPUWorld));
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpu_shared.worldbuf);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GPUWorld), &GPUWorld);
		gpu_shared.world_valid = true;
	}
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GPU_SHARED_LAMP_BINDING, gpu_shared.lampbuf);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GPU_SHARED_WORLD_BINDING, gpu_shared.worldbuf);
}

void GPU_shared_buffers_free(void)
{
	if (gpu_shared.lampbuf) {
		glDeleteBuffers(1, &gpu_shared.lampbuf);
	}
	if (gpu_shared.worldbuf) {
		glDeleteBuffers(1, &gpu_shared.worldbuf);
	}
	MEM_SAFE_FREE(gpu_shared.lamp_sent);
	MEM_SAFE_FREE(gpu_shared.lamp_valid);
	MEM_SAFE_FREE(gpu_shared.slots);
	memset(&gpu_shared, 0, sizeof(gpu_shared));
}

/* Functions */

static GPUMaterial *GPU_material_construct_begin(Material *ma)
{
	GPUMaterial *material = MEM_callocN(sizeof(GPUMaterial), "GPUMaterial");

	material->ma = ma;

	return material;
}

static void gpu_material_set_attrib_id(GPUMaterial *material)
{
	GPUVertexAttribs *attribs = &material->attribs;
	GPUPass *pass = material->pass;
	if (!pass) {
		attribs->totlayer = 0;
		return;
	}
	
	GPUShader *shader = GPU_pass_shader(pass);
	if (!shader) {
		attribs->totlayer = 0;
		return;
	}

	/* convert from attribute number to the actual id assigned by opengl,
	 * in case the attrib does not get a valid index back, it was probably
	 * removed by the glsl compiler by dead code elimination */

	int b = 0;
	for (int a = 0; a < attribs->totlayer; a++) {
		char name[32];
		BLI_snprintf(name, sizeof(name), "att%d", attribs->layer[a].attribid);
		attribs->layer[a].glindex = GPU_shader_get_attribute(shader, name);

		BLI_snprintf(name, sizeof(name), "att%d_info", attribs->layer[a].attribid);
		attribs->layer[a].glinfoindoex = GPU_shader_get_uniform(shader, name);

		if (attribs->layer[a].glindex >= 0) {
			attribs->layer[b] = attribs->layer[a];
			b++;
		}
	}

	attribs->totlayer = b;
}

static int gpu_material_construct_end(GPUMaterial *material, const char *passname)
{
	if (material->outlink) {
		GPUNodeLink *outlink = material->outlink;
		material->pass = GPU_generate_pass(&material->nodes, outlink,
			&material->attribs, &material->builtins, material->type,
			passname,
			material->is_opensubdiv,
			GPU_material_use_new_shading_nodes(material));

		if (!material->pass)
			return 0;

		gpu_material_set_attrib_id(material);
		
		GPUShader *shader = GPU_pass_shader(material->pass);

		if (material->builtins & GPU_VIEW_MATRIX)
			material->viewmatloc = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_VIEW_MATRIX));
		if (material->builtins & GPU_INVERSE_VIEW_MATRIX)
			material->invviewmatloc = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_INVERSE_VIEW_MATRIX));
		if (material->builtins & GPU_OBJECT_MATRIX)
			material->obmatloc = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_OBJECT_MATRIX));
		if (material->builtins & GPU_INVERSE_OBJECT_MATRIX)
			material->invobmatloc = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_INVERSE_OBJECT_MATRIX));
		if (material->builtins & GPU_LOC_TO_VIEW_MATRIX)
			material->localtoviewmatloc = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_LOC_TO_VIEW_MATRIX));
		if (material->builtins & GPU_INVERSE_LOC_TO_VIEW_MATRIX)
			material->invlocaltoviewmatloc = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_INVERSE_LOC_TO_VIEW_MATRIX));
		if (material->builtins & GPU_OBCOLOR)
			material->obcolloc = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_OBCOLOR));
		if (material->builtins & GPU_AUTO_BUMPSCALE)
			material->obautobumpscaleloc = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_AUTO_BUMPSCALE));
		if (material->builtins & GPU_CAMERA_TEXCO_FACTORS)
			material->cameratexcofacloc = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_CAMERA_TEXCO_FACTORS));
		if (material->builtins & GPU_PARTICLE_SCALAR_PROPS)
			material->partscalarpropsloc = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_PARTICLE_SCALAR_PROPS));
		if (material->builtins & GPU_PARTICLE_LOCATION)
			material->partcoloc = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_PARTICLE_LOCATION));
		if (material->builtins & GPU_PARTICLE_VELOCITY)
			material->partvel = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_PARTICLE_VELOCITY));
		if (material->builtins & GPU_PARTICLE_ANG_VELOCITY)
			material->partangvel = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_PARTICLE_ANG_VELOCITY));
		if (material->builtins & GPU_OBJECT_INFO)
			material->objectinfoloc = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_OBJECT_INFO));
		if (material->builtins & GPU_ORCO_MATRIX)
			material->orcomatloc = GPU_shader_get_uniform(shader, GPU_builtin_name(GPU_ORCO_MATRIX));
		return 1;
	}
	else {
		GPU_pass_free_nodes(&material->nodes);
	}

	return 0;
}

void GPU_material_free(ListBase *gpumaterial)
{
	for (LinkData *link = gpumaterial->first; link; link = link->next) {
		GPUMaterial *material = link->data;

		if (material->pass)
			GPU_pass_free(material->pass);

		for (LinkData *nlink = material->lamps.first; nlink; nlink = nlink->next) {
			GPULamp *lamp = nlink->data;

			if (material->ma) {
				Material *ma = material->ma;
				
				LinkData *next = NULL;
				for (LinkData *mlink = lamp->materials.first; mlink; mlink = next) {
					next = mlink->next;
					if (mlink->data == ma)
						BLI_freelinkN(&lamp->materials, mlink);
				}
			}
		}
		
		BLI_freelistN(&material->lamps);

		MEM_freeN(material);
	}

	BLI_freelistN(gpumaterial);
}

bool GPU_lamp_override_visible(GPULamp *lamp, SceneRenderLayer *srl, Material *ma)
{
	if (srl && srl->light_override)
		return BKE_group_object_exists(srl->light_override, lamp->ob);
	else if (ma && ma->group)
		return BKE_group_object_exists(ma->group, lamp->ob);
	else
		return true;
}

/* BGE-Remix P8: "Face Textures". The shader samples a dynamic texture
 * input; it is pointed at the face image's GL texture before binding, or at
 * a white pixel with the "on" uniform at 0 when the faces have no image. */
static GPUTexture *gpu_face_image_blank(void)
{
	static GPUTexture *blank = NULL;
	if (!blank) {
		float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
		blank = GPU_texture_create_2D(1, 1, white, GPU_HDR_NONE, NULL);
	}
	return blank;
}

/* ------------------------------------------------------------------------- */
/* BGE-Remix P8.5: Object coordinates from the slot's object */

/* the game engine's places for Blender objects (Object * -> float[4][4]) */
static GHash *gpu_texco_ob_matrices = NULL;

void GPU_texco_object_matrix_set(Object *ob, float obmat[4][4])
{
	float *m;
	if (!ob) {
		return;
	}
	if (!gpu_texco_ob_matrices) {
		gpu_texco_ob_matrices = BLI_ghash_ptr_new("gpu texco objects");
	}
	m = BLI_ghash_lookup(gpu_texco_ob_matrices, ob);
	if (!m) {
		m = MEM_mallocN(sizeof(float[4][4]), "gpu texco object matrix");
		BLI_ghash_insert(gpu_texco_ob_matrices, ob, m);
	}
	memcpy(m, obmat, sizeof(float[4][4]));
}

void GPU_texco_object_matrix_clear(void)
{
	if (gpu_texco_ob_matrices) {
		BLI_ghash_free(gpu_texco_ob_matrices, NULL, MEM_freeN);
		gpu_texco_ob_matrices = NULL;
	}
}

static void gpu_texco_object_imat(Object *ob, float r_imat[4][4])
{
	float (*m)[4] = (ob && gpu_texco_ob_matrices) ? BLI_ghash_lookup(gpu_texco_ob_matrices, ob) : NULL;
	if (!ob || !invert_m4_m4(r_imat, m ? m : ob->obmat)) {
		unit_m4(r_imat);
	}
}

/* a link to the inverse matrix of the slot's object (NULL: too many) */
static GPUNodeLink *gpu_texco_object_link(GPUMaterial *mat, MTex *mtex)
{
	int i;
	for (i = 0; i < mat->tot_texco_ob; i++) {
		if (mat->texco_mtex[i]->object == mtex->object) {
			break;
		}
	}
	if (i == mat->tot_texco_ob) {
		if (mat->tot_texco_ob == MAX_MTEX) {
			return NULL;
		}
		mat->texco_mtex[i] = mtex;
		gpu_texco_object_imat(mtex->object, mat->texco_obimat[i]);
		mat->tot_texco_ob++;
	}
	return GPU_dynamic_uniform((float *)mat->texco_obimat[i], GPU_DYNAMIC_OBJECT_TEXCO_IMAT, mtex->object);
}

/* ------------------------------------------------------------------------- */
/* BGE-Remix P8.5: Blender Internal's procedural textures
 * (gpu_shader_material_procedural.glsl) */

bool GPU_texture_is_procedural(const Tex *tex)
{
	if (!tex || (tex->use_nodes && tex->nodetree)) {
		return false;
	}
	switch (tex->type) {
		case TEX_CLOUDS:
		case TEX_WOOD:
		case TEX_MARBLE:
		case TEX_MAGIC:
		case TEX_BLEND:
		case TEX_STUCCI:
		case TEX_NOISE:
		case TEX_MUSGRAVE:
		case TEX_VORONOI:
		case TEX_DISTNOISE:
			return true;
		default:
			return false;
	}
}

/* drawn in GLSL here (the library needs GLSL 1.30) */
static bool gpu_tex_procedural(const Tex *tex)
{
	return GLEW_VERSION_3_0 && GPU_texture_is_procedural(tex);
}

/* whether the type gives a colour (before the colour band) */
static bool gpu_tex_procedural_rgb(const Tex *tex)
{
	return (tex->type == TEX_CLOUDS && tex->stype == TEX_COLOR) || tex->type == TEX_MAGIC ||
	       (tex->type == TEX_VORONOI && tex->vn_coltype != 0);
}

/* noise.c's tables in one float texture (see the GLSL file) */
static GPUTexture *gpu_procedural_tables(void)
{
	static GPUTexture *tables = NULL;
	if (!tables) {
		const unsigned char *perm;
		const float *hashvect, *hashpnt;
		float *pixels = MEM_callocN(sizeof(float) * 4 * 768, "procedural tables");
		BLI_noise_tables(&perm, &hashvect, &hashpnt);
		for (int i = 0; i < 256; i++) {
			pixels[4 * i] = (float)perm[i];
			for (int c = 0; c < 3; c++) {
				pixels[4 * (256 + i) + c] = hashvect[3 * i + c];
				pixels[4 * (512 + i) + c] = hashpnt[3 * i + c];
			}
		}
		tables = GPU_texture_create_2D(768, 1, pixels, GPU_HDR_FULL_FLOAT, NULL);
		MEM_freeN(pixels);
	}
	return tables;
}

static GPUNodeLink *gpu_procedural_tables_link(void)
{
	return GPU_dynamic_texture(gpu_procedural_tables(), GPU_DYNAMIC_SAMPLER_2DBUFFER, NULL);
}

/* the texture's settings, in the GLSL file's order */
static void gpu_procedural_params(const Tex *tex, float p[8][4])
{
	p[0][0] = tex->type;
	p[0][1] = tex->stype;
	p[0][2] = tex->noisebasis;
	p[0][3] = tex->noisebasis2;
	p[1][0] = tex->noisesize;
	p[1][1] = tex->noisedepth;
	p[1][2] = (tex->noisetype != TEX_NOISESOFT) ? 1.0f : 0.0f;
	p[1][3] = tex->turbul;
	p[2][0] = tex->bright;
	p[2][1] = tex->contrast;
	p[2][2] = tex->saturation;
	p[2][3] = (tex->flag & TEX_NO_CLAMP) ? 1.0f : 0.0f;
	p[3][0] = tex->rfac;
	p[3][1] = tex->gfac;
	p[3][2] = tex->bfac;
	p[3][3] = (tex->flag & TEX_FLIPBLEND) ? 1.0f : 0.0f;
	p[4][0] = tex->mg_H;
	p[4][1] = tex->mg_lacunarity;
	p[4][2] = tex->mg_octaves;
	p[4][3] = tex->mg_offset;
	p[5][0] = tex->mg_gain;
	p[5][1] = tex->ns_outscale;
	p[5][2] = tex->dist_amount;
	p[5][3] = tex->vn_mexp;
	p[6][0] = tex->vn_w1;
	p[6][1] = tex->vn_w2;
	p[6][2] = tex->vn_w3;
	p[6][3] = tex->vn_w4;
	p[7][0] = tex->vn_distm;
	p[7][1] = tex->vn_coltype;
	p[7][2] = tex->nabla;
	p[7][3] = 0.0f;
}

static GPUNodeLink *gpu_colorband_link(ColorBand *coba)
{
	float *array;
	int size;
	colorband_table_RGBA(coba, &array, &size);
	return GPU_texture(size, array);
}

/* The texture at texvec: its intensity, and its colour when it has one
 * (returns TEX_INT or TEX_RGB; *r_talpha: the colour's alpha counts) */
static int gpu_procedural_eval(GPUMaterial *mat, Tex *tex, GPUNodeLink *texvec,
                               GPUNodeLink **r_tin, GPUNodeLink **r_trgb, int *r_talpha)
{
	float p[8][4];
	gpu_procedural_params(tex, p);
	if ((tex->flag & TEX_COLORBAND) && tex->coba) {
		GPU_link(mat, "proc_texture_band", texvec,
		         GPU_uniform(p[0]), GPU_uniform(p[1]), GPU_uniform(p[2]), GPU_uniform(p[3]),
		         GPU_uniform(p[4]), GPU_uniform(p[5]), GPU_uniform(p[6]), GPU_uniform(p[7]),
		         gpu_procedural_tables_link(), gpu_colorband_link(tex->coba), r_tin, r_trgb);
		*r_talpha = 1;
		return TEX_RGB;
	}
	GPU_link(mat, "proc_texture", texvec,
	         GPU_uniform(p[0]), GPU_uniform(p[1]), GPU_uniform(p[2]), GPU_uniform(p[3]),
	         GPU_uniform(p[4]), GPU_uniform(p[5]), GPU_uniform(p[6]), GPU_uniform(p[7]),
	         gpu_procedural_tables_link(), r_tin, r_trgb);
	*r_talpha = 0;
	return gpu_tex_procedural_rgb(tex) ? TEX_RGB : TEX_INT;
}

bool GPU_material_link_procedural_node(GPUMaterial *mat, Tex *tex, GPUNodeLink *co,
                                       GPUNodeLink **r_value, GPUNodeLink **r_color, GPUNodeLink **r_normal)
{
	float p[8][4];
	if (!gpu_tex_procedural(tex)) {
		return false;
	}
	gpu_procedural_params(tex, p);
	if ((tex->flag & TEX_COLORBAND) && tex->coba) {
		GPU_link(mat, "proc_texture_node_band", co,
		         GPU_uniform(p[0]), GPU_uniform(p[1]), GPU_uniform(p[2]), GPU_uniform(p[3]),
		         GPU_uniform(p[4]), GPU_uniform(p[5]), GPU_uniform(p[6]), GPU_uniform(p[7]),
		         gpu_procedural_tables_link(), gpu_colorband_link(tex->coba), r_value, r_color, r_normal);
	}
	else {
		GPU_link(mat, "proc_texture_node", co,
		         GPU_uniform(p[0]), GPU_uniform(p[1]), GPU_uniform(p[2]), GPU_uniform(p[3]),
		         GPU_uniform(p[4]), GPU_uniform(p[5]), GPU_uniform(p[6]), GPU_uniform(p[7]),
		         gpu_procedural_tables_link(), r_value, r_color, r_normal);
	}
	return true;
}

/* the bump of a procedural: three samples per pixel (the renderer's 3-tap
 * bump, in 3D) */
static void gpu_procedural_bump(GPUMaterial *mat, MTex *mtex, Tex *tex, GPUNodeLink *texvec, GPUNodeLink *hscale,
                                GPUNodeLink **r_dBs, GPUNodeLink **r_dBt)
{
	float p[8][4];
	float uvmode = (mtex->texco == TEXCO_UV) ? 1.0f : 0.0f;
	gpu_procedural_params(tex, p);
	if ((tex->flag & TEX_COLORBAND) && tex->coba) {
		GPU_link(mat, "proc_bump_tap3_band", texvec,
		         GPU_uniform(p[0]), GPU_uniform(p[1]), GPU_uniform(p[2]), GPU_uniform(p[3]),
		         GPU_uniform(p[4]), GPU_uniform(p[5]), GPU_uniform(p[6]), GPU_uniform(p[7]),
		         gpu_procedural_tables_link(), gpu_colorband_link(tex->coba), GPU_uniform(&uvmode), hscale,
		         r_dBs, r_dBt);
	}
	else {
		GPU_link(mat, "proc_bump_tap3", texvec,
		         GPU_uniform(p[0]), GPU_uniform(p[1]), GPU_uniform(p[2]), GPU_uniform(p[3]),
		         GPU_uniform(p[4]), GPU_uniform(p[5]), GPU_uniform(p[6]), GPU_uniform(p[7]),
		         gpu_procedural_tables_link(), GPU_uniform(&uvmode), hscale, r_dBs, r_dBt);
	}
}

/* the renderer's procedural mapping (texco_mapping): UV in -1..1, then the
 * projection, offset and size */
static GPUNodeLink *gpu_procedural_mapping(GPUMaterial *mat, MTex *mtex, GPUNodeLink *co)
{
	GPUNodeLink *texvec;
	float proj[3] = {mtex->projx, mtex->projy, mtex->projz};
	if (mtex->texco == TEXCO_UV) {
		GPU_link(mat, "proc_texco_uv", co, &co);
	}
	GPU_link(mat, "proc_mapping", co, GPU_uniform(proj), GPU_uniform(mtex->size), GPU_uniform(mtex->ofs), &texvec);
	return texvec;
}

static void gpu_material_bind_face_image(GPUMaterial *material, double time, int mipmap)
{
	GPUTexture *tex = NULL;
	GPUInput *input;

	if (material->face_image) {
		tex = GPU_texture_from_blender(material->face_image, NULL, GL_TEXTURE_2D, false, time, mipmap);
	}
	material->face_image_on = tex ? 1.0f : 0.0f;
	if (!tex) {
		tex = gpu_face_image_blank();
	}
	for (input = material->pass->inputs.first; input; input = input->next) {
		if (input->dynamictex && input->dynamicdata == material)
		{
			input->tex = tex;
		}
	}
}

bool GPU_material_is_valid(GPUMaterial *material)
{
	return material && material->pass && GPU_pass_shader(material->pass);
}

bool GPU_material_uses_face_image(GPUMaterial *material)
{
	return material && material->use_face_image;
}

void GPU_material_set_face_image(GPUMaterial *material, Image *ima)
{
	if (material) {
		material->face_image = ima;
	}
}

/* BGE-Remix P8.10: with BGE_REMIX_SHADOW_DUMP, what a material's shader gets
 * for a lamp's shadow, once, after the lamp's buffer was written
 * (<lamp>.bind: the material's name (64 chars), then floats: viewmat,
 * viewinv, the cube's or cascades' matrices (5 x 16), cascade scale and
 * range, shadow params and clip, dynvec, dynco) */
static void gpu_lamp_shadow_dump_bind(GPULamp *lamp, GPUMaterial *material, float viewmat[4][4], float viewinv[4][4])
{
	const char *dir = getenv("BGE_REMIX_SHADOW_DUMP");
	if (!dir || !dir[0] || !lamp->dumped || lamp->bind_dumped) {
		return;
	}
	lamp->bind_dumped = true;
	char path[1024], name[64] = {0};
	BLI_snprintf(path, sizeof(path), "%s/%s.bind", dir, lamp->ob->id.name + 2);
	BLI_strncpy(name, material->ma ? material->ma->id.name + 2 : "", sizeof(name));
	FILE *f = fopen(path, "wb");
	if (f) {
		fwrite(name, 1, sizeof(name), f);
		fwrite(viewmat, sizeof(float), 16, f);
		fwrite(viewinv, sizeof(float), 16, f);
		fwrite(lamp->dyncube, sizeof(float), 16, f);
		fwrite(lamp->dyncascade, sizeof(float), 64, f);
		fwrite(lamp->cascade_scale, sizeof(float), 4, f);
		fwrite(lamp->cascade_range, sizeof(float), 4, f);
		fwrite(lamp->shadow_params, sizeof(float), 4, f);
		fwrite(lamp->shadow_clip, sizeof(float), 4, f);
		fwrite(lamp->dynvec, sizeof(float), 3, f);
		fwrite(lamp->dynco, sizeof(float), 3, f);
		fclose(f);
	}
}

void GPU_material_bind(
        GPUMaterial *material, int oblay, int viewlay, double time, int mipmap,
        float viewmat[4][4], float viewinv[4][4], float camerafactors[4], bool scenelock)
{
	if (material->pass) {
		GPUShader *shader = GPU_pass_shader(material->pass);
		SceneRenderLayer *srl = scenelock ? BLI_findlink(&material->scene->r.layers, material->scene->r.actlay) : NULL;

		if (srl)
			viewlay &= srl->lay;

		/* handle layer lamps */
		if (material->type == GPU_MATERIAL_TYPE_MESH) {
			for (LinkData *nlink = material->lamps.first; nlink; nlink = nlink->next) {
				GPULamp *lamp = nlink->data;
				
				if (!lamp->hide && (lamp->lay & viewlay) && (!(lamp->mode & LA_LAYER) || (lamp->lay & oblay)) &&
				    GPU_lamp_override_visible(lamp, srl, material->ma))
				{
					lamp->dynenergy = lamp->energy;
					copy_v3_v3(lamp->dyncol, lamp->col);
				}
				else {
					lamp->dynenergy = 0.0f;
					lamp->dyncol[0] = lamp->dyncol[1] = lamp->dyncol[2] = 0.0f;
				}
				
				if (material->dynproperty & DYN_LAMP_VEC) {
					copy_v3_v3(lamp->dynvec, lamp->vec);
					normalize_v3(lamp->dynvec);
					negate_v3(lamp->dynvec);
					mul_mat3_m4_v3(viewmat, lamp->dynvec);
				}
				
				if (material->dynproperty & DYN_LAMP_CO) {
					copy_v3_v3(lamp->dynco, lamp->co);
					mul_m4_v3(viewmat, lamp->dynco);
				}
				
				if (material->dynproperty & DYN_LAMP_IMAT) {
					mul_m4_m4m4(lamp->dynimat, lamp->imat, viewinv);
				}

				/* BGE-Remix P8.9: an area lamp's corners in view space */
				if ((material->dynproperty & DYN_LAMP_AREA) && lamp->type == LA_AREA) {
					for (int c = 0; c < 4; c++) {
						copy_v3_v3(lamp->dynarea[c], lamp->area_corner[c]);
						mul_m4_v3(viewmat, lamp->dynarea[c]);
						lamp->dynarea[c][3] = 1.0f;
					}
				}
				
				if (material->dynproperty & DYN_LAMP_PERSMAT) {
					/* The lamp matrices are already updated if we're using shadow buffers */
					if (!GPU_lamp_has_shadow_buffer(lamp)) {
						GPU_lamp_update_buffer_mats(lamp);
					}
					mul_m4_m4m4(lamp->dynpersmat, lamp->persmat, viewinv);
				}

				/* BGE-Remix P8.10: the cube's and the cascades' views from this camera */
				if ((material->dynproperty & DYN_LAMP_SHADOW) && GPU_lamp_has_shadow_buffer(lamp)) {
					if (lamp->shadow_kind == GPU_LAMP_SHADOW_CUBE) {
						mul_m4_m4m4(lamp->dyncube, lamp->viewmat, viewinv);
					}
					else if (lamp->shadow_kind == GPU_LAMP_SHADOW_CASCADE) {
						for (int c = 0; c < GPU_SHADOW_CASCADES; c++) {
							mul_m4_m4m4(lamp->dyncascade[c], lamp->cascade_persmat[c], viewinv);
						}
					}
					gpu_lamp_shadow_dump_bind(lamp, material, viewmat, viewinv);
				}
			}
		}
		
		/* BGE-Remix P8: the face image of the faces drawn next */
		if (material->use_face_image) {
			gpu_material_bind_face_image(material, time, mipmap);
		}

		/* BGE-Remix P8.5: where the coordinate objects are now */
		for (int i = 0; i < material->tot_texco_ob; i++) {
			gpu_texco_object_imat(material->texco_mtex[i]->object, material->texco_obimat[i]);
		}

		/* BGE-Remix P8.3: the lamp and world values the shader reads */
		gpu_shared_buffers_bind(material);

		/* note material must be bound before setting uniforms */
		GPU_pass_bind(material->pass, time, mipmap);

		/* handle per material built-ins */
		if (material->builtins & GPU_VIEW_MATRIX) {
			GPU_shader_uniform_vector(shader, material->viewmatloc, 16, 1, (float *)viewmat);
		}
		if (material->builtins & GPU_INVERSE_VIEW_MATRIX) {
			GPU_shader_uniform_vector(shader, material->invviewmatloc, 16, 1, (float *)viewinv);
		}
		if (material->builtins & GPU_CAMERA_TEXCO_FACTORS) {
			if (camerafactors) {
				GPU_shader_uniform_vector(shader, material->cameratexcofacloc, 4, 1, (float *)camerafactors);
			}
			else {
				/* use default, no scaling no offset */
				float borders[4] = {1.0f, 1.0f, 0.0f, 0.0f};
				GPU_shader_uniform_vector(shader, material->cameratexcofacloc, 4, 1, (float *)borders);
			}
		}

		GPU_pass_update_uniforms(material->pass);

		material->bound = 1;
	}
}

GPUBuiltin GPU_get_material_builtins(GPUMaterial *material)
{
	return material->builtins;
}

void GPU_material_bind_uniforms(
        GPUMaterial *material, float obmat[4][4], float viewmat[4][4], float obcol[4],
        float autobumpscale, GPUParticleInfo *pi, float object_info[3])
{
	if (material->pass) {
		GPUShader *shader = GPU_pass_shader(material->pass);
		float invmat[4][4], col[4];
		float localtoviewmat[4][4];
		float invlocaltoviewmat[4][4];

		/* handle per object builtins */
		if (material->builtins & GPU_OBJECT_MATRIX) {
			GPU_shader_uniform_vector(shader, material->obmatloc, 16, 1, (float *)obmat);
		}
		if (material->builtins & GPU_INVERSE_OBJECT_MATRIX) {
			invert_m4_m4(invmat, obmat);
			GPU_shader_uniform_vector(shader, material->invobmatloc, 16, 1, (float *)invmat);
		}
		if (material->builtins & GPU_LOC_TO_VIEW_MATRIX) {
			if (viewmat) {
				mul_m4_m4m4(localtoviewmat, viewmat, obmat);
				GPU_shader_uniform_vector(shader, material->localtoviewmatloc, 16, 1, (float *)localtoviewmat);
			}
		}
		if (material->builtins & GPU_INVERSE_LOC_TO_VIEW_MATRIX) {
			if (viewmat) {
				mul_m4_m4m4(localtoviewmat, viewmat, obmat);
				invert_m4_m4(invlocaltoviewmat, localtoviewmat);
				GPU_shader_uniform_vector(shader, material->invlocaltoviewmatloc, 16, 1, (float *)invlocaltoviewmat);
			}
		}
		if (material->builtins & GPU_OBCOLOR) {
			copy_v4_v4(col, obcol);
			CLAMP(col[3], 0.0f, 1.0f);
			GPU_shader_uniform_vector(shader, material->obcolloc, 4, 1, col);
		}
		if (material->builtins & GPU_AUTO_BUMPSCALE) {
			GPU_shader_uniform_vector(shader, material->obautobumpscaleloc, 1, 1, &autobumpscale);
		}
		if (material->builtins & GPU_PARTICLE_SCALAR_PROPS) {
			GPU_shader_uniform_vector(shader, material->partscalarpropsloc, 4, 1, pi->scalprops);
		}
		if (material->builtins & GPU_PARTICLE_LOCATION) {
			GPU_shader_uniform_vector(shader, material->partcoloc, 3, 1, pi->location);
		}
		if (material->builtins & GPU_PARTICLE_VELOCITY) {
			GPU_shader_uniform_vector(shader, material->partvel, 3, 1, pi->velocity);
		}
		if (material->builtins & GPU_PARTICLE_ANG_VELOCITY) {
			GPU_shader_uniform_vector(shader, material->partangvel, 3, 1, pi->angular_velocity);
		}
		if (material->builtins & GPU_OBJECT_INFO) {
			GPU_shader_uniform_vector(shader, material->objectinfoloc, 3, 1, object_info);
		}
		/* BGE-Remix P8.5: the texture space of the mesh, when given */
		if (material->builtins & GPU_ORCO_MATRIX) {
			float unit[4][4];
			unit_m4(unit);
			GPU_shader_uniform_vector(shader, material->orcomatloc, 16, 1,
			                          material->orcomat_set ? (float *)material->orcomat : (float *)unit);
		}
	}
	material->orcomat_set = false;
}

void GPU_material_set_orco_space(GPUMaterial *material, const float loc[3], const float size[3])
{
	int i;
	if (!material) {
		return;
	}
	/* orco = (co - loc) / size */
	unit_m4(material->orcomat);
	for (i = 0; i < 3; i++) {
		const float s = (fabsf(size[i]) > 1e-8f) ? size[i] : 1.0f;
		material->orcomat[i][i] = 1.0f / s;
		material->orcomat[3][i] = -loc[i] / s;
	}
	material->orcomat_set = true;
}

void GPU_material_unbind(GPUMaterial *material)
{
	if (material->pass) {
		material->bound = 0;
		GPU_pass_unbind(material->pass);
	}
}

bool GPU_material_bound(GPUMaterial *material)
{
	return material->bound;
}

Scene *GPU_material_scene(GPUMaterial *material)
{
	return material->scene;
}

GPUMatType GPU_Material_get_type(GPUMaterial *material)
{
	return material->type;
}


void GPU_material_vertex_attributes(GPUMaterial *material, GPUVertexAttribs *attribs)
{
	*attribs = material->attribs;
}

void GPU_material_output_link(GPUMaterial *material, GPUNodeLink *link)
{
	if (!material->outlink)
		material->outlink = link;
}

void GPU_material_enable_alpha(GPUMaterial *material)
{
	material->alpha = 1;
}

GPUBlendMode GPU_material_alpha_blend(GPUMaterial *material, float obcol[4])
{
	if (material->alpha || (material->obcolalpha && obcol[3] < 1.0f))
		return GPU_BLEND_ALPHA;
	else
		return GPU_BLEND_SOLID;
}

void gpu_material_add_node(GPUMaterial *material, GPUNode *node)
{
	BLI_addtail(&material->nodes, node);
}

/* Code generation */

bool GPU_material_do_color_management(GPUMaterial *mat)
{
	if (!BKE_scene_check_color_management_enabled(mat->scene))
		return false;

	/* BGE-Remix P8: the game setting "GLSL Color Management" (off for files
	 * saved in Multitexture, which drew colours as they are) */
	if (mat->scene->gm.flag & GAME_GLSL_NO_COLOR_MANAGEMENT)
		return false;

	return true;
}

bool GPU_material_use_new_shading_nodes(GPUMaterial *mat)
{
	return BKE_scene_use_new_shading_nodes(mat->scene);
}

bool GPU_material_use_world_space_shading(GPUMaterial *mat)
{
	return BKE_scene_use_world_space_shading(mat->scene);
}

static GPUNodeLink *lamp_get_visibility(GPUMaterial *mat, GPULamp *lamp, GPUNodeLink **lv, GPUNodeLink **dist)
{
	GPUNodeLink *visifac;

	/* from get_lamp_visibility */
	if (lamp->type == LA_SUN || lamp->type == LA_HEMI) {
		mat->dynproperty |= DYN_LAMP_VEC;
		GPU_link(mat, "lamp_visibility_sun_hemi",
		         GPU_dynamic_uniform(lamp->dynvec, GPU_DYNAMIC_LAMP_DYNVEC, lamp->ob), lv, dist, &visifac);
		return visifac;
	}
	else {
		mat->dynproperty |= DYN_LAMP_CO;
		GPU_link(mat, "lamp_visibility_other",
		         GPU_builtin(GPU_VIEW_POSITION),
		         GPU_dynamic_uniform(lamp->dynco, GPU_DYNAMIC_LAMP_DYNCO, lamp->ob), lv, dist, &visifac);

		if (lamp->type == LA_AREA)
			return visifac;

		switch (lamp->falloff_type) {
			case LA_FALLOFF_CONSTANT:
				break;
			case LA_FALLOFF_INVLINEAR:
				GPU_link(mat, "lamp_falloff_invlinear",
				         GPU_dynamic_uniform(&lamp->dist, GPU_DYNAMIC_LAMP_DISTANCE, lamp->ob), *dist, &visifac);
				break;
			case LA_FALLOFF_INVSQUARE:
				GPU_link(mat, "lamp_falloff_invsquare",
				         GPU_dynamic_uniform(&lamp->dist, GPU_DYNAMIC_LAMP_DISTANCE, lamp->ob), *dist, &visifac);
				break;
			case LA_FALLOFF_SLIDERS:
				GPU_link(mat, "lamp_falloff_sliders",
				         GPU_dynamic_uniform(&lamp->dist, GPU_DYNAMIC_LAMP_DISTANCE, lamp->ob),
				         GPU_dynamic_uniform(&lamp->att1, GPU_DYNAMIC_LAMP_ATT1, lamp->ob),
				         GPU_dynamic_uniform(&lamp->att2, GPU_DYNAMIC_LAMP_ATT2, lamp->ob), *dist, &visifac);
				break;
			case LA_FALLOFF_INVCOEFFICIENTS:
				GPU_link(mat, "lamp_falloff_invcoefficients",
					     GPU_dynamic_uniform(&lamp->coeff_const, GPU_DYNAMIC_LAMP_COEFFCONST, lamp->ob),
					     GPU_dynamic_uniform(&lamp->coeff_lin, GPU_DYNAMIC_LAMP_COEFFLIN, lamp->ob),
					     GPU_dynamic_uniform(&lamp->coeff_quad, GPU_DYNAMIC_LAMP_COEFFQUAD, lamp->ob), *dist, &visifac);
				break;
			case LA_FALLOFF_CURVE:
			{
				float *array;
				int size;

				curvemapping_initialize(lamp->curfalloff);
				curvemapping_table_RGBA(lamp->curfalloff, &array, &size);
				GPU_link(mat, "lamp_falloff_curve",
				         GPU_dynamic_uniform(&lamp->dist, GPU_DYNAMIC_LAMP_DISTANCE, lamp->ob),
				         GPU_texture(size, array), *dist, &visifac);

				break;
			}
		}

		if (lamp->mode & LA_SPHERE)
			GPU_link(mat, "lamp_visibility_sphere",
			         GPU_dynamic_uniform(&lamp->dist, GPU_DYNAMIC_LAMP_DISTANCE, lamp->ob),
			         *dist, visifac, &visifac);

		if (lamp->type == LA_SPOT) {
			GPUNodeLink *inpr;

			if (lamp->mode & LA_SQUARE) {
				mat->dynproperty |= DYN_LAMP_VEC | DYN_LAMP_IMAT;
				GPU_link(mat, "lamp_visibility_spot_square",
				         GPU_dynamic_uniform(lamp->dynvec, GPU_DYNAMIC_LAMP_DYNVEC, lamp->ob),
				         GPU_dynamic_uniform((float *)lamp->dynimat, GPU_DYNAMIC_LAMP_DYNIMAT, lamp->ob),
				GPU_dynamic_uniform((float *)lamp->spotvec, GPU_DYNAMIC_LAMP_SPOTSCALE, lamp->ob), *lv, &inpr);
			}
			else {
				mat->dynproperty |= DYN_LAMP_VEC | DYN_LAMP_IMAT;
				GPU_link(mat, "lamp_visibility_spot_circle",
				         GPU_dynamic_uniform(lamp->dynvec, GPU_DYNAMIC_LAMP_DYNVEC, lamp->ob),
				GPU_dynamic_uniform((float *)lamp->dynimat, GPU_DYNAMIC_LAMP_DYNIMAT, lamp->ob),
				GPU_dynamic_uniform((float *)lamp->spotvec, GPU_DYNAMIC_LAMP_SPOTSCALE, lamp->ob), *lv, &inpr);
			}
			
			GPU_link(mat, "lamp_visibility_spot",
			         GPU_dynamic_uniform(&lamp->spotsi, GPU_DYNAMIC_LAMP_SPOTSIZE, lamp->ob),
			         GPU_dynamic_uniform(&lamp->spotbl, GPU_DYNAMIC_LAMP_SPOTBLEND, lamp->ob),
			         inpr, visifac, &visifac);
		}

		GPU_link(mat, "lamp_visibility_clamp", visifac, &visifac);

		return visifac;
	}
}

#if 0
static void area_lamp_vectors(LampRen *lar)
{
	float xsize = 0.5f * lar->area_size, ysize = 0.5f * lar->area_sizey;

	/* make it smaller, so area light can be multisampled */
	float multifac = 1.0f / sqrtf((float)lar->ray_totsamp);
	xsize *= multifac;
	ysize *= multifac;

	/* corner vectors */
	lar->area[0][0] = lar->co[0] - xsize * lar->mat[0][0] - ysize * lar->mat[1][0];
	lar->area[0][1] = lar->co[1] - xsize * lar->mat[0][1] - ysize * lar->mat[1][1];
	lar->area[0][2] = lar->co[2] - xsize * lar->mat[0][2] - ysize * lar->mat[1][2];

	/* corner vectors */
	lar->area[1][0] = lar->co[0] - xsize * lar->mat[0][0] + ysize * lar->mat[1][0];
	lar->area[1][1] = lar->co[1] - xsize * lar->mat[0][1] + ysize * lar->mat[1][1];
	lar->area[1][2] = lar->co[2] - xsize * lar->mat[0][2] + ysize * lar->mat[1][2];

	/* corner vectors */
	lar->area[2][0] = lar->co[0] + xsize * lar->mat[0][0] + ysize * lar->mat[1][0];
	lar->area[2][1] = lar->co[1] + xsize * lar->mat[0][1] + ysize * lar->mat[1][1];
	lar->area[2][2] = lar->co[2] + xsize * lar->mat[0][2] + ysize * lar->mat[1][2];

	/* corner vectors */
	lar->area[3][0] = lar->co[0] + xsize * lar->mat[0][0] - ysize * lar->mat[1][0];
	lar->area[3][1] = lar->co[1] + xsize * lar->mat[0][1] - ysize * lar->mat[1][1];
	lar->area[3][2] = lar->co[2] + xsize * lar->mat[0][2] - ysize * lar->mat[1][2];
	/* only for correction button size, matrix size works on energy */
	lar->areasize = lar->dist * lar->dist / (4.0f * xsize * ysize);
}
#endif

static void ramp_blend(
        GPUMaterial *mat, GPUNodeLink *fac, GPUNodeLink *col1, GPUNodeLink *col2, int type,
        GPUNodeLink **r_col)
{
	static const char *names[] = {"mix_blend", "mix_add", "mix_mult", "mix_sub",
		"mix_screen", "mix_div", "mix_diff", "mix_dark", "mix_light",
		"mix_overlay", "mix_dodge", "mix_burn", "mix_hue", "mix_sat",
		"mix_val", "mix_color", "mix_soft", "mix_linear"};

	GPU_link(mat, names[type], fac, col1, col2, r_col);
}

static void do_colorband_blend(
        GPUMaterial *mat, ColorBand *coba, GPUNodeLink *fac, float rampfac, int type,
        GPUNodeLink *incol, GPUNodeLink **r_col)
{
	GPUNodeLink *tmp, *alpha, *col;
	float *array;
	int size;

	/* do colorband */
	colorband_table_RGBA(coba, &array, &size);
	GPU_link(mat, "valtorgb", fac, GPU_texture(size, array), &col, &tmp);

	/* use alpha in fac */
	GPU_link(mat, "mtex_alpha_from_col", col, &alpha);
	GPU_link(mat, "math_multiply", alpha, GPU_uniform(&rampfac), &fac);

	/* blending method */
	ramp_blend(mat, fac, incol, col, type, r_col);
}

static void ramp_diffuse_result(GPUShadeInput *shi, GPUNodeLink **diff)
{
	Material *ma = shi->mat;
	GPUMaterial *mat = shi->gpumat;

	if (!(mat->scene->gm.flag & GAME_GLSL_NO_RAMPS)) {
		if (ma->ramp_col) {
			if (ma->rampin_col == MA_RAMP_IN_RESULT) {
				GPUNodeLink *fac;
				GPU_link(mat, "ramp_rgbtobw", *diff, &fac);
				
				/* colorband + blend */
				do_colorband_blend(mat, ma->ramp_col, fac, ma->rampfac_col, ma->rampblend_col, *diff, diff);
			}
		}
	}
}

static void add_to_diffuse(
        GPUMaterial *mat, Material *ma, GPUShadeInput *shi, GPUNodeLink *is, GPUNodeLink *rgb,
        GPUNodeLink **r_diff)
{
	GPUNodeLink *fac, *tmp, *addcol;
	
	if (!(mat->scene->gm.flag & GAME_GLSL_NO_RAMPS) &&
	    ma->ramp_col && (ma->mode & MA_RAMP_COL))
	{
		/* MA_RAMP_IN_RESULT is exceptional */
		if (ma->rampin_col == MA_RAMP_IN_RESULT) {
			addcol = shi->rgb;
		}
		else {
			/* input */
			switch (ma->rampin_col) {
				case MA_RAMP_IN_ENERGY:
					GPU_link(mat, "ramp_rgbtobw", rgb, &fac);
					break;
				case MA_RAMP_IN_SHADER:
					fac = is;
					break;
				case MA_RAMP_IN_NOR:
					GPU_link(mat, "vec_math_dot", shi->view, shi->vn, &tmp, &fac);
					break;
				default:
					GPU_link(mat, "set_value_zero", &fac);
					break;
			}

			/* colorband + blend */
			do_colorband_blend(mat, ma->ramp_col, fac, ma->rampfac_col, ma->rampblend_col, shi->rgb, &addcol);
		}
	}
	else
		addcol = shi->rgb;

	/* output to */
	GPU_link(mat, "shade_madd", *r_diff, rgb, addcol, r_diff);
}

static void ramp_spec_result(GPUShadeInput *shi, GPUNodeLink **spec)
{
	Material *ma = shi->mat;
	GPUMaterial *mat = shi->gpumat;

	if (!(mat->scene->gm.flag & GAME_GLSL_NO_RAMPS) &&
	    ma->ramp_spec && ma->rampin_spec == MA_RAMP_IN_RESULT)
	{
		GPUNodeLink *fac;
		GPU_link(mat, "ramp_rgbtobw", *spec, &fac);
		
		/* colorband + blend */
		do_colorband_blend(mat, ma->ramp_spec, fac, ma->rampfac_spec, ma->rampblend_spec, *spec, spec);
	}
}

static void do_specular_ramp(GPUShadeInput *shi, GPUNodeLink *is, GPUNodeLink *t, GPUNodeLink **spec)
{
	Material *ma = shi->mat;
	GPUMaterial *mat = shi->gpumat;
	GPUNodeLink *fac, *tmp;

	*spec = shi->specrgb;

	/* MA_RAMP_IN_RESULT is exception */
	if (ma->ramp_spec && (ma->rampin_spec != MA_RAMP_IN_RESULT)) {
		
		/* input */
		switch (ma->rampin_spec) {
			case MA_RAMP_IN_ENERGY:
				fac = t;
				break;
			case MA_RAMP_IN_SHADER:
				fac = is;
				break;
			case MA_RAMP_IN_NOR:
				GPU_link(mat, "vec_math_dot", shi->view, shi->vn, &tmp, &fac);
				break;
			default:
				GPU_link(mat, "set_value_zero", &fac);
				break;
		}
		
		/* colorband + blend */
		do_colorband_blend(mat, ma->ramp_spec, fac, ma->rampfac_spec, ma->rampblend_spec, *spec, spec);
	}
}

static void add_user_list(ListBase *list, void *data)
{
	LinkData *link = MEM_callocN(sizeof(LinkData), "GPULinkData");
	link->data = data;
	BLI_addtail(list, link);
}

/* BGE-Remix P8.9: a procedural texture on a lamp, as Blender Internal maps it
 * (View: across the spot's cone, -1..1; Global and the others: the world) */
static void shade_light_procedural(GPUMaterial *mat, GPULamp *lamp, MTex *mtex, GPUNodeLink **rgb)
{
	GPUNodeLink *co, *texvec, *tin, *trgb, *tcol, *fact;
	int talpha = 0;
	float one = 1.0f, col[4] = {mtex->r, mtex->g, mtex->b, 1.0f};

	if (mtex->texco == TEXCO_VIEW || mtex->texco == TEXCO_UV) {
		mat->dynproperty |= DYN_LAMP_PERSMAT;
		GPU_link(mat, "lamp_texco_view", GPU_builtin(GPU_VIEW_POSITION),
		         GPU_dynamic_uniform((float *)lamp->dynpersmat, GPU_DYNAMIC_LAMP_DYNPERSMAT, lamp->ob), &co);
	}
	else {
		GPU_link(mat, "lamp_texco_global", GPU_builtin(GPU_VIEW_POSITION), GPU_builtin(GPU_INVERSE_VIEW_MATRIX), &co);
	}
	texvec = gpu_procedural_mapping(mat, mtex, co);
	if (gpu_procedural_eval(mat, mtex->tex, texvec, &tin, &trgb, &talpha) & TEX_RGB) {
		tcol = trgb;
		GPU_link(mat, "set_value", GPU_uniform(&one), &fact);
	}
	else {
		GPU_link(mat, "set_rgba", GPU_uniform(col), &tcol);
		fact = tin;
	}
	texture_rgb_blend(mat, tcol, *rgb, fact, GPU_uniform(&mtex->colfac), mtex->blendtype, rgb);
}

static void shade_light_textures(GPUMaterial *mat, GPULamp *lamp, GPUNodeLink **rgb)
{
	for (int i = 0; i < MAX_MTEX; ++i) {
		MTex *mtex = lamp->la->mtex[i];

		if (mtex && mtex->tex && GPU_texture_is_procedural(mtex->tex)) {
			shade_light_procedural(mat, lamp, mtex, rgb);
		}
		else if (mtex && mtex->tex && (mtex->tex->type & TEX_IMAGE) && mtex->tex->ima) {
			mat->dynproperty |= DYN_LAMP_PERSMAT;

			float one = 1.0f;
			GPUNodeLink *tex_rgb;

			GPU_link(mat, "shade_light_texture",
			         GPU_builtin(GPU_VIEW_POSITION),
			         GPU_image(mtex->tex->ima, &mtex->tex->iuser, false),
			         GPU_dynamic_uniform((float *)lamp->dynpersmat, GPU_DYNAMIC_LAMP_DYNPERSMAT, lamp->ob),
			         &tex_rgb);
			/* BGE-Remix P8.9: the image's colours as F12 lights with them */
			if (GPU_material_do_color_management(mat)) {
				GPU_link(mat, "srgb_to_linearrgb", tex_rgb, &tex_rgb);
			}
			texture_rgb_blend(mat, tex_rgb, *rgb, GPU_uniform(&one), GPU_uniform(&mtex->colfac), mtex->blendtype, rgb);
		}
	}
}

/* BGE-Remix P8.10: the scene's depth from the camera, for contact shadows
 * (one texture for all materials, sized by GPU_scene_depth_begin) */
static GPUTexture *gpu_scene_depth_tex = NULL;
static unsigned int gpu_scene_depth_fbo = 0;
static int gpu_scene_depth_size[2] = {0, 0};
static int gpu_scene_depth_prevfb = 0;
static float gpu_scene_depth_on = 0.0f;
static bool gpu_scene_depth_drawing = false;

/* a depth texture read as values (sampler2D), not compared */
static void gpu_shadow_texture_raw(GPUTexture *tex)
{
	glBindTexture(GL_TEXTURE_2D, GPU_texture_opengl_bindcode(tex));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
}

static GPUTexture *gpu_scene_depth(void)
{
	if (!gpu_scene_depth_tex) {
		gpu_scene_depth_tex = GPU_texture_create_depth(1, 1, NULL);
		if (gpu_scene_depth_tex) {
			gpu_shadow_texture_raw(gpu_scene_depth_tex);
			gpu_scene_depth_size[0] = gpu_scene_depth_size[1] = 1;
		}
	}
	return gpu_scene_depth_tex;
}

/* BGE-Remix P8.10: the lamp's shadow for the point being shaded (1 lit, 0 in
 * shadow): the lamp's map (spot: one view, point and area: a cube, sun:
 * cascades), soft from the light's size, then contact shadows */
static GPUNodeLink *gpu_lamp_shadow(GPUMaterial *mat, GPULamp *lamp, GPUNodeLink *vn, GPUNodeLink *lv, GPUNodeLink *inp)
{
	GPUNodeLink *shadfac = NULL;

	mat->dynproperty |= DYN_LAMP_PERSMAT | DYN_LAMP_SHADOW;

	if (lamp->depthfb) {
		/* Variance (spot lamps) */
		GPU_link(mat, "test_shadowbuf_vsm",
		         GPU_builtin(GPU_VIEW_POSITION),
		         GPU_dynamic_texture(lamp->tex, GPU_DYNAMIC_SAMPLER_2DSHADOW, lamp->ob),
		         GPU_dynamic_uniform((float *)lamp->dynpersmat, GPU_DYNAMIC_LAMP_DYNPERSMAT, lamp->ob),
		         GPU_dynamic_uniform(&lamp->bias, GPU_DYNAMIC_LAMP_SHADOWBIAS, lamp->ob),
		         GPU_dynamic_uniform(&lamp->bleedbias, GPU_DYNAMIC_LAMP_BLEEDBIAS, lamp->ob), inp, &shadfac);
	}
	else if (lamp->shadow_kind == GPU_LAMP_SHADOW_CUBE) {
		GPU_link(mat, "shadow_cube_soft",
		         GPU_builtin(GPU_VIEW_POSITION), vn, inp,
		         GPU_dynamic_texture(lamp->tex, GPU_DYNAMIC_SAMPLER_2DSHADOW, lamp->ob),
		         GPU_dynamic_uniform((float *)lamp->dyncube, GPU_DYNAMIC_LAMP_SHADOWMAT, lamp->ob),
		         GPU_dynamic_uniform(lamp->shadow_params, GPU_DYNAMIC_LAMP_SHADOWPARAMS, lamp->ob),
		         GPU_dynamic_uniform(lamp->shadow_clip, GPU_DYNAMIC_LAMP_SHADOWCLIP, lamp->ob), &shadfac);
	}
	else if (lamp->shadow_kind == GPU_LAMP_SHADOW_CASCADE) {
		GPU_link(mat, "shadow_sun_soft",
		         GPU_builtin(GPU_VIEW_POSITION), vn, inp,
		         GPU_dynamic_texture(lamp->tex, GPU_DYNAMIC_SAMPLER_2DSHADOW, lamp->ob),
		         GPU_dynamic_uniform((float *)lamp->dyncascade[0], GPU_DYNAMIC_LAMP_SHADOWMAT, lamp->ob),
		         GPU_dynamic_uniform((float *)lamp->dyncascade[1], GPU_DYNAMIC_LAMP_SHADOWMAT, lamp->ob),
		         GPU_dynamic_uniform((float *)lamp->dyncascade[2], GPU_DYNAMIC_LAMP_SHADOWMAT, lamp->ob),
		         GPU_dynamic_uniform((float *)lamp->dyncascade[3], GPU_DYNAMIC_LAMP_SHADOWMAT, lamp->ob),
		         GPU_dynamic_uniform(lamp->cascade_scale, GPU_DYNAMIC_LAMP_SHADOWCLIP, lamp->ob),
		         GPU_dynamic_uniform(lamp->cascade_range, GPU_DYNAMIC_LAMP_SHADOWCLIP, lamp->ob),
		         GPU_dynamic_uniform(lamp->shadow_params, GPU_DYNAMIC_LAMP_SHADOWPARAMS, lamp->ob), &shadfac);
	}
	else {
		GPU_link(mat, "shadow_spot_soft",
		         GPU_builtin(GPU_VIEW_POSITION), vn, inp,
		         GPU_dynamic_texture(lamp->tex, GPU_DYNAMIC_SAMPLER_2DSHADOW, lamp->ob),
		         GPU_dynamic_uniform((float *)lamp->dynpersmat, GPU_DYNAMIC_LAMP_DYNPERSMAT, lamp->ob),
		         GPU_dynamic_uniform(lamp->shadow_params, GPU_DYNAMIC_LAMP_SHADOWPARAMS, lamp->ob),
		         GPU_dynamic_uniform(lamp->shadow_clip, GPU_DYNAMIC_LAMP_SHADOWCLIP, lamp->ob), &shadfac);
	}

	/* contact shadows, where the scene's depth was drawn */
	GPUTexture *depth = gpu_scene_depth();
	if (depth) {
		GPU_link(mat, "shadow_contact",
		         GPU_builtin(GPU_VIEW_POSITION), lv,
		         GPU_dynamic_texture(depth, GPU_DYNAMIC_SAMPLER_2DBUFFER, NULL),
		         GPU_dynamic_uniform(lamp->contact, GPU_DYNAMIC_LAMP_CONTACT, lamp->ob),
		         GPU_dynamic_uniform(&gpu_scene_depth_on, GPU_DYNAMIC_LAMP_CONTACT, lamp->ob), shadfac, &shadfac);
	}

	return shadfac;
}

static void shade_one_light(GPUShadeInput *shi, GPUShadeResult *shr, GPULamp *lamp)
{
	Material *ma = shi->mat;
	GPUMaterial *mat = shi->gpumat;
	GPUNodeLink *lv, *dist, *is, *inp, *i;
	GPUNodeLink *outcol, *specfac, *t, *shadfac = NULL, *lcol;
	float one = 1.0f;

	if ((lamp->mode & LA_ONLYSHADOW) && !(ma->mode & MA_SHADOW))
		return;
	
	GPUNodeLink *vn = shi->vn;
	GPUNodeLink *view = shi->view;

	GPUNodeLink *visifac = lamp_get_visibility(mat, lamp, &lv, &dist);

#if 0
	if (ma->mode & MA_TANGENT_V)
		GPU_link(mat, "shade_tangent_v", lv, GPU_attribute(CD_TANGENT, ""), &vn);
#endif
	
	GPU_link(mat, "shade_inp", vn, lv, &inp);

	if (lamp->mode & LA_NO_DIFF) {
		GPU_link(mat, "shade_is_no_diffuse", &is);
	}
	else if (lamp->type == LA_HEMI) {
		GPU_link(mat, "shade_is_hemi", inp, &is);
	}
	else {
		if (lamp->type == LA_AREA) {
			/* BGE-Remix P8.9: the lamp's corners and size correction (both
			 * were zeros: area lamps gave no light in the game) */
			mat->dynproperty |= DYN_LAMP_VEC | DYN_LAMP_CO | DYN_LAMP_AREA;
			GPU_link(mat, "shade_inp_area",
			         GPU_builtin(GPU_VIEW_POSITION),
			         GPU_dynamic_uniform(lamp->dynco, GPU_DYNAMIC_LAMP_DYNCO, lamp->ob),
			         GPU_dynamic_uniform(lamp->dynvec, GPU_DYNAMIC_LAMP_DYNVEC, lamp->ob), vn,
			         GPU_dynamic_uniform((float *)lamp->dynarea, GPU_DYNAMIC_LAMP_AREA, lamp->ob),
			         GPU_dynamic_uniform(&lamp->areasize, GPU_DYNAMIC_LAMP_AREASIZE, lamp->ob),
			         GPU_uniform(&lamp->k), &inp);
		}

		is = inp; /* Lambert */

		if (!(mat->scene->gm.flag & GAME_GLSL_NO_SHADERS)) {
			if (ma->diff_shader == MA_DIFF_ORENNAYAR)
				GPU_link(mat, "shade_diffuse_oren_nayer", inp, vn, lv, view,
				         GPU_uniform(&ma->roughness), &is);
			else if (ma->diff_shader == MA_DIFF_TOON)
				GPU_link(mat, "shade_diffuse_toon", vn, lv, view,
				         GPU_uniform(&ma->param[0]), GPU_uniform(&ma->param[1]), &is);
			else if (ma->diff_shader == MA_DIFF_MINNAERT)
				GPU_link(mat, "shade_diffuse_minnaert", inp, vn, view,
				         GPU_uniform(&ma->darkness), &is);
			else if (ma->diff_shader == MA_DIFF_FRESNEL)
				GPU_link(mat, "shade_diffuse_fresnel", vn, lv, view,
				         GPU_uniform(&ma->param[0]), GPU_uniform(&ma->param[1]), &is);
		}
	}

	if (!(mat->scene->gm.flag & GAME_GLSL_NO_SHADERS))
		if (ma->shade_flag & MA_CUBIC)
			GPU_link(mat, "shade_cubic", is, &is);
	
	i = is;
	GPU_link(mat, "shade_visifac", i, visifac, shi->refl, &i);
	
	GPU_link(mat, "set_rgb", GPU_dynamic_uniform(lamp->dyncol, GPU_DYNAMIC_LAMP_DYNCOL, lamp->ob), &lcol);
	shade_light_textures(mat, lamp, &lcol);
	GPU_link(mat, "shade_mul_value_v3",
	         GPU_dynamic_uniform(&lamp->dynenergy, GPU_DYNAMIC_LAMP_DYNENERGY, lamp->ob), lcol, &lcol);

#if 0
	if (ma->mode & MA_TANGENT_VN)
		GPU_link(mat, "shade_tangent_v_spec", GPU_attribute(CD_TANGENT, ""), &vn);
#endif

	/* this replaces if (i > 0.0) conditional until that is supported */
	/* done in shade_visifac now, GPU_link(mat, "mtex_value_clamp_positive", i, &i); */

	if ((ma->mode & MA_SHADOW) && GPU_lamp_has_shadow_buffer(lamp)) {
		if (!(mat->scene->gm.flag & GAME_GLSL_NO_SHADOWS)) {
			/* BGE-Remix P8.10: every lamp type, soft from the light's size */
			shadfac = gpu_lamp_shadow(mat, lamp, vn, lv, inp);

			if (lamp->mode & LA_ONLYSHADOW) {
				GPUNodeLink *shadrgb;
				GPU_link(mat, "shade_only_shadow", i, shadfac,
					GPU_dynamic_uniform(&lamp->dynenergy, GPU_DYNAMIC_LAMP_DYNENERGY, lamp->ob),
					GPU_dynamic_uniform(lamp->shadow_color, GPU_DYNAMIC_LAMP_SHADOWCOLOR, lamp->ob), &shadrgb);
				
				if (!(lamp->mode & LA_NO_DIFF)) {
					GPU_link(mat, "shade_only_shadow_diffuse", shadrgb, shi->rgb,
					         shr->diff, &shr->diff);
				}

				if (!(lamp->mode & LA_NO_SPEC)) {
					GPU_link(mat, "shade_only_shadow_specular", shadrgb, shi->specrgb,
					         shr->spec, &shr->spec);
				}
				
				add_user_list(&mat->lamps, lamp);
				add_user_list(&lamp->materials, shi->gpumat->ma);
				return;
			}
		}
	}
	else if ((mat->scene->gm.flag & GAME_GLSL_NO_SHADOWS) && (lamp->mode & LA_ONLYSHADOW)) {
		add_user_list(&mat->lamps, lamp);
		add_user_list(&lamp->materials, shi->gpumat->ma);
		return;
	}
	else
		GPU_link(mat, "set_value", GPU_uniform(&one), &shadfac);

	if (shi->physical) {
		/* BGE-Remix P8.7: metal, roughness and Fresnel */
		float flags[3] = {
			(lamp->mode & LA_NO_DIFF) ? 1.0f : 0.0f,
			(lamp->mode & (LA_NO_SPEC | LA_ONLYSHADOW)) ? 1.0f : 0.0f,
			(lamp->type == LA_HEMI) ? 1.0f : 0.0f,
		};
		MaterialPhysical ph;
		BKE_material_physical_get(ma, &ph);
		GPU_link(mat, "shade_pbr_light", vn, lv, view, inp, visifac, shadfac, lcol,
		         GPU_dynamic_uniform(lamp->shadow_color, GPU_DYNAMIC_LAMP_SHADOWCOLOR, lamp->ob), shi->pbr_diff, shi->pbr_f0, shi->roughness,
		         GPU_uniform(&ph.subsurface), GPU_uniform(flags), shr->diff, shr->spec, &shr->diff, &shr->spec);
		add_user_list(&mat->lamps, lamp);
		add_user_list(&lamp->materials, shi->gpumat->ma);
		return;
	}

	if (GPU_link_changed(shi->refl) || ma->ref != 0.0f) {
		if (!(lamp->mode & LA_NO_DIFF)) {
			GPUNodeLink *rgb;
			GPU_link(mat, "shade_mul_value", i, lcol, &rgb);
			GPU_link(mat, "mtex_value_invert", shadfac, &shadfac);
			GPU_link(mat, "mix_mult",  shadfac, rgb, GPU_dynamic_uniform(lamp->shadow_color, GPU_DYNAMIC_LAMP_SHADOWCOLOR, lamp->ob), &rgb);
			GPU_link(mat, "mtex_value_invert", shadfac, &shadfac);
			add_to_diffuse(mat, ma, shi, is, rgb, &shr->diff);
		}
	}

	if (mat->scene->gm.flag & GAME_GLSL_NO_SHADERS) {
		/* pass */
	}
	else if (!(lamp->mode & LA_NO_SPEC) && !(lamp->mode & LA_ONLYSHADOW) &&
	         (GPU_link_changed(shi->spec) || ma->spec != 0.0f))
	{
		if (lamp->type == LA_HEMI) {
			GPU_link(mat, "shade_hemi_spec", vn, lv, view, GPU_uniform(&ma->spec), shi->har, visifac, &t);
			GPU_link(mat, "shade_add_spec", t, lcol, shi->specrgb, &outcol);
			GPU_link(mat, "shade_add_clamped", shr->spec, outcol, &shr->spec);
		}
		else {
			if (ma->spec_shader == MA_SPEC_PHONG) {
				GPU_link(mat, "shade_phong_spec", vn, lv, view, shi->har, &specfac);
			}
			else if (ma->spec_shader == MA_SPEC_COOKTORR) {
				GPU_link(mat, "shade_cooktorr_spec", vn, lv, view, shi->har, &specfac);
			}
			else if (ma->spec_shader == MA_SPEC_BLINN) {
				GPU_link(mat, "shade_blinn_spec", vn, lv, view,
				         GPU_uniform(&ma->refrac), shi->har, &specfac);
			}
			else if (ma->spec_shader == MA_SPEC_WARDISO) {
				GPU_link(mat, "shade_wardiso_spec", vn, lv, view,
				         GPU_uniform(&ma->rms), &specfac);
			}
			else {
				GPU_link(mat, "shade_toon_spec", vn, lv, view,
				         GPU_uniform(&ma->param[2]), GPU_uniform(&ma->param[3]), &specfac);
			}

			if (lamp->type == LA_AREA)
				GPU_link(mat, "shade_spec_area_inp", specfac, inp, &specfac);

			GPU_link(mat, "shade_spec_t", shadfac, shi->spec, visifac, specfac, &t); 

			if (ma->mode & MA_RAMP_SPEC) {
				GPUNodeLink *spec;
				do_specular_ramp(shi, specfac, t, &spec);
				GPU_link(mat, "shade_add_spec", t, lcol, spec, &outcol);
				GPU_link(mat, "shade_add_clamped", shr->spec, outcol, &shr->spec);
			}
			else {
				GPU_link(mat, "shade_add_spec", t, lcol, shi->specrgb, &outcol);
				GPU_link(mat, "shade_add_clamped", shr->spec, outcol, &shr->spec);
			}
		}
	}

	add_user_list(&mat->lamps, lamp);
	add_user_list(&lamp->materials, shi->gpumat->ma);
}

/* BGE-Remix P8.9: a lamp in a material's exclusive light group lights only
 * that group's materials (as F12: add_lightgroup) */
static bool gpu_lamp_in_exclusive_group(Object *ob)
{
	for (Material *ma = G.main->mat.first; ma; ma = ma->id.next) {
		if (ma->group && (ma->mode & MA_GROUP_NOLAY) && BKE_group_object_exists(ma->group, ob)) {
			return true;
		}
	}
	return false;
}

static void material_lights(GPUShadeInput *shi, GPUShadeResult *shr)
{
	Base *base;
	Scene *sce_iter;
	Material *ma = shi->mat;

	/* BGE-Remix P8.9: a material with a light group is lit by that group's
	 * lamps only (F12 does so; the game lit it with every lamp) */
	if (ma && ma->group) {
		for (GroupObject *go = ma->group->gobject.first; go; go = go->next) {
			if (go->ob && go->ob->type == OB_LAMP) {
				GPULamp *lamp = GPU_lamp_from_blender(shi->gpumat->scene, go->ob, NULL);
				if (lamp)
					shade_one_light(shi, shr, lamp);
			}
		}
		GPU_link(shi->gpumat, "shade_clamp_positive", shr->spec, &shr->spec);
		GPU_link(shi->gpumat, "shade_clamp_positive", shr->diff, &shr->diff);
		return;
	}

	for (SETLOOPER(shi->gpumat->scene, sce_iter, base)) {
		Object *ob = base->object;

		if (ob->type == OB_LAMP && !gpu_lamp_in_exclusive_group(ob)) {
			GPULamp *lamp = GPU_lamp_from_blender(shi->gpumat->scene, ob, NULL);
			if (lamp)
				shade_one_light(shi, shr, lamp);
		}

		if (ob->transflag & OB_DUPLI) {
			ListBase *lb = object_duplilist(G.main->eval_ctx, shi->gpumat->scene, ob);
			
			for (DupliObject *dob = lb->first; dob; dob = dob->next) {
				Object *ob_iter = dob->ob;

				if (ob_iter->type == OB_LAMP) {
					float omat[4][4];
					copy_m4_m4(omat, ob_iter->obmat);
					copy_m4_m4(ob_iter->obmat, dob->mat);

					GPULamp *lamp = GPU_lamp_from_blender(shi->gpumat->scene, ob_iter, ob);
					if (lamp)
						shade_one_light(shi, shr, lamp);

					copy_m4_m4(ob_iter->obmat, omat);
				}
			}
			
			free_object_duplilist(lb);
		}
	}

	/* prevent only shadow lamps from producing negative colors.*/
	GPU_link(shi->gpumat, "shade_clamp_positive", shr->spec, &shr->spec);
	GPU_link(shi->gpumat, "shade_clamp_positive", shr->diff, &shr->diff);
}

static void texture_rgb_blend(
        GPUMaterial *mat, GPUNodeLink *tex, GPUNodeLink *out, GPUNodeLink *fact, GPUNodeLink *facg,
        int blendtype, GPUNodeLink **in)
{
	switch (blendtype) {
		case MTEX_BLEND:
			GPU_link(mat, "mtex_rgb_blend", out, tex, fact, facg, in);
			break;
		case MTEX_MUL:
			GPU_link(mat, "mtex_rgb_mul", out, tex, fact, facg, in);
			break;
		case MTEX_SCREEN:
			GPU_link(mat, "mtex_rgb_screen", out, tex, fact, facg, in);
			break;
		case MTEX_OVERLAY:
			GPU_link(mat, "mtex_rgb_overlay", out, tex, fact, facg, in);
			break;
		case MTEX_SUB:
			GPU_link(mat, "mtex_rgb_sub", out, tex, fact, facg, in);
			break;
		case MTEX_ADD:
			GPU_link(mat, "mtex_rgb_add", out, tex, fact, facg, in);
			break;
		case MTEX_DIV:
			GPU_link(mat, "mtex_rgb_div", out, tex, fact, facg, in);
			break;
		case MTEX_DIFF:
			GPU_link(mat, "mtex_rgb_diff", out, tex, fact, facg, in);
			break;
		case MTEX_DARK:
			GPU_link(mat, "mtex_rgb_dark", out, tex, fact, facg, in);
			break;
		case MTEX_LIGHT:
			GPU_link(mat, "mtex_rgb_light", out, tex, fact, facg, in);
			break;
		case MTEX_BLEND_HUE:
			GPU_link(mat, "mtex_rgb_hue", out, tex, fact, facg, in);
			break;
		case MTEX_BLEND_SAT:
			GPU_link(mat, "mtex_rgb_sat", out, tex, fact, facg, in);
			break;
		case MTEX_BLEND_VAL:
			GPU_link(mat, "mtex_rgb_val", out, tex, fact, facg, in);
			break;
		case MTEX_BLEND_COLOR:
			GPU_link(mat, "mtex_rgb_color", out, tex, fact, facg, in);
			break;
		case MTEX_SOFT_LIGHT:
			GPU_link(mat, "mtex_rgb_soft", out, tex, fact, facg, in);
			break;
		case MTEX_LIN_LIGHT:
			GPU_link(mat, "mtex_rgb_linear", out, tex, fact, facg, in);
			break;
		default:
			GPU_link(mat, "set_rgb_zero", &in);
			break;
	}
}

static void texture_value_blend(
        GPUMaterial *mat, GPUNodeLink *tex, GPUNodeLink *out, GPUNodeLink *fact, GPUNodeLink *facg,
        int blendtype, GPUNodeLink **in)
{
	switch (blendtype) {
		case MTEX_BLEND:
			GPU_link(mat, "mtex_value_blend", out, tex, fact, facg, in);
			break;
		case MTEX_MUL:
			GPU_link(mat, "mtex_value_mul", out, tex, fact, facg, in);
			break;
		case MTEX_SCREEN:
			GPU_link(mat, "mtex_value_screen", out, tex, fact, facg, in);
			break;
		case MTEX_SUB:
			GPU_link(mat, "mtex_value_sub", out, tex, fact, facg, in);
			break;
		case MTEX_ADD:
			GPU_link(mat, "mtex_value_add", out, tex, fact, facg, in);
			break;
		case MTEX_DIV:
			GPU_link(mat, "mtex_value_div", out, tex, fact, facg, in);
			break;
		case MTEX_DIFF:
			GPU_link(mat, "mtex_value_diff", out, tex, fact, facg, in);
			break;
		case MTEX_DARK:
			GPU_link(mat, "mtex_value_dark", out, tex, fact, facg, in);
			break;
		case MTEX_LIGHT:
			GPU_link(mat, "mtex_value_light", out, tex, fact, facg, in);
			break;
		default:
			GPU_link(mat, "set_value_zero", &in);
			break;
	}
}

/* the bump space (object, texture or view), set up again when it changes
 * from one bump texture to the next */
static void gpu_bump_space_init(
        GPUMaterial *mat, GPUShadeInput *shi, int texflag, bool *init_done, int *iBumpSpacePrev,
        GPUNodeLink **vNorg, GPUNodeLink **vNacc, GPUNodeLink **fPrevMagnitude,
        GPUNodeLink **vR1, GPUNodeLink **vR2, GPUNodeLink **fDet)
{
	int iBumpSpace;

	if (!*init_done) {
		/* copy shi->vn to vNorg and vNacc, set magnitude to 1 */
		GPU_link(mat, "mtex_bump_normals_init", shi->vn, vNorg, vNacc, fPrevMagnitude);
		*iBumpSpacePrev = 0;
		*init_done = true;
	}

	/* find current bump space */
	if (texflag & MTEX_BUMP_OBJECTSPACE)
		iBumpSpace = 1;
	else if (texflag & MTEX_BUMP_TEXTURESPACE)
		iBumpSpace = 2;
	else
		iBumpSpace = 4; /* ViewSpace */

	/* re-initialize if bump space changed */
	if (*iBumpSpacePrev != iBumpSpace) {
		GPUNodeLink *surf_pos = GPU_builtin(GPU_VIEW_POSITION);

		if (texflag & MTEX_BUMP_OBJECTSPACE)
			GPU_link(mat, "mtex_bump_init_objspace",
			         surf_pos, *vNorg,
			         GPU_builtin(GPU_VIEW_MATRIX),
			         GPU_builtin(GPU_INVERSE_VIEW_MATRIX),
			         GPU_builtin(GPU_OBJECT_MATRIX),
			         GPU_builtin(GPU_INVERSE_OBJECT_MATRIX),
			         *fPrevMagnitude, *vNacc,
			         fPrevMagnitude, vNacc,
			         vR1, vR2, fDet);
		else if (texflag & MTEX_BUMP_TEXTURESPACE)
			GPU_link(mat, "mtex_bump_init_texturespace",
			         surf_pos, *vNorg,
			         *fPrevMagnitude, *vNacc,
			         fPrevMagnitude, vNacc,
			         vR1, vR2, fDet);
		else
			GPU_link(mat, "mtex_bump_init_viewspace",
			         surf_pos, *vNorg,
			         *fPrevMagnitude, *vNacc,
			         fPrevMagnitude, vNacc,
			         vR1, vR2, fDet);

		*iBumpSpacePrev = iBumpSpace;
	}
}

static void do_material_tex(GPUShadeInput *shi)
{
	Material *ma = shi->mat;
	GPUMaterial *mat = shi->gpumat;
	MTex *mtex;
	Tex *tex;
	GPUNodeLink *texco, *tin, *trgb, *tnor, *tcol, *stencil, *tnorfac;
	GPUNodeLink *texco_norm, *texco_orco, *texco_object;
	GPUNodeLink *texco_global, *texco_uv = NULL;
	GPUNodeLink *newnor, *orn;
	float one = 1.0f;
	int rgbnor, talpha;
	bool init_done = false;
	int iBumpSpacePrev = 0; /* Not necessary, quieting gcc warning. */
	GPUNodeLink *vNorg, *vNacc, *fPrevMagnitude;
	/* kept from one bump texture to the next while the bump space is the same */
	GPUNodeLink *vR1 = NULL, *vR2 = NULL, *fDet = NULL;
	int iFirstTimeNMap = 1;
	bool found_deriv_map = false;

	GPU_link(mat, "set_value", GPU_uniform(&one), &stencil);

	GPU_link(mat, "texco_norm", GPU_builtin(GPU_VIEW_NORMAL), &texco_norm);
	/* BGE-Remix P8.5: in the mesh's texture space also where the drawing
	 * sends raw coordinates (the game engine) */
	GPU_link(mat, "texco_orco_space", GPU_attribute(CD_ORCO, ""), GPU_builtin(GPU_ORCO_MATRIX), &texco_orco);
	GPU_link(mat, "texco_object", GPU_builtin(GPU_INVERSE_VIEW_MATRIX),
		GPU_builtin(GPU_INVERSE_OBJECT_MATRIX),
		GPU_builtin(GPU_VIEW_POSITION), &texco_object);
#if 0
	GPU_link(mat, "texco_tangent", GPU_attribute(CD_TANGENT, ""), &texco_tangent);
#endif
	GPU_link(mat, "texco_global", GPU_builtin(GPU_INVERSE_VIEW_MATRIX),
		GPU_builtin(GPU_VIEW_POSITION), &texco_global);

	orn = texco_norm;

	/* go over texture slots */
	for (int tex_nr = 0; tex_nr < MAX_MTEX; tex_nr++) {
		/* separate tex switching */
		if (ma->septex & (1 << tex_nr)) continue;
		
		if (ma->mtex[tex_nr]) {
			mtex = ma->mtex[tex_nr];
			
			tex = mtex->tex;
			if (tex == NULL) continue;

			/* BGE-Remix P8.5: Blender Internal's procedural textures */
			const bool procedural = gpu_tex_procedural(tex);
			GPUNodeLink *texvec = NULL;

			/* which coords */
			if (mtex->texco == TEXCO_ORCO)
				texco = texco_orco;
			else if (mtex->texco == TEXCO_OBJECT) {
				/* BGE-Remix P8.5: the slot's object, as a render uses it */
				GPUNodeLink *obimat = mtex->object ? gpu_texco_object_link(mat, mtex) : NULL;
				if (obimat) {
					GPU_link(mat, "texco_object", GPU_builtin(GPU_INVERSE_VIEW_MATRIX), obimat,
					         GPU_builtin(GPU_VIEW_POSITION), &texco);
				}
				else {
					texco = texco_object;
				}
			}
			else if (mtex->texco == TEXCO_NORM)
				texco = orn;
			else if (mtex->texco == TEXCO_TANGENT) {
				if (procedural)
					GPU_link(mat, "texco_tangent", GPU_attribute(CD_TANGENT, ""), &texco);
				else
					texco = texco_object;
			}
			else if (procedural && mtex->texco == TEXCO_WINDOW) {
				GPU_link(mat, "proc_texco_window", GPU_builtin(GPU_VIEW_POSITION),
				         GPU_builtin(GPU_CAMERA_TEXCO_FACTORS), &texco);
			}
			else if (mtex->texco == TEXCO_GLOB)
				texco = texco_global;
			else if (mtex->texco == TEXCO_REFL) {
				GPU_link(mat, "texco_refl", shi->vn, shi->view, &shi->ref);
				texco = shi->ref;
			}
			else if (mtex->texco == TEXCO_UV) {
				if (1) { //!(texco_uv && strcmp(mtex->uvname, lastuvname) == 0)) {
					GPU_link(mat, "texco_uv", GPU_attribute(CD_MTFACE, mtex->uvname), &texco_uv);
					/*lastuvname = mtex->uvname;*/ /*UNUSED*/
				}
				texco = texco_uv;
			}
			else
				continue;

			if (procedural) {
				/* the renderer's procedural mapping, in 3D */
				texvec = gpu_procedural_mapping(mat, mtex, texco);
			}
			else {
				/* BGE-Remix P8.5: an image's projection and Flat/Cube/Tube/Sphere
				 * mapping, as a render does them (the defaults draw as before) */
				const bool map_default = (mtex->projx == PROJ_X && mtex->projy == PROJ_Y &&
				                          mtex->projz == PROJ_Z && mtex->mapping == MTEX_FLAT);
				if (tex->type == TEX_IMAGE && !map_default) {
					float proj[3] = {mtex->projx, mtex->projy, mtex->projz};
					float wrap = mtex->mapping;
					float uvmode = (mtex->texco == TEXCO_UV) ? 1.0f : 0.0f;
					GPUNodeLink *cubenor;
					/* the cube side: by the normal in the coordinates' space */
					GPU_link(mat, "direction_transform_m4v3", GPU_builtin(GPU_VIEW_NORMAL),
					         GPU_builtin(GPU_INVERSE_VIEW_MATRIX), &cubenor);
					if (mtex->texco != TEXCO_GLOB) {
						GPUNodeLink *obimat = (mtex->texco == TEXCO_OBJECT && mtex->object) ?
						                      gpu_texco_object_link(mat, mtex) : NULL;
						GPU_link(mat, "direction_transform_m4v3", cubenor,
						         obimat ? obimat : GPU_builtin(GPU_INVERSE_OBJECT_MATRIX), &cubenor);
					}
					GPU_link(mat, "mtex_image_mapping", texco, GPU_uniform(proj), GPU_uniform(&wrap),
					         GPU_uniform(&uvmode), cubenor, &texco);
				}
				/* in case of uv, this would just undo a multiplication in texco_uv */
				else if (mtex->texco != TEXCO_UV)
					GPU_link(mat, "mtex_2d_mapping", texco, &texco);

				if (mtex->size[0] != 1.0f || mtex->size[1] != 1.0f || mtex->size[2] != 1.0f)
					GPU_link(mat, "mtex_mapping_size", texco, GPU_uniform(mtex->size), &texco);

				float ofs[3] = {
					mtex->ofs[0] + 0.5f - 0.5f * mtex->size[0],
					mtex->ofs[1] + 0.5f - 0.5f * mtex->size[1],
					0.0f
				};

				if (ofs[0] != 0.0f || ofs[1] != 0.0f || ofs[2] != 0.0f)
					GPU_link(mat, "mtex_mapping_ofs", texco, GPU_uniform(ofs), &texco);
			}

			talpha = 0;

			if (tex && tex->ima &&
			    ((tex->type == TEX_IMAGE) ||
			     ((tex->type == TEX_ENVMAP) && (mtex->texco == TEXCO_REFL))))
			{
				if (tex->type == TEX_IMAGE) {
					GPU_link(mat, "mtex_image", texco, GPU_image(tex->ima, &tex->iuser, false), &tin, &trgb);
				}
				else {
					GPU_link(mat, "mtex_cube_map_refl",
					         GPU_cube_map(tex->ima, &tex->iuser, false), shi->view, shi->vn,
					         GPU_builtin(GPU_INVERSE_VIEW_MATRIX),
					         GPU_builtin(GPU_VIEW_MATRIX), &tin, &trgb);
				}
				rgbnor = TEX_RGB;

				talpha = ((tex->imaflag & TEX_USEALPHA) && tex->ima && (tex->ima->flag & IMA_IGNORE_ALPHA) == 0);
			}
			else if (procedural) {
				rgbnor = gpu_procedural_eval(mat, tex, texvec, &tin, &trgb, &talpha);
			}
			else {
				continue;
			}

			/* texture output */
			if ((rgbnor & TEX_RGB) && (mtex->texflag & MTEX_RGBTOINT)) {
				GPU_link(mat, "mtex_rgbtoint", trgb, &tin);
				rgbnor -= TEX_RGB;
			}

			if (mtex->texflag & MTEX_NEGATIVE) {
				if (rgbnor & TEX_RGB)
					GPU_link(mat, "mtex_rgb_invert", trgb, &trgb);
				else
					GPU_link(mat, "mtex_value_invert", tin, &tin);
			}

			if (mtex->texflag & MTEX_STENCIL) {
				if (rgbnor & TEX_RGB)
					GPU_link(mat, "mtex_rgb_stencil", stencil, trgb, &stencil, &trgb);
				else
					GPU_link(mat, "mtex_value_stencil", stencil, tin, &stencil, &tin);
			}

			/* mapping */
			if (mtex->mapto & (MAP_COL | MAP_COLSPEC | MAP_COLMIR)) {
				/* stencil maps on the texture control slider, not texture intensity value */
				if ((rgbnor & TEX_RGB) == 0) {
					GPU_link(mat, "set_rgb", GPU_uniform(&mtex->r), &tcol);
				}
				else {
					GPU_link(mat, "set_rgba", trgb, &tcol);

					if (mtex->mapto & MAP_ALPHA)
						GPU_link(mat, "set_value", stencil, &tin);
					else if (talpha)
						GPU_link(mat, "mtex_alpha_from_col", trgb, &tin);
					else
						GPU_link(mat, "set_value_one", &tin);
				}

				if ((tex->type == TEX_IMAGE) ||
				    ((tex->type == TEX_ENVMAP) && (mtex->texco == TEXCO_REFL)))
				{
					if (GPU_material_do_color_management(mat)) {
						GPU_link(mat, "srgb_to_linearrgb", tcol, &tcol);
					}
				}
				
				if (mtex->mapto & MAP_COL) {
					GPUNodeLink *colfac;

					if (mtex->colfac == 1.0f) colfac = stencil;
					else GPU_link(mat, "math_multiply", GPU_uniform(&mtex->colfac), stencil, &colfac);

					texture_rgb_blend(mat, tcol, shi->rgb, tin, colfac, mtex->blendtype, &shi->rgb);
				}
				
				if (!(mat->scene->gm.flag & GAME_GLSL_NO_EXTRA_TEX) && (mtex->mapto & MAP_COLSPEC)) {
					GPUNodeLink *colspecfac;

					if (mtex->colspecfac == 1.0f) colspecfac = stencil;
					else GPU_link(mat, "math_multiply", GPU_uniform(&mtex->colspecfac), stencil, &colspecfac);

					texture_rgb_blend(mat, tcol, shi->specrgb, tin, colspecfac, mtex->blendtype, &shi->specrgb);
				}

				if (mtex->mapto & MAP_COLMIR) {
					GPUNodeLink *colmirfac;

					if (mtex->mirrfac == 1.0f) colmirfac = stencil;
					else GPU_link(mat, "math_multiply", GPU_uniform(&mtex->mirrfac), stencil, &colmirfac);

					/* exception for envmap only */
					if (tex->type == TEX_ENVMAP && mtex->blendtype == MTEX_BLEND) {
						GPU_link(mat, "mtex_mirror", tcol, shi->refcol, tin, colmirfac, &shi->refcol);
					}
					else
						texture_rgb_blend(mat, tcol, shi->mir, tin, colmirfac, mtex->blendtype, &shi->mir);
				}
			}

			if (!(mat->scene->gm.flag & GAME_GLSL_NO_EXTRA_TEX) && (mtex->mapto & MAP_NORM)) {
				if (tex->type == TEX_IMAGE) {
					found_deriv_map = tex->imaflag & TEX_DERIVATIVEMAP;

					if (tex->imaflag & TEX_NORMALMAP) {
						/* normalmap image */
						GPU_link(mat, "mtex_normal", texco, GPU_image(tex->ima, &tex->iuser, true), &tnor);
						
						if (mtex->norfac < 0.0f)
							GPU_link(mat, "mtex_negate_texnormal", tnor, &tnor);

						if (mtex->normapspace == MTEX_NSPACE_TANGENT) {
							if (iFirstTimeNMap != 0) {
								// use unnormalized normal (this is how we bake it - closer to gamedev)
								GPUNodeLink *vNegNorm;
								GPU_link(mat, "vec_math_negate",
								         GPU_builtin(GPU_VIEW_NORMAL), &vNegNorm);
								GPU_link(mat, "mtex_nspace_tangent",
								         GPU_attribute(CD_TANGENT, ""), vNegNorm, tnor, &newnor);
								iFirstTimeNMap = 0;
							}
							else { /* otherwise use accumulated perturbations */
								GPU_link(mat, "mtex_nspace_tangent",
								         GPU_attribute(CD_TANGENT, ""), shi->vn, tnor, &newnor);
							}
						}
						else if (mtex->normapspace == MTEX_NSPACE_OBJECT) {
							/* transform normal by object then view matrix */
							GPU_link(mat, "mtex_nspace_object", tnor, &newnor);
						}
						else if (mtex->normapspace == MTEX_NSPACE_WORLD) {
							/* transform normal by view matrix */
							GPU_link(mat, "mtex_nspace_world", GPU_builtin(GPU_VIEW_MATRIX), tnor, &newnor);
						}
						else {
							/* no transform, normal in camera space */
							newnor = tnor;
						}
						
						float norfac = min_ff(fabsf(mtex->norfac), 1.0f);
						
						if (norfac == 1.0f && !GPU_link_changed(stencil)) {
							shi->vn = newnor;
						}
						else {
							tnorfac = GPU_uniform(&norfac);
	
							if (GPU_link_changed(stencil))
								GPU_link(mat, "math_multiply", tnorfac, stencil, &tnorfac);
	
							GPU_link(mat, "mtex_blend_normal", tnorfac, shi->vn, newnor, &shi->vn);
						}
						
					}
					else if (found_deriv_map ||
					         (mtex->texflag & (MTEX_3TAP_BUMP | MTEX_5TAP_BUMP | MTEX_BICUBIC_BUMP)))
					{
						/* ntap bumpmap image */
						float ima_x, ima_y;

						float imag_tspace_dimension_x = 1024.0f; /* only used for texture space variant */
						float aspect = 1.0f;
						
						GPUNodeLink *dBs, *dBt;

						float hScale = 0.1f; /* compatibility adjustment factor for all bumpspace types */
						if (mtex->texflag & MTEX_BUMP_TEXTURESPACE)
							hScale = 13.0f; /* factor for scaling texspace bumps */
						else if (found_deriv_map)
							hScale = 1.0f;

						/* resolve texture resolution */
						if ((mtex->texflag & MTEX_BUMP_TEXTURESPACE) || found_deriv_map) {
							ImBuf *ibuf = BKE_image_acquire_ibuf(tex->ima, &tex->iuser, NULL);
							ima_x = 512.0f; ima_y = 512.0f; /* prevent calling textureSize, glsl 1.3 only */
							if (ibuf) {
								ima_x = ibuf->x;
								ima_y = ibuf->y;
								aspect = (float)ima_y / ima_x;
							}
							BKE_image_release_ibuf(tex->ima, ibuf, NULL);
						}

						/* The negate on norfac is done because the
						 * normal in the renderer points inward which corresponds
						 * to inverting the bump map. Should this ever change
						 * this negate must be removed. */
						float norfac = -hScale * mtex->norfac;
						if (found_deriv_map) {
							float fVirtDim = sqrtf(fabsf(ima_x * mtex->size[0] * ima_y * mtex->size[1]));
							norfac /= MAX2(fVirtDim, FLT_EPSILON);
						}

						tnorfac = GPU_uniform(&norfac);

						if (found_deriv_map)
							GPU_link(mat, "math_multiply", tnorfac, GPU_builtin(GPU_AUTO_BUMPSCALE), &tnorfac);
						
						if (GPU_link_changed(stencil))
							GPU_link(mat, "math_multiply", tnorfac, stencil, &tnorfac);
						
						gpu_bump_space_init(mat, shi, mtex->texflag, &init_done, &iBumpSpacePrev,
						                    &vNorg, &vNacc, &fPrevMagnitude, &vR1, &vR2, &fDet);
						
						
						if (found_deriv_map) {
							GPU_link(mat, "mtex_bump_deriv",
							         texco, GPU_image(tex->ima, &tex->iuser, true),
							         GPU_uniform(&ima_x), GPU_uniform(&ima_y), tnorfac,
							         &dBs, &dBt);
						}
						else if (mtex->texflag & MTEX_3TAP_BUMP)
							GPU_link(mat, "mtex_bump_tap3",
							         texco, GPU_image(tex->ima, &tex->iuser, true), tnorfac,
							         &dBs, &dBt);
						else if (mtex->texflag & MTEX_5TAP_BUMP)
							GPU_link(mat, "mtex_bump_tap5",
							         texco, GPU_image(tex->ima, &tex->iuser, true), tnorfac,
							         &dBs, &dBt);
						else if (mtex->texflag & MTEX_BICUBIC_BUMP) {
							if (GPU_bicubic_bump_support()) {
								GPU_link(mat, "mtex_bump_bicubic",
								         texco, GPU_image(tex->ima, &tex->iuser, true), tnorfac,
								         &dBs, &dBt);
							}
							else {
								GPU_link(mat, "mtex_bump_tap5",
								         texco, GPU_image(tex->ima, &tex->iuser, true), tnorfac,
								         &dBs, &dBt);
							}
						}
						
						
						if (mtex->texflag & MTEX_BUMP_TEXTURESPACE) {
							float imag_tspace_dimension_y = aspect * imag_tspace_dimension_x;
							GPU_link(mat, "mtex_bump_apply_texspace",
							         fDet, dBs, dBt, vR1, vR2,
							         GPU_image(tex->ima, &tex->iuser, true), texco,
							         GPU_uniform(&imag_tspace_dimension_x),
							         GPU_uniform(&imag_tspace_dimension_y), vNacc,
							         &vNacc, &shi->vn);
						}
						else
							GPU_link(mat, "mtex_bump_apply",
							         fDet, dBs, dBt, vR1, vR2, vNacc,
							         &vNacc, &shi->vn);
						
					}
				}
				else if (procedural) {
					/* BGE-Remix P8.5: a procedural bumps the surface with three
					 * samples per pixel, whatever bump method is chosen; texture
					 * space is drawn as object space */
					GPUNodeLink *dBs, *dBt;
					int texflag = mtex->texflag;
					/* the renderer's factor: 0.1, and 1 in texture space */
					float norfac = -mtex->norfac * ((texflag & MTEX_BUMP_TEXTURESPACE) ? 1.0f : 0.1f);

					if (texflag & MTEX_BUMP_TEXTURESPACE) {
						texflag = (texflag & ~MTEX_BUMP_TEXTURESPACE) | MTEX_BUMP_OBJECTSPACE;
					}
					tnorfac = GPU_uniform(&norfac);
					if (GPU_link_changed(stencil))
						GPU_link(mat, "math_multiply", tnorfac, stencil, &tnorfac);

					gpu_bump_space_init(mat, shi, texflag, &init_done, &iBumpSpacePrev,
					                    &vNorg, &vNacc, &fPrevMagnitude, &vR1, &vR2, &fDet);
					gpu_procedural_bump(mat, mtex, tex, texvec, tnorfac, &dBs, &dBt);
					GPU_link(mat, "mtex_bump_apply", fDet, dBs, dBt, vR1, vR2, vNacc, &vNacc, &shi->vn);
				}
				
				GPU_link(mat, "vec_math_negate", shi->vn, &orn);
			}

			if ((mtex->mapto & MAP_VARS)) {
				if (rgbnor & TEX_RGB) {
					if (talpha)
						GPU_link(mat, "mtex_alpha_from_col", trgb, &tin);
					else
						GPU_link(mat, "mtex_rgbtoint", trgb, &tin);
				}

				if (!(mat->scene->gm.flag & GAME_GLSL_NO_EXTRA_TEX) && mtex->mapto & MAP_REF) {
					GPUNodeLink *difffac;

					if (mtex->difffac == 1.0f) difffac = stencil;
					else GPU_link(mat, "math_multiply", GPU_uniform(&mtex->difffac), stencil, &difffac);

					texture_value_blend(
					        mat, GPU_uniform(&mtex->def_var), shi->refl, tin, difffac,
					        mtex->blendtype, &shi->refl);
					GPU_link(mat, "mtex_value_clamp_positive", shi->refl, &shi->refl);
				}
				if (!(mat->scene->gm.flag & GAME_GLSL_NO_EXTRA_TEX) && mtex->mapto & MAP_SPEC) {
					GPUNodeLink *specfac;

					if (mtex->specfac == 1.0f) specfac = stencil;
					else GPU_link(mat, "math_multiply", GPU_uniform(&mtex->specfac), stencil, &specfac);

					texture_value_blend(
					        mat, GPU_uniform(&mtex->def_var), shi->spec, tin, specfac,
					        mtex->blendtype, &shi->spec);
					GPU_link(mat, "mtex_value_clamp_positive", shi->spec, &shi->spec);
				}
				if (!(mat->scene->gm.flag & GAME_GLSL_NO_EXTRA_TEX) && mtex->mapto & MAP_EMIT) {
					GPUNodeLink *emitfac;

					if (mtex->emitfac == 1.0f) emitfac = stencil;
					else GPU_link(mat, "math_multiply", GPU_uniform(&mtex->emitfac), stencil, &emitfac);

					texture_value_blend(
					        mat, GPU_uniform(&mtex->def_var), shi->emit, tin, emitfac,
					        mtex->blendtype, &shi->emit);
					GPU_link(mat, "mtex_value_clamp_positive", shi->emit, &shi->emit);
				}
				if (!(mat->scene->gm.flag & GAME_GLSL_NO_EXTRA_TEX) && mtex->mapto & MAP_HAR) {
					GPUNodeLink *hardfac;

					if (mtex->hardfac == 1.0f) hardfac = stencil;
					else GPU_link(mat, "math_multiply", GPU_uniform(&mtex->hardfac), stencil, &hardfac);

					if (shi->physical) {
						/* BGE-Remix P8.7: Hardness is the roughness in a physical material */
						texture_value_blend(
						        mat, GPU_uniform(&mtex->def_var), shi->roughness, tin, hardfac,
						        mtex->blendtype, &shi->roughness);
						GPU_link(mat, "mtex_value_clamp", shi->roughness, &shi->roughness);
					}
					else {
						GPU_link(mat, "mtex_har_divide", shi->har, &shi->har);
						texture_value_blend(
						        mat, GPU_uniform(&mtex->def_var), shi->har, tin, hardfac,
						        mtex->blendtype, &shi->har);
						GPU_link(mat, "mtex_har_multiply_clamp", shi->har, &shi->har);
					}
				}
				if (shi->physical && (mtex->mapto & MAP_RAYMIRR)) {
					/* BGE-Remix P8.7: Ray Mirror is the metallic value in a physical material */
					GPUNodeLink *metalfac;

					if (mtex->raymirrfac == 1.0f) metalfac = stencil;
					else GPU_link(mat, "math_multiply", GPU_uniform(&mtex->raymirrfac), stencil, &metalfac);

					texture_value_blend(
					        mat, GPU_uniform(&mtex->def_var), shi->metallic, tin, metalfac,
					        mtex->blendtype, &shi->metallic);
					GPU_link(mat, "mtex_value_clamp", shi->metallic, &shi->metallic);
				}
				if (mtex->mapto & MAP_ALPHA) {
					GPUNodeLink *alphafac;

					if (mtex->alphafac == 1.0f) alphafac = stencil;
					else GPU_link(mat, "math_multiply", GPU_uniform(&mtex->alphafac), stencil, &alphafac);

					texture_value_blend(
					        mat, GPU_uniform(&mtex->def_var), shi->alpha, tin, alphafac,
					        mtex->blendtype, &shi->alpha);
					GPU_link(mat, "mtex_value_clamp", shi->alpha, &shi->alpha);
				}
				if (!(mat->scene->gm.flag & GAME_GLSL_NO_EXTRA_TEX) && mtex->mapto & MAP_AMB) {
					GPUNodeLink *ambfac;

					if (mtex->ambfac == 1.0f) ambfac = stencil;
					else GPU_link(mat, "math_multiply", GPU_uniform(&mtex->ambfac), stencil, &ambfac);

					texture_value_blend(
					        mat, GPU_uniform(&mtex->def_var), shi->amb, tin, ambfac,
					        mtex->blendtype, &shi->amb);
					GPU_link(mat, "mtex_value_clamp", shi->amb, &shi->amb);
				}
			}
		}
	}
}

void GPU_shadeinput_set(GPUMaterial *mat, Material *ma, GPUShadeInput *shi)
{
	float one = 1.0f;

	memset(shi, 0, sizeof(*shi));

	shi->gpumat = mat;
	shi->mat = ma;

	GPU_link(mat, "set_rgb", GPU_dynamic_uniform(&ma->r, GPU_DYNAMIC_MAT_DIFFRGB, ma), &shi->rgb);
	GPU_link(mat, "set_rgb", GPU_dynamic_uniform(&ma->specr, GPU_DYNAMIC_MAT_SPECRGB, ma), &shi->specrgb);
	GPU_link(mat, "set_rgb", GPU_dynamic_uniform(&ma->mirr, GPU_DYNAMIC_MAT_MIR, ma), &shi->mir);
	GPU_link(mat, "set_rgba_zero", &shi->refcol);
	GPU_link(mat, "shade_norm", GPU_builtin(GPU_VIEW_NORMAL), &shi->vn);

	if (mat->alpha)
		GPU_link(mat, "set_value", GPU_dynamic_uniform(&ma->alpha, GPU_DYNAMIC_MAT_ALPHA, ma), &shi->alpha);
	else
		GPU_link(mat, "set_value", GPU_uniform(&one), &shi->alpha);

	GPU_link(mat, "set_value", GPU_dynamic_uniform(&ma->ref, GPU_DYNAMIC_MAT_REF, ma), &shi->refl);
	GPU_link(mat, "set_value", GPU_dynamic_uniform(&ma->spec, GPU_DYNAMIC_MAT_SPEC, ma), &shi->spec);
	GPU_link(mat, "set_value", GPU_dynamic_uniform(&ma->emit, GPU_DYNAMIC_MAT_EMIT, ma), &shi->emit);
	GPU_link(mat, "set_value", GPU_dynamic_uniform((float *)&ma->har, GPU_DYNAMIC_MAT_HARD, ma), &shi->har);
	GPU_link(mat, "set_value", GPU_dynamic_uniform(&ma->amb, GPU_DYNAMIC_MAT_AMB, ma), &shi->amb);
	GPU_link(mat, "set_value", GPU_uniform(&ma->spectra), &shi->spectra);
	GPU_link(mat, "shade_view", GPU_builtin(GPU_VIEW_POSITION), &shi->view);
	GPU_link(mat, "vcol_attribute", GPU_attribute(CD_MCOL, ""), &shi->vcol);
	if (GPU_material_do_color_management(mat))
		GPU_link(mat, "srgb_to_linearrgb", shi->vcol, &shi->vcol);
	GPU_link(mat, "texco_refl", shi->vn, shi->view, &shi->ref);

	/* BGE-Remix P8.7: the physical material's values */
	shi->physical = mat->type != GPU_MATERIAL_TYPE_WORLD && BKE_material_is_physical(ma);
	if (shi->physical) {
		MaterialPhysical ph;
		BKE_material_physical_get(ma, &ph);
		GPU_link(mat, "set_value", GPU_uniform(&ph.metallic), &shi->metallic);
		GPU_link(mat, "set_value", GPU_uniform(&ph.roughness), &shi->roughness);
		GPU_link(mat, "set_value", GPU_uniform(&ph.specular), &shi->spec);
		GPU_link(mat, "set_value", GPU_uniform(&ph.emission_strength), &shi->emit);
		GPU_link(mat, "set_value", GPU_uniform(&one), &shi->refl);
	}
}

void GPU_mist_update_enable(short enable)
{
	GPUWorld.mistenabled = (float)enable;
}

void GPU_mist_update_values(int type, float start, float dist, float inten, float color[3])
{
	GPUWorld.mistype = (float)type;
	GPUWorld.miststart = start;
	GPUWorld.mistdistance = dist;
	GPUWorld.mistintensity = inten;
	copy_v3_v3(GPUWorld.mistcol, color);
	GPUWorld.mistcol[3] = 1.0f;
}

void GPU_horizon_update_color(float color[3])
{
	copy_v3_v3(GPUWorld.horicol, color);
}

void GPU_ambient_update_color(float color[3])
{
	copy_v3_v3(GPUWorld.ambcol, color);
	GPUWorld.ambcol[3] = 1.0f;
}

void GPU_zenith_update_color(float color[3])
{
	copy_v3_v3(GPUWorld.zencol, color);
}

/* BGE-Remix P8.7: a physical material's alpha counts when it is glass or
 * drawn with transparency */
static bool gpu_material_physical_alpha(Material *ma)
{
	MaterialPhysical ph;
	if (!BKE_material_is_physical(ma)) {
		return false;
	}
	BKE_material_physical_get(ma, &ph);
	return ph.transmission > 0.0f || ma->game.alpha_blend != GEMAT_SOLID;
}

/* BGE-Remix P8.7: a physical material lit by the lamps and the world */
static void gpu_shade_physical(GPUShadeInput *shi, GPUShadeResult *shr)
{
	GPUMaterial *mat = shi->gpumat;
	Material *ma = shi->mat;
	World *world = mat->scene->world;
	MaterialPhysical ph;
	GPUNodeLink *emission;

	BKE_material_physical_get(ma, &ph);
	GPU_link(mat, "pbr_prepare", shi->rgb, shi->metallic, shi->spec, GPU_uniform(&ph.transmission),
	         &shi->pbr_diff, &shi->pbr_f0);
	/* the lamps' Only Shadow darkening uses these */
	shi->rgb = shi->pbr_diff;
	shi->specrgb = shi->pbr_f0;

	GPU_link(mat, "set_rgb_zero", &shr->diff);
	GPU_link(mat, "set_rgb_zero", &shr->spec);
	if ((mat->scene->gm.flag & GAME_GLSL_NO_LIGHTS) || (ma->mode & MA_SHLESS)) {
		GPU_link(mat, "set_rgb", shi->pbr_diff, &shr->diff);
	}
	else {
		material_lights(shi, shr);
		if (world) {
			GPU_link(mat, "shade_pbr_ambient", shi->vn, shi->view, shi->pbr_diff, shi->pbr_f0, shi->roughness,
			         shi->amb, GPU_dynamic_uniform(GPUWorld.ambcol, GPU_DYNAMIC_AMBIENT_COLOR, NULL),
			         GPU_dynamic_uniform(GPUWorld.horicol, GPU_DYNAMIC_HORIZON_COLOR, NULL),
			         GPU_dynamic_uniform(GPUWorld.zencol, GPU_DYNAMIC_ZENITH_COLOR, NULL),
			         GPU_builtin(GPU_INVERSE_VIEW_MATRIX), shr->diff, &shr->diff);
		}
	}
	/* emission: its colour times its (textured) strength */
	GPU_link(mat, "set_rgb", GPU_uniform(ph.emission), &emission);
	GPU_link(mat, "shade_maddf", shr->diff, shi->emit, emission, &shr->diff);

	GPU_link(mat, "shade_add", shr->diff, shr->spec, &shr->combined);
	if (ph.transmission > 0.0f) {
		GPU_link(mat, "shade_pbr_alpha", shi->vn, shi->view, shi->pbr_f0, GPU_uniform(&ph.transmission),
		         shi->alpha, &shr->alpha);
	}
	else {
		GPU_link(mat, "set_value", shi->alpha, &shr->alpha);
	}
}

void GPU_shaderesult_set(GPUShadeInput *shi, GPUShadeResult *shr)
{
	GPUMaterial *mat = shi->gpumat;
	GPUNodeLink *emit, *ulinfac, *ulogfac, *mistfac;
	Material *ma = shi->mat;
	World *world = mat->scene->world;
	float linfac, logfac;

	memset(shr, 0, sizeof(*shr));

	if (ma->mode & MA_VERTEXCOLP)
		shi->rgb = shi->vcol;

	/* BGE-Remix P8: "Face Textures" as in Multitexture: the face's image is
	 * the first texture (UV, mixed fully into the colour, its alpha the
	 * material's alpha with "Face Textures Alpha") */
	if ((ma->mode & MA_FACETEXTURE) && mat->type == GPU_MATERIAL_TYPE_MESH) {
		GPUNodeLink *uv, *value, *col, *texalpha, *on;

		/* a node's output, so it can feed two nodes (a uniform's link is
		 * used up by the node it feeds: reusing it read freed memory, the
		 * crash of mt_features in CI) */
		GPU_link(mat, "set_value", GPU_dynamic_uniform(&mat->face_image_on, GPU_DYNAMIC_MAT_FACEIMA, mat), &on);
		mat->use_face_image = true;
		GPU_link(mat, "texco_uv", GPU_attribute(CD_MTFACE, ""), &uv);
		GPU_link(mat, "mtex_image", uv,
		         GPU_dynamic_texture(gpu_face_image_blank(), GPU_DYNAMIC_SAMPLER_2DIMAGE, mat), &value, &col);
		if (GPU_material_do_color_management(mat)) {
			GPU_link(mat, "srgb_to_linearrgb", col, &col);
		}
		GPU_link(mat, "mtex_face_image", col, on, shi->rgb, &shi->rgb, &texalpha);
		if (ma->mode & MA_FACETEXTURE_ALPHA) {
			GPU_link(mat, "mtex_face_alpha", texalpha, on, shi->alpha, &shi->alpha);
		}
	}

	do_material_tex(shi);

	if (shi->physical) {
		/* BGE-Remix P8.7 */
		gpu_shade_physical(shi, shr);
	}
	else if ((mat->scene->gm.flag & GAME_GLSL_NO_LIGHTS) || (ma->mode & MA_SHLESS)) {
		GPU_link(mat, "set_rgb", shi->rgb, &shr->diff);
		GPU_link(mat, "set_rgb_zero", &shr->spec);
		GPU_link(mat, "set_value", shi->alpha, &shr->alpha);
		shr->combined = shr->diff;
	}
	else {
		if (GPU_link_changed(shi->emit) || ma->emit != 0.0f) {
			if ((ma->mode & (MA_VERTEXCOL | MA_VERTEXCOLP)) == MA_VERTEXCOL) {
				GPU_link(mat, "shade_add", shi->emit, shi->vcol, &emit);
				GPU_link(mat, "shade_mul", emit, shi->rgb, &shr->diff);
			}
			else
				GPU_link(mat, "shade_mul_value", shi->emit, shi->rgb, &shr->diff);
		}
		else
			GPU_link(mat, "set_rgb_zero", &shr->diff);

		GPU_link(mat, "set_rgb_zero", &shr->spec);

		material_lights(shi, shr);

		shr->combined = shr->diff;

		GPU_link(mat, "set_value", shi->alpha, &shr->alpha);

		if (world) {
			/* exposure correction */
			if (world->exp != 0.0f || world->range != 1.0f) {
				linfac = 1.0f + powf((2.0f * world->exp + 0.5f), -10);
				logfac = logf((linfac - 1.0f) / linfac) / world->range;

				GPU_link(mat, "set_value", GPU_uniform(&linfac), &ulinfac);
				GPU_link(mat, "set_value", GPU_uniform(&logfac), &ulogfac);

				GPU_link(mat, "shade_exposure_correct", shr->combined,
					ulinfac, ulogfac, &shr->combined);
				GPU_link(mat, "shade_exposure_correct", shr->spec,
					ulinfac, ulogfac, &shr->spec);
			}

			/* environment lighting */
			if (!(mat->scene->gm.flag & GAME_GLSL_NO_ENV_LIGHTING) &&
			    (world->mode & WO_ENV_LIGHT) &&
			    (mat->scene->r.mode & R_SHADOW) &&
			    !BKE_scene_use_new_shading_nodes(mat->scene))
			{
				if ((world->ao_env_energy != 0.0f) && (GPU_link_changed(shi->amb) || ma->amb != 0.0f) &&
				    (GPU_link_changed(shi->refl) || ma->ref != 0.0f))
				{
					if (world->aocolor != WO_AOPLAIN) {
						if (!(is_zero_v3(&world->horr) & is_zero_v3(&world->zenr))) {
							GPUNodeLink *fcol, *f;
							GPU_link(mat, "math_multiply", shi->amb, shi->refl, &f);
							GPU_link(mat, "math_multiply", f, GPU_uniform(&world->ao_env_energy), &f);
							GPU_link(mat, "shade_mul_value", f, shi->rgb, &fcol);
							GPU_link(mat, "env_apply", shr->combined,
							         GPU_dynamic_uniform(GPUWorld.horicol, GPU_DYNAMIC_HORIZON_COLOR, NULL),
							         GPU_dynamic_uniform(GPUWorld.zencol, GPU_DYNAMIC_ZENITH_COLOR, NULL), fcol,
							         GPU_builtin(GPU_VIEW_MATRIX), shi->vn, &shr->combined);
						}
					}
					else {
						GPUNodeLink *f;
						GPU_link(mat, "math_multiply", shi->amb, shi->refl, &f);
						GPU_link(mat, "math_multiply", f, GPU_uniform(&world->ao_env_energy), &f);
						GPU_link(mat, "shade_maddf", shr->combined, f, shi->rgb, &shr->combined);
					}
				}
			}

			/* ambient color */
			if (GPU_link_changed(shi->amb) || ma->amb != 0.0f) {
				GPU_link(mat, "shade_maddf", shr->combined, GPU_uniform(&ma->amb),
				         GPU_dynamic_uniform(GPUWorld.ambcol, GPU_DYNAMIC_AMBIENT_COLOR, NULL),
				         &shr->combined);
			}
		}

		if (ma->mode & MA_TRANSP && (ma->mode & (MA_ZTRANSP | MA_RAYTRANSP))) {
			if (GPU_link_changed(shi->spectra) || ma->spectra != 0.0f) {
				GPU_link(mat, "alpha_spec_correction", shr->spec, shi->spectra,
				         shi->alpha, &shr->alpha);
			}
		}

		if (ma->mode & MA_RAMP_COL) ramp_diffuse_result(shi, &shr->combined);
		if (ma->mode & MA_RAMP_SPEC) ramp_spec_result(shi, &shr->spec);

		if (GPU_link_changed(shi->refcol))
			GPU_link(mat, "shade_add_mirror", shi->mir, shi->refcol, shr->combined, &shr->combined);

		if (GPU_link_changed(shi->spec) || ma->spec != 0.0f)
			GPU_link(mat, "shade_add", shr->combined, shr->spec, &shr->combined);
	}

	GPU_link(mat, "mtex_alpha_to_col", shr->combined, shr->alpha, &shr->combined);

	if (ma->shade_flag & MA_OBCOLOR)
		GPU_link(mat, "shade_obcolor", shr->combined, GPU_builtin(GPU_OBCOLOR), &shr->combined);

	if (!(ma->mode & MA_NOMIST)) {
		GPU_link(mat, "shade_mist_factor", GPU_builtin(GPU_VIEW_POSITION),
		         GPU_dynamic_uniform(&GPUWorld.mistenabled, GPU_DYNAMIC_MIST_ENABLE, NULL),
		         GPU_dynamic_uniform(&GPUWorld.miststart, GPU_DYNAMIC_MIST_START, NULL),
		         GPU_dynamic_uniform(&GPUWorld.mistdistance, GPU_DYNAMIC_MIST_DISTANCE, NULL),
		         GPU_dynamic_uniform(&GPUWorld.mistype, GPU_DYNAMIC_MIST_TYPE, NULL),
		         GPU_dynamic_uniform(&GPUWorld.mistintensity, GPU_DYNAMIC_MIST_INTENSITY, NULL), &mistfac);

		GPU_link(mat, "mix_blend", mistfac, shr->combined,
		         GPU_dynamic_uniform(GPUWorld.mistcol, GPU_DYNAMIC_MIST_COLOR, NULL), &shr->combined);
	}

	if (!mat->alpha) {
		if (world && (GPU_link_changed(shr->alpha) || ma->alpha != 1.0f))
			GPU_link(mat, "shade_world_mix", GPU_dynamic_uniform(GPUWorld.horicol, GPU_DYNAMIC_HORIZON_COLOR, NULL),
			         shr->combined, &shr->combined);

		GPU_link(mat, "shade_alpha_opaque", shr->combined, &shr->combined);
	}

	if (ma->shade_flag & MA_OBCOLOR) {
		mat->obcolalpha = 1;
		GPU_link(mat, "shade_alpha_obcolor", shr->combined, GPU_builtin(GPU_OBCOLOR), &shr->combined);
	}
}

static GPUNodeLink *GPU_blender_material(GPUMaterial *mat, Material *ma)
{
	GPUShadeInput shi;
	GPUShadeResult shr;

	GPU_shadeinput_set(mat, ma, &shi);
	GPU_shaderesult_set(&shi, &shr);

	return shr.combined;
}

static GPUNodeLink *gpu_material_diffuse_bsdf(GPUMaterial *mat, Material *ma)
{
	static float roughness = 0.0f;
	GPUNodeLink *outlink;

	GPU_link(mat, "node_bsdf_diffuse",
	         GPU_uniform(&ma->r), GPU_uniform(&roughness), GPU_builtin(GPU_VIEW_NORMAL), &outlink);

	return outlink;
}

static GPUNodeLink *gpu_material_preview_matcap(GPUMaterial *mat, Material *ma)
{
	GPUNodeLink *outlink;
	
	/* some explanations here:
	 * matcap normal holds the normal remapped to the 0.0 - 1.0 range. To take advantage of flat shading, we abuse
	 * the built in secondary color of opengl. Color is just the regular color, which should include mask value too.
	 * This also needs flat shading so we use the primary opengl color built-in */
	GPU_link(mat, "material_preview_matcap", GPU_uniform(&ma->r), GPU_image_preview(ma->preview),
	         GPU_opengl_builtin(GPU_MATCAP_NORMAL), GPU_opengl_builtin(GPU_COLOR), &outlink);
	
	return outlink;
}

/* new solid draw mode with glsl matcaps */
GPUMaterial *GPU_material_matcap(Scene *scene, Material *ma, bool use_opensubdiv)
{
	GPUMaterial *mat;
	GPUNodeLink *outlink;
	LinkData *link;
	
	for (link = ma->gpumaterial.first; link; link = link->next) {
		GPUMaterial *current_material = (GPUMaterial *)link->data;
		if (current_material->scene == scene &&
		    current_material->is_opensubdiv == use_opensubdiv)
		{
			return current_material;
		}
	}
	
	/* allocate material */
	mat = GPU_material_construct_begin(ma);
	mat->scene = scene;
	mat->type = GPU_MATERIAL_TYPE_MESH;
	mat->is_opensubdiv = use_opensubdiv;

	if (ma->preview && ma->preview->rect[0]) {
		outlink = gpu_material_preview_matcap(mat, ma);
	}
	else {
		outlink = gpu_material_diffuse_bsdf(mat, ma);
	}
		
	GPU_material_output_link(mat, outlink);

	gpu_material_construct_end(mat, "matcap_pass");
	
	/* note that even if building the shader fails in some way, we still keep
	 * it to avoid trying to compile again and again, and simple do not use
	 * the actual shader on drawing */
	
	link = MEM_callocN(sizeof(LinkData), "GPUMaterialLink");
	link->data = mat;
	BLI_addtail(&ma->gpumaterial, link);
	
	return mat;
}

static void do_world_tex(GPUShadeInput *shi, struct World *wo, GPUNodeLink **hor, GPUNodeLink **zen, GPUNodeLink **blend)
{
	GPUMaterial *mat = shi->gpumat;
	GPUNodeLink *texco, *tin, *trgb, *stencil, *tcol, *zenfac;
	MTex *mtex;
	Tex *tex;
	float ofs[3], zero = 0.0f;
	int tex_nr, rgbnor;

	GPU_link(mat, "set_value_one", &stencil);
	/* go over texture slots */
	for (tex_nr = 0; tex_nr < MAX_MTEX; tex_nr++) {
		if (wo->mtex[tex_nr]) {
			mtex = wo->mtex[tex_nr];
			tex = mtex->tex;
			if (tex == NULL || !tex->ima || (tex->type != TEX_IMAGE && tex->type != TEX_ENVMAP))
				continue;
			/* which coords */
			if (mtex->texco == TEXCO_VIEW || mtex->texco == TEXCO_GLOB) {
				if (tex->type == TEX_IMAGE)
					texco = GPU_builtin(GPU_VIEW_POSITION);
				else if (tex->type == TEX_ENVMAP)
					GPU_link(mat, "background_transform_to_world", GPU_builtin(GPU_VIEW_POSITION), &texco);
			}
			else if (mtex->texco == TEXCO_EQUIRECTMAP || mtex->texco == TEXCO_ANGMAP) {
				if ((tex->type == TEX_IMAGE && wo->skytype & WO_SKYREAL) || tex->type == TEX_ENVMAP)
					GPU_link(mat, "background_transform_to_world", GPU_builtin(GPU_VIEW_POSITION), &texco);
				else
					texco = GPU_builtin(GPU_VIEW_POSITION);
			}
			else
				continue;
			GPU_link(mat, "texco_norm", texco, &texco);
			if (tex->type == TEX_IMAGE && !(wo->skytype & WO_SKYREAL)) {
				GPU_link(mat, "mtex_2d_mapping", texco, &texco);
			}
			if (mtex->size[0] != 1.0f || mtex->size[1] != 1.0f || mtex->size[2] != 1.0f) {
				float size[3] = { mtex->size[0], mtex->size[1], mtex->size[2] };
				if (tex->type == TEX_ENVMAP) {
					size[1] = mtex->size[2];
					size[2] = mtex->size[1];
				}
				GPU_link(mat, "mtex_mapping_size", texco, GPU_uniform(size), &texco);
			}
			ofs[0] = mtex->ofs[0] + 0.5f - 0.5f * mtex->size[0];
			if (tex->type == TEX_ENVMAP) {
				ofs[1] = -mtex->ofs[2] + 0.5f - 0.5f * mtex->size[2];
				ofs[2] = mtex->ofs[1] + 0.5f - 0.5f * mtex->size[1];
			}
			else {
				ofs[1] = mtex->ofs[1] + 0.5f - 0.5f * mtex->size[1];
				ofs[2] = 0.0;
			}
			if (ofs[0] != 0.0f || ofs[1] != 0.0f || ofs[2] != 0.0f)
				GPU_link(mat, "mtex_mapping_ofs", texco, GPU_uniform(ofs), &texco);
			if (mtex->texco == TEXCO_EQUIRECTMAP) {
				GPU_link(mat, "node_tex_environment_equirectangular", texco, GPU_image(tex->ima, &tex->iuser, false), &trgb);
			}
			else if (mtex->texco == TEXCO_ANGMAP) {
				GPU_link(mat, "node_tex_environment_mirror_ball", texco, GPU_image(tex->ima, &tex->iuser, false), &trgb);
			}
			else {
				if (tex->type == TEX_ENVMAP)
					GPU_link(mat, "mtex_cube_map", texco, GPU_cube_map(tex->ima, &tex->iuser, false), &tin, &trgb);
				else if (tex->type == TEX_IMAGE)
					GPU_link(mat, "mtex_image", texco, GPU_image(tex->ima, &tex->iuser, false), &tin, &trgb);
			}
			rgbnor = TEX_RGB;
			if (tex->type == TEX_IMAGE || tex->type == TEX_ENVMAP)
				if (GPU_material_do_color_management(mat))
					GPU_link(mat, "srgb_to_linearrgb", trgb, &trgb);
			/* texture output */
			if ((rgbnor & TEX_RGB) && (mtex->texflag & MTEX_RGBTOINT)) {
				GPU_link(mat, "mtex_rgbtoint", trgb, &tin);
				rgbnor -= TEX_RGB;
			}
			if (mtex->texflag & MTEX_NEGATIVE) {
				if (rgbnor & TEX_RGB)
					GPU_link(mat, "mtex_rgb_invert", trgb, &trgb);
				else
					GPU_link(mat, "mtex_value_invert", tin, &tin);
			}
			if (mtex->texflag & MTEX_STENCIL) {
				if (rgbnor & TEX_RGB)
					GPU_link(mat, "mtex_rgb_stencil", stencil, trgb, &stencil, &trgb);
				else
					GPU_link(mat, "mtex_value_stencil", stencil, tin, &stencil, &tin);
			}
			else {
				if (rgbnor & TEX_RGB)
					GPU_link(mat, "mtex_alpha_multiply_value", trgb, stencil, &trgb);
				else
					GPU_link(mat, "math_multiply", stencil, tin, &tin);
			}
			/* color mapping */
			if (mtex->mapto & (WOMAP_HORIZ + WOMAP_ZENUP + WOMAP_ZENDOWN)) {
				if ((rgbnor & TEX_RGB) == 0)
					GPU_link(mat, "set_rgb", GPU_uniform(&mtex->r), &trgb);
				else
					GPU_link(mat, "mtex_alpha_from_col", trgb, &tin);
				GPU_link(mat, "set_rgb", trgb, &tcol);
				if (mtex->mapto & WOMAP_HORIZ) {
					texture_rgb_blend(mat, tcol, *hor, tin, GPU_uniform(&mtex->colfac), mtex->blendtype, hor);
				}
				if (mtex->mapto & (WOMAP_ZENUP + WOMAP_ZENDOWN)) {
					GPU_link(mat, "set_value_zero", &zenfac);
					if (wo->skytype & WO_SKYREAL) {
						if (mtex->mapto & WOMAP_ZENUP) {
							if (mtex->mapto & WOMAP_ZENDOWN) {
								GPU_link(mat, "world_zen_mapping", shi->view, GPU_uniform(&mtex->zenupfac),
								         GPU_uniform(&mtex->zendownfac), &zenfac);
							}
							else {
								GPU_link(mat, "world_zen_mapping", shi->view, GPU_uniform(&mtex->zenupfac),
								         GPU_uniform(&zero), &zenfac);
							}
						}
						else if (mtex->mapto & WOMAP_ZENDOWN) {
							GPU_link(mat, "world_zen_mapping", shi->view, GPU_uniform(&zero),
							         GPU_uniform(&mtex->zendownfac), &zenfac);
						}
					}
					else {
						if (mtex->mapto & WOMAP_ZENUP)
							GPU_link(mat, "set_value", GPU_uniform(&mtex->zenupfac), &zenfac);
						else if (mtex->mapto & WOMAP_ZENDOWN)
							GPU_link(mat, "set_value", GPU_uniform(&mtex->zendownfac), &zenfac);
					}
					texture_rgb_blend(mat, tcol, *zen, tin, zenfac, mtex->blendtype, zen);
				}
			}
			if (mtex->mapto & WOMAP_BLEND && wo->skytype & WO_SKYBLEND) {
				if (rgbnor & TEX_RGB)
					GPU_link(mat, "mtex_rgbtoint", trgb, &tin);
				texture_value_blend(mat, GPU_uniform(&mtex->def_var), *blend, tin, GPU_uniform(&mtex->blendfac), mtex->blendtype, blend);
			}
		}
	}
}

static void gpu_material_old_world(struct GPUMaterial *mat, struct World *wo)
{
	GPUShadeInput shi;
	GPUShadeResult shr;
	GPUNodeLink *hor, *zen, *ray, *blend;

	shi.gpumat = mat;

	for (int i = 0; i < MAX_MTEX; i++) {
		if (wo->mtex[i] && wo->mtex[i]->tex) {
			wo->skytype |= WO_SKYTEX;
			break;
		}
	}
	if ((wo->skytype & (WO_SKYBLEND + WO_SKYTEX)) == 0) {
		GPU_link(mat, "set_rgb", GPU_dynamic_uniform(&wo->horr, GPU_DYNAMIC_HORIZON_COLOR, NULL), &shr.combined);
	}
	else {
		GPU_link(mat, "set_rgb_zero", &shi.rgb);
		GPU_link(mat, "background_transform_to_world", GPU_builtin(GPU_VIEW_POSITION), &ray);
		if (wo->skytype & WO_SKYPAPER)
			GPU_link(mat, "world_paper_view", GPU_builtin(GPU_VIEW_POSITION), &shi.view);
		else
			GPU_link(mat, "shade_view", ray, &shi.view);
		if (wo->skytype & WO_SKYBLEND) {
			if (wo->skytype & WO_SKYPAPER) {
				if (wo->skytype & WO_SKYREAL)
					GPU_link(mat, "world_blend_paper_real", GPU_builtin(GPU_VIEW_POSITION), &blend);
				else
					GPU_link(mat, "world_blend_paper", GPU_builtin(GPU_VIEW_POSITION), &blend);
			}
			else {
				if (wo->skytype & WO_SKYREAL)
					GPU_link(mat, "world_blend_real", ray, &blend);
				else
					GPU_link(mat, "world_blend", ray, &blend);
			}
		}
		else {
			GPU_link(mat, "set_value_zero", &blend);
		}
		GPU_link(mat, "set_rgb", GPU_dynamic_uniform(&wo->horr, GPU_DYNAMIC_HORIZON_COLOR, NULL), &hor);
		GPU_link(mat, "set_rgb", GPU_dynamic_uniform(&wo->zenr, GPU_DYNAMIC_ZENITH_COLOR, NULL), &zen);
		do_world_tex(&shi, wo, &hor, &zen, &blend);
		if (wo->skytype & WO_SKYBLEND)
			GPU_link(mat, "node_mix_shader", blend, hor, zen, &shi.rgb);
		else
			GPU_link(mat, "set_rgb", hor, &shi.rgb);
		GPU_link(mat, "set_rgb", shi.rgb, &shr.combined);
	}
	GPU_material_output_link(mat, shr.combined);
}

GPUMaterial *GPU_material_world(struct Scene *scene, struct World *wo)
{
	LinkData *link;
	GPUMaterial *mat;

	for (link = wo->gpumaterial.first; link; link = link->next)
		if (((GPUMaterial *)link->data)->scene == scene)
			return link->data;

	/* allocate material */
	mat = GPU_material_construct_begin(NULL);
	mat->scene = scene;
	mat->type = GPU_MATERIAL_TYPE_WORLD;
	
	/* create nodes */
	if (BKE_scene_use_new_shading_nodes(scene) && wo->nodetree && wo->use_nodes) {
		ntreeGPUMaterialNodes(wo->nodetree, mat, NODE_NEW_SHADING);
	}
	else {
		gpu_material_old_world(mat, wo);
	}

	if (GPU_material_do_color_management(mat))
		if (mat->outlink)
			GPU_link(mat, "linearrgb_to_srgb", mat->outlink, &mat->outlink);

	gpu_material_construct_end(mat, wo->id.name);
	
	/* note that even if building the shader fails in some way, we still keep
	 * it to avoid trying to compile again and again, and simple do not use
	 * the actual shader on drawing */

	link = MEM_callocN(sizeof(LinkData), "GPUMaterialLink");
	link->data = mat;
	BLI_addtail(&wo->gpumaterial, link);

	return mat;
}


/* BGE-Remix P8: as in Multitexture, a texture mapped to alpha (or the face
 * image's alpha) makes the material see-through */
static bool gpu_material_texture_alpha(Material *ma)
{
	int a;
	if (ma->mode & MA_FACETEXTURE_ALPHA) {
		return true;
	}
	for (a = 0; a < MAX_MTEX; a++) {
		MTex *mtex = ma->mtex[a];
		if (mtex && mtex->tex && (ma->septex & (1 << a)) == 0 && (mtex->mapto & MAP_ALPHA)) {
			return true;
		}
	}
	return false;
}

/* BGE-Remix P8.6: the material in UV space, one value per pixel */
GPUMaterial *GPU_material_bake_create(Scene *scene, Material *ma, int channel, float scale)
{
	GPUMaterial *mat;
	GPUShadeInput shi;
	GPUNodeLink *uv, *out = NULL, *value;
	float srgb, one = 1.0f;

	if (!ma || (ma->use_nodes && ma->nodetree)) {
		return NULL;
	}
	mat = GPU_material_construct_begin(ma);
	mat->scene = scene;
	mat->type = GPU_MATERIAL_TYPE_MESH_BAKE;
	mat->is_opensubdiv = false;
	mat->alpha = 1;  /* the material's alpha, not 1 */

	GPU_shadeinput_set(mat, ma, &shi);
	if (ma->mode & MA_VERTEXCOLP) {
		shi.rgb = shi.vcol;
	}
	do_material_tex(&shi);

	GPU_link(mat, "texco_uv", GPU_attribute(CD_MTFACE, ""), &uv);
	switch (channel) {
		case GPU_BAKE_COLOR:
			srgb = GPU_material_do_color_management(mat) ? 1.0f : 0.0f;
			GPU_link(mat, "bake_out_color", shi.rgb, shi.refl, GPU_uniform(&srgb), uv, &out);
			break;
		case GPU_BAKE_ALPHA:
			GPU_link(mat, "bake_out_value", shi.alpha, GPU_uniform(&one), uv, &out);
			break;
		case GPU_BAKE_HARDNESS:
			if (shi.physical) {
				GPU_link(mat, "bake_out_value", shi.roughness, GPU_uniform(&one), uv, &out);
			}
			else {
				scale = 1.0f / 511.0f;
				GPU_link(mat, "bake_out_value", shi.har, GPU_uniform(&scale), uv, &out);
			}
			break;
		case GPU_BAKE_METALLIC:
			if (shi.physical) {
				GPU_link(mat, "bake_out_value", shi.metallic, GPU_uniform(&one), uv, &out);
			}
			else {
				GPU_link(mat, "set_value_zero", &value);
				GPU_link(mat, "bake_out_value", value, GPU_uniform(&one), uv, &out);
			}
			break;
		case GPU_BAKE_EMIT:
			scale = (scale > 0.0f) ? 1.0f / scale : 1.0f;
			GPU_link(mat, "bake_out_value", shi.emit, GPU_uniform(&scale), uv, &out);
			break;
		case GPU_BAKE_NORMAL:
			GPU_link(mat, "bake_out_normal", shi.vn, GPU_builtin(GPU_VIEW_NORMAL),
			         GPU_attribute(CD_TANGENT, ""), uv, &out);
			break;
		default:
			GPU_link(mat, "set_value", GPU_uniform(&one), &value);
			GPU_link(mat, "bake_out_value", value, GPU_uniform(&one), uv, &out);
			break;
	}
	GPU_material_output_link(mat, out);

	if (!gpu_material_construct_end(mat, "bake")) {
		GPU_material_bake_free(mat);
		return NULL;
	}
	return mat;
}

void GPU_material_bake_free(GPUMaterial *material)
{
	if (material) {
		if (material->pass) {
			GPU_pass_free(material->pass);
		}
		BLI_freelistN(&material->lamps);
		MEM_freeN(material);
	}
}

GPUMaterial *GPU_material_from_blender(Scene *scene, Material *ma, bool use_opensubdiv)
{
	GPUMaterial *mat;
	GPUNodeLink *outlink;
	LinkData *link;

	for (link = ma->gpumaterial.first; link; link = link->next) {
		GPUMaterial *current_material = (GPUMaterial *)link->data;
		if (current_material->scene == scene &&
		    current_material->is_opensubdiv == use_opensubdiv)
		{
			return current_material;
		}
	}

	/* allocate material */
	mat = GPU_material_construct_begin(ma);
	mat->scene = scene;
	mat->type = GPU_MATERIAL_TYPE_MESH;
	mat->is_opensubdiv = use_opensubdiv;

	/* render pipeline option */
	bool new_shading_nodes = BKE_scene_use_new_shading_nodes(scene);
	if (!new_shading_nodes && ((ma->mode & MA_TRANSP) || gpu_material_texture_alpha(ma) ||
	                           gpu_material_physical_alpha(ma)))
		GPU_material_enable_alpha(mat);
	else if (new_shading_nodes && ma->alpha < 1.0f)
		GPU_material_enable_alpha(mat);

	if (!(scene->gm.flag & GAME_GLSL_NO_NODES) && ma->nodetree && ma->use_nodes) {
		/* create nodes */
		if (new_shading_nodes)
			ntreeGPUMaterialNodes(ma->nodetree, mat, NODE_NEW_SHADING);
		else
			ntreeGPUMaterialNodes(ma->nodetree, mat, NODE_OLD_SHADING);
	}
	else {
		if (new_shading_nodes) {
			/* create simple diffuse material instead of nodes */
			outlink = gpu_material_diffuse_bsdf(mat, ma);
		}
		else {
			/* create blender material */
			outlink = GPU_blender_material(mat, ma);
		}

		GPU_material_output_link(mat, outlink);
	}

	if (GPU_material_do_color_management(mat))
		if (mat->outlink)
			GPU_link(mat, "linearrgb_to_srgb", mat->outlink, &mat->outlink);

	gpu_material_construct_end(mat, ma->id.name);

	/* note that even if building the shader fails in some way, we still keep
	 * it to avoid trying to compile again and again, and simple do not use
	 * the actual shader on drawing */

	link = MEM_callocN(sizeof(LinkData), "GPUMaterialLink");
	link->data = mat;
	BLI_addtail(&ma->gpumaterial, link);

	return mat;
}

void GPU_materials_free(void)
{
	Object *ob;
	Material *ma;
	World *wo;
	extern Material defmaterial;

	for (ma = G.main->mat.first; ma; ma = ma->id.next)
		GPU_material_free(&ma->gpumaterial);

	for (wo = G.main->world.first; wo; wo = wo->id.next)
		GPU_material_free(&wo->gpumaterial);
	
	GPU_material_free(&defmaterial.gpumaterial);

	for (ob = G.main->object.first; ob; ob = ob->id.next)
		GPU_lamp_free(ob);

	/* BGE-Remix P8.10: no shader uses these any more */
	if (gpu_scene_depth_tex) {
		GPU_texture_free(gpu_scene_depth_tex);
		gpu_scene_depth_tex = NULL;
	}
	if (gpu_scene_depth_fbo) {
		glDeleteFramebuffersEXT(1, &gpu_scene_depth_fbo);
		gpu_scene_depth_fbo = 0;
	}
	gpu_scene_depth_size[0] = gpu_scene_depth_size[1] = 0;
	gpu_scene_depth_on = 0.0f;
}

/* Lamps and shadow buffers */

static void gpu_lamp_calc_winmat(GPULamp *lamp)
{
	float temp, angle, pixsize, wsize;

	if (lamp->type == LA_SUN) {
		wsize = lamp->la->shadow_frustum_size;
		orthographic_m4(lamp->winmat, -wsize, wsize, -wsize, wsize, lamp->d, lamp->clipend);
	}
	else if (lamp->type == LA_SPOT) {
		angle = saacos(lamp->spotsi);
		temp = 0.5f * lamp->size * cosf(angle) / sinf(angle);
		pixsize = lamp->d / temp;
		wsize = pixsize * 0.5f * lamp->size;
		/* compute shadows according to X and Y scaling factors */
		perspective_m4(
		        lamp->winmat,
		        -wsize * lamp->spotvec[0], wsize * lamp->spotvec[0],
		        -wsize * lamp->spotvec[1], wsize * lamp->spotvec[1],
		        lamp->d, lamp->clipend);
	}
}

/* BGE-Remix P8.9: an area lamp's corners, as Blender Internal makes them
 * (area_lamp_vectors with one sample: the whole rectangle; the object's scale
 * stretches it, the size correction uses the lamp's own size) */
static void gpu_lamp_area_update(GPULamp *lamp, float obmat[4][4])
{
	const float xs = 0.5f * max_ff(lamp->area_x, 1e-4f), ys = 0.5f * max_ff(lamp->area_y, 1e-4f);
	const float sign[4][2] = {{-1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, -1.0f}};
	for (int i = 0; i < 4; i++) {
		for (int k = 0; k < 3; k++) {
			lamp->area_corner[i][k] = obmat[3][k] + sign[i][0] * xs * obmat[0][k] + sign[i][1] * ys * obmat[1][k];
		}
	}
	lamp->areasize = lamp->dist * lamp->dist / (4.0f * xs * ys);
}

void GPU_lamp_update(GPULamp *lamp, int lay, int hide, float obmat[4][4])
{
	float mat[4][4];
	float obmat_scale[3];

	lamp->lay = lay;
	lamp->hide = hide;

	normalize_m4_m4_ex(mat, obmat, obmat_scale);

	copy_v3_v3(lamp->vec, mat[2]);
	copy_v3_v3(lamp->co, mat[3]);
	copy_m4_m4(lamp->obmat, mat);
	invert_m4_m4(lamp->imat, mat);

	if (lamp->type == LA_SPOT) {
		/* update spotlamp scale on X and Y axis */
		lamp->spotvec[0] = obmat_scale[0] / obmat_scale[2];
		lamp->spotvec[1] = obmat_scale[1] / obmat_scale[2];
	}

	if (lamp->type == LA_AREA) {
		copy_m4_m4(lamp->area_obmat, obmat);
		gpu_lamp_area_update(lamp, obmat);
	}

	if (GPU_lamp_has_shadow_buffer(lamp) || lamp->type == LA_SPOT) {
		/* makeshadowbuf (BGE-Remix P8.9: spots without a shadow buffer too, for
		 * their projected textures) */
		gpu_lamp_calc_winmat(lamp);
	}
}

void GPU_lamp_update_size(GPULamp *lamp, float size, float area_x, float area_y, float sun_angle)
{
	lamp->size_radius = size;
	lamp->sun_angle = sun_angle;
	if (lamp->type == LA_AREA && (lamp->area_x != area_x || lamp->area_y != area_y)) {
		float mat[4][4];
		lamp->area_x = area_x;
		lamp->area_y = area_y;
		copy_m4_m4(mat, lamp->area_obmat);
		gpu_lamp_area_update(lamp, mat);
	}
}

void GPU_lamp_update_shadow(GPULamp *lamp, float bias, float bleedbias, const float color[3], float contact)
{
	/* as gpu_lamp_from_blender: initshadowbuf's 0.02 and a quarter */
	lamp->bias = 0.02f * bias * 0.25f;
	lamp->bleedbias = bleedbias;
	if (lamp->tex && lamp->fb) {
		copy_v3_v3(lamp->shadow_color, color);
	}
	/* BGE-Remix P8.10: the soft shadows' bias, contact shadows */
	lamp->shadow_params[1] = bias;
	lamp->contact[0] = max_ff(contact, 0.0f);
}

void GPU_lamp_update_colors(GPULamp *lamp, float r, float g, float b, float energy)
{
	lamp->energy = energy;
	if (lamp->mode & LA_NEG) lamp->energy = -lamp->energy;

	lamp->col[0] = r;
	lamp->col[1] = g;
	lamp->col[2] = b;
}

void GPU_lamp_update_distance(GPULamp *lamp, float distance, float att1, float att2,
                              float coeff_const, float coeff_lin, float coeff_quad)
{
	lamp->dist = distance;
	lamp->att1 = att1;
	lamp->att2 = att2;
	lamp->coeff_const = coeff_const;
	lamp->coeff_lin = coeff_lin;
	lamp->coeff_quad = coeff_quad;
	if (lamp->type == LA_AREA) {
		/* the size correction follows the distance */
		lamp->areasize = lamp->dist * lamp->dist /
		                 (max_ff(lamp->area_x, 1e-4f) * max_ff(lamp->area_y, 1e-4f));
	}
}

void GPU_lamp_update_spot(GPULamp *lamp, float spotsize, float spotblend)
{
	lamp->spotsi = cosf(spotsize * 0.5f);
	lamp->spotbl = (1.0f - lamp->spotsi) * spotblend;
}

static void gpu_lamp_from_blender(Scene *scene, Object *ob, Object *par, Lamp *la, GPULamp *lamp)
{
	lamp->scene = scene;
	lamp->ob = ob;
	lamp->par = par;
	lamp->la = la;

	/* add_render_lamp */
	lamp->mode = la->mode;
	lamp->type = la->type;

	lamp->energy = la->energy;
	if (lamp->mode & LA_NEG) lamp->energy = -lamp->energy;

	lamp->col[0] = la->r;
	lamp->col[1] = la->g;
	lamp->col[2] = la->b;

	/* BGE-Remix P8.9: the light's size (before GPU_lamp_update: area corners) */
	lamp->dist = la->dist;
	lamp->size_radius = BKE_lamp_size(la);
	lamp->area_x = la->area_size;
	lamp->area_y = (la->area_shape == LA_AREA_RECT) ? la->area_sizey : la->area_size;

	GPU_lamp_update(lamp, ob->lay, (ob->restrictflag & OB_RESTRICT_RENDER), ob->obmat);

	lamp->spotsi = la->spotsize;
	if (lamp->mode & LA_HALO)
		if (lamp->spotsi > DEG2RADF(170.0f))
			lamp->spotsi = DEG2RADF(170.0f);
	lamp->spotsi = cosf(lamp->spotsi * 0.5f);
	lamp->spotbl = (1.0f - lamp->spotsi) * la->spotblend;
	lamp->k = la->k;

	lamp->dist = la->dist;
	lamp->falloff_type = la->falloff_type;
	lamp->att1 = la->att1;
	lamp->att2 = la->att2;
	lamp->coeff_const = la->coeff_const;
	lamp->coeff_lin = la->coeff_lin;
	lamp->coeff_quad = la->coeff_quad;
	lamp->curfalloff = la->curfalloff;
	/* BGE-Remix P8.9: physical falloff is Inverse Coefficients with these */
	if (BKE_lamp_is_physical(la)) {
		lamp->falloff_type = LA_FALLOFF_INVCOEFFICIENTS;
		BKE_lamp_physical_coefficients(la->dist, BKE_lamp_size(la), &lamp->coeff_const, &lamp->coeff_lin,
		                               &lamp->coeff_quad);
	}
	lamp->bleedbias = la->bleedbias;

	/* initshadowbuf */
	lamp->bias = 0.02f * la->bias;
	lamp->size = la->bufsize;
	lamp->d = la->clipsta;
	lamp->clipend = la->clipend;

	/* arbitrary correction for the fact we do no soft transition */
	lamp->bias *= 0.25f;

	/* BGE-Remix P8.10: soft shadows and contact shadows */
	lamp->sun_angle = BKE_lamp_sun_angle(la);
	lamp->shadow_params[1] = la->bias;
	lamp->contact[0] = BKE_lamp_contact_distance(la);
	lamp->contact[1] = 0.2f;   /* thickness */
	lamp->contact[3] = 0.01f;  /* bias */
}

static void gpu_lamp_shadow_free(GPULamp *lamp)
{
	if (lamp->tex) {
		GPU_texture_free(lamp->tex);
		lamp->tex = NULL;
	}
	if (lamp->depthtex) {
		GPU_texture_free(lamp->depthtex);
		lamp->depthtex = NULL;
	}
	if (lamp->fb) {
		GPU_framebuffer_free(lamp->fb);
		lamp->fb = NULL;
	}
	if (lamp->blurtex) {
		GPU_texture_free(lamp->blurtex);
		lamp->blurtex = NULL;
	}
	if (lamp->blurfb) {
		GPU_framebuffer_free(lamp->blurfb);
		lamp->blurfb = NULL;
	}
	if (lamp->depthfb) {
		GPU_framebuffer_free(lamp->depthfb);
		lamp->depthfb = NULL;
	}
	lamp->shadow_kind = GPU_LAMP_SHADOW_NONE;
}

/* BGE-Remix P8.10: the lamp's shadow buffer: spot lamps one view (Simple or
 * Variance), point and area lamps a cube's six faces side by side, the sun
 * four cascades; Simple maps are read as depths (soft shadows), Variance
 * ones get their depth drawn first and turned into moments, so cut-out
 * casters work in both */
static bool gpu_lamp_shadow_create(GPULamp *lamp)
{
	Lamp *la = lamp->la;
	const int kind = !BKE_lamp_casts_shadow(la) ? GPU_LAMP_SHADOW_NONE :
	                 (la->type == LA_SPOT) ? GPU_LAMP_SHADOW_SPOT :
	                 (la->type == LA_SUN) ? GPU_LAMP_SHADOW_CASCADE : GPU_LAMP_SHADOW_CUBE;
	if (kind == GPU_LAMP_SHADOW_NONE) {
		return false;
	}

	lamp->shadow_kind = kind;
	lamp->tiles_x = (kind == GPU_LAMP_SHADOW_CUBE) ? 3 : (kind == GPU_LAMP_SHADOW_CASCADE) ? 2 : 1;
	lamp->tiles_y = (kind == GPU_LAMP_SHADOW_SPOT) ? 1 : 2;
	lamp->nviews = lamp->tiles_x * lamp->tiles_y;
	lamp->size = min_ii(max_ii(lamp->size, 16), GPU_SHADOW_MAX_ATLAS / lamp->tiles_x);
	lamp->shadow_params[3] = 1.0f / (float)lamp->size;

	const int w = lamp->size * lamp->tiles_x, h = lamp->size * lamp->tiles_y;

	lamp->fb = GPU_framebuffer_create();
	if (!lamp->fb) {
		gpu_lamp_shadow_free(lamp);
		return false;
	}

	if (kind == GPU_LAMP_SHADOW_SPOT && la->shadowmap_type == LA_SHADMAP_VARIANCE) {
		/* the depth, drawn first */
		lamp->depthfb = GPU_framebuffer_create();
		lamp->depthtex = GPU_texture_create_depth(lamp->size, lamp->size, NULL);
		if (!lamp->depthfb || !lamp->depthtex ||
		    !GPU_framebuffer_texture_attach(lamp->depthfb, lamp->depthtex, 0, NULL) ||
		    !GPU_framebuffer_check_valid(lamp->depthfb, NULL))
		{
			gpu_lamp_shadow_free(lamp);
			return false;
		}
		gpu_shadow_texture_raw(lamp->depthtex);

		/* its moments, and the texture to blur them through */
		lamp->tex = GPU_texture_create_vsm_shadow_map(lamp->size, NULL);
		if (!lamp->tex || !GPU_framebuffer_texture_attach(lamp->fb, lamp->tex, 0, NULL)) {
			gpu_lamp_shadow_free(lamp);
			return false;
		}
		GPU_texture_bind_as_framebuffer(lamp->tex);
		if (!GPU_framebuffer_check_valid(lamp->fb, NULL)) {
			GPU_framebuffer_texture_unbind(lamp->fb, lamp->tex);
			gpu_lamp_shadow_free(lamp);
			return false;
		}
		GPU_framebuffer_texture_unbind(lamp->fb, lamp->tex);

		lamp->blurfb = GPU_framebuffer_create();
		lamp->blurtex = GPU_texture_create_vsm_shadow_map(lamp->size * 0.5, NULL);
		if (!lamp->blurfb || !lamp->blurtex || !GPU_framebuffer_texture_attach(lamp->blurfb, lamp->blurtex, 0, NULL)) {
			gpu_lamp_shadow_free(lamp);
			return false;
		}
		GPU_texture_bind_as_framebuffer(lamp->blurtex);
		if (!GPU_framebuffer_check_valid(lamp->blurfb, NULL)) {
			GPU_framebuffer_texture_unbind(lamp->blurfb, lamp->blurtex);
			gpu_lamp_shadow_free(lamp);
			return false;
		}
		GPU_framebuffer_texture_unbind(lamp->blurfb, lamp->blurtex);
	}
	else {
		lamp->tex = GPU_texture_create_depth(w, h, NULL);
		if (!lamp->tex || !GPU_framebuffer_texture_attach(lamp->fb, lamp->tex, 0, NULL) ||
		    !GPU_framebuffer_check_valid(lamp->fb, NULL))
		{
			gpu_lamp_shadow_free(lamp);
			return false;
		}
		gpu_shadow_texture_raw(lamp->tex);
	}

	GPU_framebuffer_restore();
	return true;
}

GPULamp *GPU_lamp_from_blender(Scene *scene, Object *ob, Object *par)
{
	Lamp *la;
	GPULamp *lamp;
	LinkData *link;

	for (link = ob->gpulamp.first; link; link = link->next) {
		lamp = (GPULamp *)link->data;

		if (lamp->par == par && lamp->scene == scene)
			return link->data;
	}

	lamp = MEM_callocN(sizeof(GPULamp), "GPULamp");
	gpu_shared_slot_add(lamp);

	link = MEM_callocN(sizeof(LinkData), "GPULampLink");
	link->data = lamp;
	BLI_addtail(&ob->gpulamp, link);

	la = ob->data;
	gpu_lamp_from_blender(scene, ob, par, la, lamp);

	/* BGE-Remix P8.10: every lamp that casts gets a shadow buffer */
	if (gpu_lamp_shadow_create(lamp)) {
		lamp->shadow_color[0] = la->shdwr;
		lamp->shadow_color[1] = la->shdwg;
		lamp->shadow_color[2] = la->shdwb;
	}
	else {
		lamp->shadow_color[0] = 1.0;
		lamp->shadow_color[1] = 1.0;
		lamp->shadow_color[2] = 1.0;
	}

	return lamp;
}

void GPU_lamp_free(Object *ob)
{
	GPULamp *lamp;
	LinkData *link;
	LinkData *nlink;
	Material *ma;

	for (link = ob->gpulamp.first; link; link = link->next) {
		lamp = link->data;

		while (lamp->materials.first) {
			nlink = lamp->materials.first;
			ma = nlink->data;
			BLI_freelinkN(&lamp->materials, nlink);

			if (ma->gpumaterial.first)
				GPU_material_free(&ma->gpumaterial);
		}

		gpu_lamp_shadow_free(lamp);

		gpu_shared_slot_remove(lamp);
		MEM_freeN(lamp);
	}

	BLI_freelistN(&ob->gpulamp);
}

bool GPU_lamp_has_shadow_buffer(GPULamp *lamp)
{
	return (!(lamp->scene->gm.flag & GAME_GLSL_NO_SHADOWS) &&
	        !(lamp->scene->gm.flag & GAME_GLSL_NO_LIGHTS) &&
	        lamp->tex && lamp->fb);
}

/* BGE-Remix P8.10: a cube face's camera, from the lamp's own axes: it looks
 * along +-X, +-Y or +-Z, its up the next axis (the shaders' shadow_cube_soft
 * picks the same) */
static void gpu_lamp_cube_face(int face, float r_mat[4][4])
{
	float f[3] = {0.0f, 0.0f, 0.0f}, u[3] = {0.0f, 0.0f, 0.0f}, r[3];
	const int axis = face / 2;
	f[axis] = (face % 2) ? -1.0f : 1.0f;
	u[(axis + 1) % 3] = 1.0f;
	cross_v3_v3v3(r, f, u);

	unit_m4(r_mat);
	for (int k = 0; k < 3; k++) {
		r_mat[k][0] = r[k];
		r_mat[k][1] = u[k];
		r_mat[k][2] = -f[k];
	}
}

/* BGE-Remix P8.10: the sun's cascades: the camera's view up to the lamp's
 * Clip End split into four (each further one longer), each in a square
 * around the slice's bounding sphere, moved in whole texels so the edges
 * do not crawl as the camera moves */
static void gpu_lamp_cascades(GPULamp *lamp)
{
	float rot[4][4];
	copy_m4_m4(rot, lamp->viewmat);
	zero_v3(rot[3]);

	if (!lamp->has_cam) {
		/* no camera yet: the box around the lamp, for every cascade */
		const float wsize = lamp->la->shadow_frustum_size;
		for (int c = 0; c < GPU_SHADOW_CASCADES; c++) {
			copy_m4_m4(lamp->view_viewmat[c], lamp->viewmat);
			orthographic_m4(lamp->view_winmat[c], -wsize, wsize, -wsize, wsize, lamp->d, lamp->clipend);
			lamp->cascade_scale[c] = 0.5f / max_ff(wsize, 1e-4f);
			lamp->cascade_range[c] = max_ff(lamp->clipend - lamp->d, 1e-4f);
		}
	}
	else {
		/* the camera's frustum corners in its view space, near and far */
		float pinv[4][4], cnear[4][3], cfar[4][3];
		const float ndc[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
		invert_m4_m4(pinv, lamp->cam_winmat);
		for (int k = 0; k < 4; k++) {
			float pn[4] = {ndc[k][0], ndc[k][1], -1.0f, 1.0f}, pf[4] = {ndc[k][0], ndc[k][1], 1.0f, 1.0f};
			mul_m4_v4(pinv, pn);
			mul_m4_v4(pinv, pf);
			mul_v3_v3fl(cnear[k], pn, 1.0f / pn[3]);
			mul_v3_v3fl(cfar[k], pf, 1.0f / pf[3]);
		}
		const float znear = -cnear[0][2], zfar = -cfar[0][2];
		const float reach = max_ff(min_ff(zfar, znear + lamp->clipend), znear + 1e-3f);

		float split[GPU_SHADOW_CASCADES + 1];
		const float lognear = max_ff(znear, 1e-3f);
		for (int c = 0; c <= GPU_SHADOW_CASCADES; c++) {
			const float t = (float)c / GPU_SHADOW_CASCADES;
			const float uniform = znear + (reach - znear) * t;
			const float logd = lognear * powf(reach / lognear, t);
			split[c] = 0.75f * logd + 0.25f * uniform;
		}
		split[0] = znear;
		split[GPU_SHADOW_CASCADES] = reach;

		for (int c = 0; c < GPU_SHADOW_CASCADES; c++) {
			float corners[8][3], center[3] = {0.0f, 0.0f, 0.0f}, radius = 0.0f;
			for (int k = 0; k < 8; k++) {
				const float z = split[c + (k / 4)];
				const float t = (z - znear) / max_ff(zfar - znear, 1e-6f);
				interp_v3_v3v3(corners[k], cnear[k % 4], cfar[k % 4], t);
				mul_m4_v3(lamp->cam_viewinv, corners[k]);
				add_v3_v3(center, corners[k]);
			}
			mul_v3_fl(center, 1.0f / 8.0f);
			for (int k = 0; k < 8; k++) {
				radius = max_ff(radius, len_v3v3(corners[k], center));
			}
			radius = ceilf(radius * 16.0f) / 16.0f;

			/* in the sun's axes, moved in whole texels */
			float lc[3];
			mul_v3_m4v3(lc, rot, center);
			const float texel = 2.0f * radius / (float)lamp->size;
			lc[0] = floorf(lc[0] / texel) * texel;
			lc[1] = floorf(lc[1] / texel) * texel;

			/* the depth reaches toward the sun by the lamp's Clip End, for
			 * casters outside the slice */
			const float back = max_ff(lamp->clipend, radius);
			copy_m4_m4(lamp->view_viewmat[c], rot);
			orthographic_m4(lamp->view_winmat[c], lc[0] - radius, lc[0] + radius, lc[1] - radius, lc[1] + radius,
			                -lc[2] - radius - back, -lc[2] + radius);
			lamp->cascade_scale[c] = 0.5f / radius;
			lamp->cascade_range[c] = 2.0f * radius + back;
		}
	}

	float rangemat[4][4], persmat[4][4];
	unit_m4(rangemat);
	rangemat[0][0] = rangemat[1][1] = rangemat[2][2] = 0.5f;
	rangemat[3][0] = rangemat[3][1] = rangemat[3][2] = 0.5f;
	for (int c = 0; c < GPU_SHADOW_CASCADES; c++) {
		mul_m4_m4m4(persmat, lamp->view_winmat[c], lamp->view_viewmat[c]);
		mul_m4_m4m4(lamp->cascade_persmat[c], rangemat, persmat);
	}
}

void GPU_lamp_update_buffer_mats(GPULamp *lamp)
{
	float rangemat[4][4], persmat[4][4];

	/* initshadowbuf */
	invert_m4_m4(lamp->viewmat, lamp->obmat);
	normalize_v3(lamp->viewmat[0]);
	normalize_v3(lamp->viewmat[1]);
	normalize_v3(lamp->viewmat[2]);

	/* makeshadowbuf */
	mul_m4_m4m4(persmat, lamp->winmat, lamp->viewmat);

	/* opengl depth buffer is range 0.0..1.0 instead of -1.0..1.0 in blender */
	unit_m4(rangemat);
	rangemat[0][0] = 0.5f;
	rangemat[1][1] = 0.5f;
	rangemat[2][2] = 0.5f;
	rangemat[3][0] = 0.5f;
	rangemat[3][1] = 0.5f;
	rangemat[3][2] = 0.5f;

	mul_m4_m4m4(lamp->persmat, rangemat, persmat);

	/* BGE-Remix P8.10: each view of the shadow buffer, and what the shaders
	 * read: the light's width (sun: its spread) and the views' depths */
	switch (lamp->shadow_kind) {
		case GPU_LAMP_SHADOW_SPOT:
			copy_m4_m4(lamp->view_viewmat[0], lamp->viewmat);
			copy_m4_m4(lamp->view_winmat[0], lamp->winmat);
			lamp->shadow_params[0] = 2.0f * lamp->size_radius;
			lamp->shadow_clip[0] = lamp->d;
			lamp->shadow_clip[1] = lamp->clipend;
			lamp->shadow_clip[2] = 1.0f / max_ff(lamp->winmat[0][0], 1e-6f);
			break;
		case GPU_LAMP_SHADOW_CUBE:
		{
			const float n = lamp->d, t = GPU_SHADOW_CUBE_TAN;
			for (int k = 0; k < 6; k++) {
				float face[4][4];
				gpu_lamp_cube_face(k, face);
				mul_m4_m4m4(lamp->view_viewmat[k], face, lamp->viewmat);
				perspective_m4(lamp->view_winmat[k], -n * t, n * t, -n * t, n * t, n, lamp->clipend);
			}
			lamp->shadow_params[0] = (lamp->type == LA_AREA) ? 0.5f * (lamp->area_x + lamp->area_y) :
			                                                   2.0f * lamp->size_radius;
			lamp->shadow_clip[0] = n;
			lamp->shadow_clip[1] = lamp->clipend;
			lamp->shadow_clip[2] = t;
			break;
		}
		case GPU_LAMP_SHADOW_CASCADE:
			gpu_lamp_cascades(lamp);
			lamp->shadow_params[0] = 2.0f * tanf(DEG2RADF(max_ff(lamp->sun_angle, 0.0f)) * 0.5f);
			break;
	}
}

int GPU_lamp_shadow_view_count(GPULamp *lamp)
{
	return GPU_lamp_has_shadow_buffer(lamp) ? lamp->nviews : 0;
}

void GPU_lamp_shadow_camera(GPULamp *lamp, float camtoworld[4][4], float winmat[4][4])
{
	if (camtoworld && winmat && fabsf(determinant_m4(winmat)) > 0.0f) {
		copy_m4_m4(lamp->cam_viewinv, camtoworld);
		copy_m4_m4(lamp->cam_winmat, winmat);
		lamp->has_cam = true;
	}
	else {
		lamp->has_cam = false;
	}
}

unsigned int GPU_lamp_shadow_signature(GPULamp *lamp)
{
	/* FNV-1a over the views and the settings that change the buffer */
	unsigned int h = 2166136261u;
	GPU_lamp_update_buffer_mats(lamp);
#define HASH_BYTES(p, n) \
	{ const unsigned char *b_ = (const unsigned char *)(p); \
	  for (size_t i_ = 0; i_ < (size_t)(n); i_++) { h ^= b_[i_]; h *= 16777619u; } } (void)0
	HASH_BYTES(lamp->view_viewmat, sizeof(float[16]) * lamp->nviews);
	HASH_BYTES(lamp->view_winmat, sizeof(float[16]) * lamp->nviews);
	HASH_BYTES(&lamp->size, sizeof(lamp->size));
	HASH_BYTES(&lamp->lay, sizeof(lamp->lay));
#undef HASH_BYTES
	return h;
}

void GPU_lamp_shadow_buffer_bind(GPULamp *lamp, float viewmat[4][4], int *winsize, float winmat[4][4])
{
	GPU_lamp_update_buffer_mats(lamp);

	/* opengl */
	glDisable(GL_SCISSOR_TEST);
	/* BGE-Remix P8.10: Variance draws the depth first */
	GPU_texture_bind_as_framebuffer(lamp->depthfb ? lamp->depthtex : lamp->tex);
	glDepthMask(GL_TRUE);
	glClear(GL_DEPTH_BUFFER_BIT);

	/* set matrices */
	GPU_lamp_shadow_buffer_view(lamp, 0, viewmat, winmat, NULL);
	*winsize = lamp->size;
}

void GPU_lamp_shadow_buffer_view(GPULamp *lamp, int view, float viewmat[4][4], float winmat[4][4], int r_rect[4])
{
	const int v = max_ii(0, min_ii(view, lamp->nviews - 1));
	const int x = (v % lamp->tiles_x) * lamp->size, y = (v / lamp->tiles_x) * lamp->size;

	/* the view's place in the atlas; its clears stay in it */
	glViewport(x, y, lamp->size, lamp->size);
	glEnable(GL_SCISSOR_TEST);
	glScissor(x, y, lamp->size, lamp->size);

	copy_m4_m4(viewmat, lamp->view_viewmat[v]);
	copy_m4_m4(winmat, lamp->view_winmat[v]);
	if (r_rect) {
		r_rect[0] = x;
		r_rect[1] = y;
		r_rect[2] = lamp->size;
		r_rect[3] = lamp->size;
	}
}

/* BGE-Remix P8.10: a Variance map's moments from its depth */
static GPUShader *gpu_vsm_moments_shader = NULL;

static void gpu_lamp_vsm_moments(GPULamp *lamp)
{
	static const char *vert =
	        "varying vec2 uv;\n"
	        "void main() {\n"
	        "	uv = gl_MultiTexCoord0.st;\n"
	        "	gl_Position = gl_Vertex;\n"
	        "}\n";
	static const char *frag =
	        "uniform sampler2D depthmap;\n"
	        "varying vec2 uv;\n"
	        "void main() {\n"
	        "	float d = texture2D(depthmap, uv).r;\n"
	        "	float dx = dFdx(d), dy = dFdy(d);\n"
	        "	gl_FragColor = vec4(d, d * d + 0.25 * (dx * dx + dy * dy), 0.0, 0.0);\n"
	        "}\n";

	if (!gpu_vsm_moments_shader) {
		gpu_vsm_moments_shader = GPU_shader_create(vert, frag, NULL, NULL, NULL, 0, 0, 0);
		if (!gpu_vsm_moments_shader) {
			return;
		}
	}

	GPU_texture_bind_as_framebuffer(lamp->tex);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	GPU_shader_bind(gpu_vsm_moments_shader);
	GPU_texture_bind(lamp->depthtex, 0);
	GPU_shader_uniform_texture(gpu_vsm_moments_shader,
	                           GPU_shader_get_uniform(gpu_vsm_moments_shader, "depthmap"), lamp->depthtex);
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
	glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, -1.0f);
	glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
	glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, 1.0f);
	glEnd();
	GPU_texture_unbind(lamp->depthtex);
	GPU_shader_unbind();

	GPU_framebuffer_blur(lamp->fb, lamp->tex, lamp->blurfb, lamp->blurtex);
	GPU_framebuffer_texture_unbind(lamp->fb, lamp->tex);
}

/* BGE-Remix P8.10: BGE_REMIX_SHADOW_DUMP=<folder> writes each lamp's shadow
 * buffer once (the sun's once it follows a camera) with its views, for
 * checking the maps against what the shaders expect: <lamp>.depth holds
 * ints w, h, views, size, has_cam, then floats: the depths (w x h), each
 * view's viewmat and winmat, cascade scale and range, shadow params and
 * clip, the camera's camtoworld and winmat */
static void gpu_lamp_shadow_dump(GPULamp *lamp)
{
	const char *dir = getenv("BGE_REMIX_SHADOW_DUMP");
	if (!dir || !dir[0] || lamp->dumped || lamp->depthfb ||
	    (lamp->shadow_kind == GPU_LAMP_SHADOW_CASCADE && !lamp->has_cam))
	{
		return;
	}
	lamp->dumped = true;
	const int w = lamp->size * lamp->tiles_x, h = lamp->size * lamp->tiles_y;
	float *depth = MEM_mallocN(sizeof(float) * w * h, "shadow dump");
	glBindTexture(GL_TEXTURE_2D, GPU_texture_opengl_bindcode(lamp->tex));
	glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
	glBindTexture(GL_TEXTURE_2D, 0);
	char path[1024];
	BLI_snprintf(path, sizeof(path), "%s/%s.depth", dir, lamp->ob->id.name + 2);
	FILE *f = fopen(path, "wb");
	if (f) {
		const int head[5] = {w, h, lamp->nviews, lamp->size, lamp->has_cam ? 1 : 0};
		fwrite(head, sizeof(int), 5, f);
		fwrite(depth, sizeof(float), (size_t)w * h, f);
		fwrite(lamp->view_viewmat, sizeof(float[16]), 6, f);
		fwrite(lamp->view_winmat, sizeof(float[16]), 6, f);
		fwrite(lamp->cascade_scale, sizeof(float), 4, f);
		fwrite(lamp->cascade_range, sizeof(float), 4, f);
		fwrite(lamp->shadow_params, sizeof(float), 4, f);
		fwrite(lamp->shadow_clip, sizeof(float), 4, f);
		fwrite(lamp->cam_viewinv, sizeof(float), 16, f);
		fwrite(lamp->cam_winmat, sizeof(float), 16, f);
		fclose(f);
		printf("BGE-Remix shadow: %s dumped to %s\n", lamp->ob->id.name + 2, path);
	}
	MEM_freeN(depth);
}

void GPU_lamp_shadow_buffer_unbind(GPULamp *lamp)
{
	glDisable(GL_SCISSOR_TEST);
	if (lamp->depthfb) {
		GPU_framebuffer_texture_unbind(lamp->depthfb, lamp->depthtex);
		gpu_lamp_vsm_moments(lamp);
	}
	else {
		GPU_framebuffer_texture_unbind(lamp->fb, lamp->tex);
	}
	GPU_framebuffer_restore();
	glEnable(GL_SCISSOR_TEST);
	gpu_lamp_shadow_dump(lamp);
}

int GPU_lamp_shadow_buffer_type(GPULamp *lamp)
{
	/* BGE-Remix P8.10: Variance maps are drawn as depth too (no override shader) */
	return lamp->depthfb ? LA_SHADMAP_VARIANCE : LA_SHADMAP_SIMPLE;
}

/* BGE-Remix P8.10: the scene's depth, for contact shadows */
void GPU_scene_depth_begin(int width, int height)
{
	GPUTexture *tex = gpu_scene_depth();
	if (!tex || width <= 0 || height <= 0) {
		return;
	}

	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &gpu_scene_depth_prevfb);
	if (!gpu_scene_depth_fbo) {
		glGenFramebuffersEXT(1, &gpu_scene_depth_fbo);
		gpu_scene_depth_size[0] = gpu_scene_depth_size[1] = -1;
	}
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, gpu_scene_depth_fbo);
	if (gpu_scene_depth_size[0] != width || gpu_scene_depth_size[1] != height) {
		glBindTexture(GL_TEXTURE_2D, GPU_texture_opengl_bindcode(tex));
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
		glBindTexture(GL_TEXTURE_2D, 0);
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_TEXTURE_2D,
		                          GPU_texture_opengl_bindcode(tex), 0);
		gpu_scene_depth_size[0] = width;
		gpu_scene_depth_size[1] = height;
	}
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);

	glPushAttrib(GL_VIEWPORT_BIT | GL_SCISSOR_BIT | GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT);
	glViewport(0, 0, width, height);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glClear(GL_DEPTH_BUFFER_BIT);
	/* not read while it is drawn */
	gpu_scene_depth_on = 0.0f;
	gpu_scene_depth_drawing = true;
}

void GPU_scene_depth_end(void)
{
	if (!gpu_scene_depth_drawing) {
		return;
	}
	gpu_scene_depth_drawing = false;
	glPopAttrib();
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, (GLuint)gpu_scene_depth_prevfb);
	gpu_scene_depth_on = 1.0f;
}

void GPU_scene_depth_disable(void)
{
	gpu_scene_depth_on = 0.0f;
}

int GPU_lamp_shadow_bind_code(GPULamp *lamp)
{
	return lamp->tex ? GPU_texture_opengl_bindcode(lamp->tex) : -1;
}

float *GPU_lamp_dynpersmat(GPULamp *lamp)
{
	return &lamp->dynpersmat[0][0];
}

int GPU_lamp_shadow_layer(GPULamp *lamp)
{
	if (lamp->fb && lamp->tex && (lamp->mode & (LA_LAYER | LA_LAYER_SHADOW)))
		return lamp->lay;
	else
		return -1;
}

GPUNodeLink *GPU_lamp_get_data(
        GPUMaterial *mat, GPULamp *lamp,
        GPUNodeLink **r_col, GPUNodeLink **r_lv, GPUNodeLink **r_dist, GPUNodeLink **r_shadow, GPUNodeLink **r_energy)
{
	GPUNodeLink *visifac;

	*r_col = GPU_dynamic_uniform(lamp->dyncol, GPU_DYNAMIC_LAMP_DYNCOL, lamp->ob);
	*r_energy = GPU_dynamic_uniform(&lamp->dynenergy, GPU_DYNAMIC_LAMP_DYNENERGY, lamp->ob);
	visifac = lamp_get_visibility(mat, lamp, r_lv, r_dist);

	shade_light_textures(mat, lamp, r_col);

	if (GPU_lamp_has_shadow_buffer(lamp)) {
		GPUNodeLink *vn, *inp, *shadfac;

		GPU_link(mat, "shade_norm", GPU_builtin(GPU_VIEW_NORMAL), &vn);
		GPU_link(mat, "shade_inp", vn, *r_lv, &inp);

		/* BGE-Remix P8.10: every lamp type, soft from the light's size */
		shadfac = gpu_lamp_shadow(mat, lamp, vn, *r_lv, inp);
		GPU_link(mat, "shadow_color_mix", inp, shadfac,
		         GPU_dynamic_uniform(lamp->shadow_color, GPU_DYNAMIC_LAMP_SHADOWCOLOR, lamp->ob), r_shadow);
	}
	else {
		GPU_link(mat, "set_rgb_one", r_shadow);
	}

	/* ensure shadow buffer and lamp textures will be updated */
	add_user_list(&mat->lamps, lamp);
	add_user_list(&lamp->materials, mat->ma);

	return visifac;
}

/* export the GLSL shader */

GPUShaderExport *GPU_shader_export(struct Scene *scene, struct Material *ma)
{
	static struct {
		GPUBuiltin gputype;
		GPUDynamicType dynamictype;
		GPUDataType datatype;
	} builtins[] = {
		{ GPU_VIEW_MATRIX, GPU_DYNAMIC_OBJECT_VIEWMAT, GPU_DATA_16F },
		{ GPU_INVERSE_VIEW_MATRIX, GPU_DYNAMIC_OBJECT_VIEWIMAT, GPU_DATA_16F },
		{ GPU_OBJECT_MATRIX, GPU_DYNAMIC_OBJECT_MAT, GPU_DATA_16F },
		{ GPU_INVERSE_OBJECT_MATRIX, GPU_DYNAMIC_OBJECT_IMAT, GPU_DATA_16F },
		{ GPU_LOC_TO_VIEW_MATRIX, GPU_DYNAMIC_OBJECT_LOCTOVIEWMAT, GPU_DATA_16F },
		{ GPU_INVERSE_LOC_TO_VIEW_MATRIX, GPU_DYNAMIC_OBJECT_LOCTOVIEWIMAT, GPU_DATA_16F },
		{ GPU_OBCOLOR, GPU_DYNAMIC_OBJECT_COLOR, GPU_DATA_4F },
		{ GPU_AUTO_BUMPSCALE, GPU_DYNAMIC_OBJECT_AUTOBUMPSCALE, GPU_DATA_1F },
		{ 0 }
	};

	GPUShaderExport *shader = NULL;
	GPUInput *input;
	int liblen, fraglen;

	/* TODO(sergey): How to determine whether we need OSD or not here? */
	GPUMaterial *mat = GPU_material_from_blender(scene, ma, false);
	GPUPass *pass = (mat) ? mat->pass : NULL;

	if (pass && pass->fragmentcode && pass->vertexcode) {
		shader = MEM_callocN(sizeof(GPUShaderExport), "GPUShaderExport");

		for (input = pass->inputs.first; input; input = input->next) {
			GPUInputUniform *uniform = MEM_callocN(sizeof(GPUInputUniform), "GPUInputUniform");

			if (input->ima) {
				/* image sampler uniform */
				uniform->type = GPU_DYNAMIC_SAMPLER_2DIMAGE;
				uniform->datatype = GPU_DATA_1I;
				uniform->image = input->ima;
				uniform->texnumber = input->texid;
				BLI_strncpy(uniform->varname, input->shadername, sizeof(uniform->varname));
			}
			else if (input->tex) {
				/* generated buffer */
				uniform->texnumber = input->texid;
				uniform->datatype = GPU_DATA_1I;
				BLI_strncpy(uniform->varname, input->shadername, sizeof(uniform->varname));

				switch (input->textype) {
					case GPU_SHADOW2D:
						uniform->type = GPU_DYNAMIC_SAMPLER_2DSHADOW;
						uniform->lamp = input->dynamicdata;
						break;
					case GPU_TEX2D:
						if (GPU_texture_opengl_bindcode(input->tex)) {
							uniform->type = GPU_DYNAMIC_SAMPLER_2DBUFFER;
							glBindTexture(GL_TEXTURE_2D, GPU_texture_opengl_bindcode(input->tex));
							uniform->texsize = GPU_texture_width(input->tex) * GPU_texture_height(input->tex);
							uniform->texpixels = MEM_mallocN(uniform->texsize * 4, "RGBApixels");
							glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, uniform->texpixels);
							glBindTexture(GL_TEXTURE_2D, 0);
						}
						break;

					case GPU_NONE:
					case GPU_TEXCUBE:
					case GPU_FLOAT:
					case GPU_VEC2:
					case GPU_VEC3:
					case GPU_VEC4:
					case GPU_MAT3:
					case GPU_MAT4:
					case GPU_ATTRIB:
						break;
				}
			}
			else {
				uniform->type = input->dynamictype;
				BLI_strncpy(uniform->varname, input->shadername, sizeof(uniform->varname));
				switch (input->type) {
					case GPU_FLOAT:
						uniform->datatype = GPU_DATA_1F;
						break;
					case GPU_VEC2:
						uniform->datatype = GPU_DATA_2F;
						break;
					case GPU_VEC3:
						uniform->datatype = GPU_DATA_3F;
						break;
					case GPU_VEC4:
						uniform->datatype = GPU_DATA_4F;
						break;
					case GPU_MAT3:
						uniform->datatype = GPU_DATA_9F;
						break;
					case GPU_MAT4:
						uniform->datatype = GPU_DATA_16F;
						break;

					case GPU_NONE:
					case GPU_TEX2D:
					case GPU_TEXCUBE:
					case GPU_SHADOW2D:
					case GPU_ATTRIB:
						break;
				}

				if (GPU_DYNAMIC_GROUP_FROM_TYPE(uniform->type) == GPU_DYNAMIC_GROUP_LAMP)
					uniform->lamp = input->dynamicdata;

				if (GPU_DYNAMIC_GROUP_FROM_TYPE(uniform->type) == GPU_DYNAMIC_GROUP_MAT)
					uniform->material = input->dynamicdata;
			}

			if (uniform->type != GPU_DYNAMIC_NONE)
				BLI_addtail(&shader->uniforms, uniform);
			else
				MEM_freeN(uniform);
		}

		/* process builtin uniform */
		for (int i = 0; builtins[i].gputype; i++) {
			if (mat->builtins & builtins[i].gputype) {
				GPUInputUniform *uniform = MEM_callocN(sizeof(GPUInputUniform), "GPUInputUniform");
				uniform->type = builtins[i].dynamictype;
				uniform->datatype = builtins[i].datatype;
				BLI_strncpy(uniform->varname, GPU_builtin_name(builtins[i].gputype), sizeof(uniform->varname));
				BLI_addtail(&shader->uniforms, uniform);
			}
		}

		/* now link fragment shader with library shader */
		/* TBD: remove the function that are not used in the main function */
		liblen = (pass->libcode) ? strlen(pass->libcode) : 0;
		fraglen = strlen(pass->fragmentcode);
		shader->fragment = (char *)MEM_mallocN(liblen + fraglen + 1, "GPUFragShader");
		if (pass->libcode)
			memcpy(shader->fragment, pass->libcode, liblen);
		memcpy(&shader->fragment[liblen], pass->fragmentcode, fraglen);
		shader->fragment[liblen + fraglen] = 0;

		// export the attribute
		for (int i = 0; i < mat->attribs.totlayer; i++) {
			GPUInputAttribute *attribute = MEM_callocN(sizeof(GPUInputAttribute), "GPUInputAttribute");
			attribute->type = mat->attribs.layer[i].type;
			attribute->number = mat->attribs.layer[i].glindex;
			BLI_snprintf(attribute->varname, sizeof(attribute->varname), "att%d", mat->attribs.layer[i].attribid);

			switch (attribute->type) {
				case CD_TANGENT:
					attribute->datatype = GPU_DATA_4F;
					break;
				case CD_MTFACE:
					attribute->datatype = GPU_DATA_2F;
					attribute->name = mat->attribs.layer[i].name;
					break;
				case CD_MCOL:
					attribute->datatype = GPU_DATA_4UB;
					attribute->name = mat->attribs.layer[i].name;
					break;
				case CD_ORCO:
					attribute->datatype = GPU_DATA_3F;
					break;
			}

			if (attribute->datatype != GPU_DATA_NONE)
				BLI_addtail(&shader->attributes, attribute);
			else
				MEM_freeN(attribute);
		}

		/* export the vertex shader */
		shader->vertex = BLI_strdup(pass->vertexcode);
	}

	return shader;
}

void GPU_free_shader_export(GPUShaderExport *shader)
{
	if (shader == NULL)
		return;

	for (GPUInputUniform *uniform = shader->uniforms.first; uniform; uniform = uniform->next)
		if (uniform->texpixels)
			MEM_freeN(uniform->texpixels);

	BLI_freelistN(&shader->uniforms);
	BLI_freelistN(&shader->attributes);

	if (shader->vertex)
		MEM_freeN(shader->vertex);
	if (shader->fragment)
		MEM_freeN(shader->fragment);

	MEM_freeN(shader);
}

#ifdef WITH_OPENSUBDIV
void GPU_material_update_fvar_offset(GPUMaterial *gpu_material,
                                     DerivedMesh *dm)
{
	GPUPass *pass = gpu_material->pass;
	GPUShader *shader = (pass != NULL ? pass->shader : NULL);
	ListBase *inputs = (pass != NULL ? &pass->inputs : NULL);
	GPUInput *input;

	if (shader == NULL) {
		return;
	}

	GPU_shader_bind(shader);

	for (input = inputs->first;
	     input != NULL;
	     input = input->next)
	{
		if (input->source == GPU_SOURCE_ATTRIB &&
		    input->attribtype == CD_MTFACE)
		{
			char name[64];
			/* TODO(sergey): This will work for until names are
			 * consistent, we'll need to solve this somehow in the future.
			 */
			int layer_index;
			int location;

			if (input->attribname[0] != '\0') {
				layer_index = CustomData_get_named_layer(&dm->loopData,
				                                         CD_MLOOPUV,
				                                         input->attribname);
			}
			else {
				layer_index = CustomData_get_active_layer(&dm->loopData,
				                                          CD_MLOOPUV);
			}

			BLI_snprintf(name, sizeof(name),
			             "fvar%d_offset",
			             input->attribid);
			location = GPU_shader_get_uniform(shader, name);
			GPU_shader_uniform_int(shader, location, layer_index);
		}
	}

	GPU_shader_unbind();
}
#endif
