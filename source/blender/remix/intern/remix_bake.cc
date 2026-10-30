/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file remix_bake.cc
 *  \ingroup remix
 *
 * BGE-Remix P8.6: procedural textures baked into images for RTX Remix
 * (see BRMX_bake.h). The material is drawn in UV space into an offscreen
 * buffer, once per value.
 * P8.7 (OpenGL 4.5): each value stays on the graphics card in a texture;
 * compute shaders pad the seams and make Remix's images from the values;
 * only those images are read back. A bake is keyed by its content, and a
 * second start finds its files in the cache without drawing.
 */

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "BRMX_bake.h"
#include "BRMX_material.h"

#include "GPU_glew.h"

extern "C" {
#include "MEM_guardedalloc.h"

#include "DNA_customdata_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_texture_types.h"

#include "BLI_math.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_DerivedMesh.h"
#include "BKE_idprop.h"
#include "BKE_material_physical.h"
#include "BKE_mesh.h"

#include "BKE_customdata.h"

#include "GPU_framebuffer.h"
#include "GPU_glcheck.h"
#include "GPU_layer.h"
#include "GPU_material.h"
}

namespace brmx {

namespace {

const int DILATE_PASSES = 16;

uint64_t fnv(const void *data, size_t n, uint64_t h)
{
	const unsigned char *p = (const unsigned char *)data;
	for (size_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= 1099511628211ull;
	}
	return h;
}

template<typename T> uint64_t fnv_v(const T &v, uint64_t h)
{
	return fnv(&v, sizeof(v), h);
}

float srgb_to_linear(float c)
{
	return (c < 0.04045f) ? ((c < 0.0f) ? 0.0f : c * (1.0f / 12.92f)) : powf((c + 0.055f) * (1.0f / 1.055f), 2.4f);
}

float linear_to_srgb(float c)
{
	return (c < 0.0031308f) ? ((c < 0.0f) ? 0.0f : c * 12.92f) : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

unsigned char to_byte(float v)
{
	return (unsigned char)std::max(0, std::min(255, (int)lroundf(v * 255.0f)));
}

/* --------------------------------------------------------------------- */
/* drawing in UV space */

int g_slot = 0;
GPUMaterial *g_material = NULL;

/* for tests (BGE_REMIX_TRACE): each step of a bake, printed and flushed, so
 * a crash shows where it happened */
void trace(const char *step, const std::string &what)
{
	static int on = -1;
	if (on < 0) {
		const char *env = getenv("BGE_REMIX_TRACE");
		on = (env && env[0]) ? 1 : 0;
	}
	if (on) {
		printf("BGE-Remix bake: %s %s\n", step, what.c_str());
		fflush(stdout);
	}
}

/* The game engine leaves its own vertex arrays enabled (and pointing into
 * its buffers) between draws; a bake draws with Blender's DerivedMesh code,
 * so it starts from no arrays and no buffers, and puts the game's back. */
struct CleanArrays {
	GLint array_buffer, element_buffer, client_texture;
	CleanArrays()
	{
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
		glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &element_buffer);
		glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &client_texture);
		glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		GLint attribs = 0;
		glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &attribs);
		for (GLint i = 0; i < attribs && i < 32; i++) {
			glDisableVertexAttribArray(i);
		}
		GLint units = 0;
		glGetIntegerv(GL_MAX_TEXTURE_COORDS, &units);
		for (GLint i = 0; i < units && i < 8; i++) {
			glClientActiveTexture(GL_TEXTURE0 + i);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}
		glClientActiveTexture(GL_TEXTURE0);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
	}
	~CleanArrays()
	{
		glPopClientAttrib();
		glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
		glClientActiveTexture(client_texture);
	}
};

int set_material(int matnr, void *attribs)
{
	if (matnr != g_slot || !g_material) {
		return 0;
	}
	GPU_material_vertex_attributes(g_material, (GPUVertexAttribs *)attribs);
	return 1;
}

/* --------------------------------------------------------------------- */
/* P8.7: on the graphics card */

#define BAKE_CHANNELS 6 /* GPUBakeChannel */

/* the seams padded: each empty pixel next to drawn ones takes their mean
 * (integer, as dilate() below), one ring per pass */
const char *PAD_SRC =
        "layout(local_size_x = 8, local_size_y = 8) in;\n"
        "layout(rgba8, binding = 0) readonly uniform image2D src;\n"
        "layout(rgba8, binding = 1) writeonly uniform image2D dst;\n"
        "uniform int size;\n"
        "void main()\n"
        "{\n"
        "	ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "	if (p.x >= size || p.y >= size) return;\n"
        "	vec4 c = imageLoad(src, p);\n"
        "	if (c.a != 0.0) { imageStore(dst, p, c); return; }\n"
        "	ivec3 sum = ivec3(0);\n"
        "	int n = 0;\n"
        "	for (int dy = -1; dy <= 1; dy++) {\n"
        "		for (int dx = -1; dx <= 1; dx++) {\n"
        "			ivec2 q = p + ivec2(dx, dy);\n"
        "			if ((dx != 0 || dy != 0) && q.x >= 0 && q.y >= 0 && q.x < size && q.y < size) {\n"
        "				vec4 v = imageLoad(src, q);\n"
        "				if (v.a != 0.0) { sum += ivec3(round(v.rgb * 255.0)); n++; }\n"
        "			}\n"
        "		}\n"
        "	}\n"
        "	imageStore(dst, p, n > 0 ? vec4(vec3(sum / n) / 255.0, 1.0) : c);\n"
        "}\n";

/* Remix's images from the values (mode: the BakeSlot), as the CPU did it */
const char *PACK_SRC =
        "layout(local_size_x = 8, local_size_y = 8) in;\n"
        "layout(rgba8, binding = 0) readonly uniform image2D v_color;\n"
        "layout(rgba8, binding = 1) readonly uniform image2D v_alpha;\n"
        "layout(rgba8, binding = 2) readonly uniform image2D v_hardness;\n"
        "layout(rgba8, binding = 3) readonly uniform image2D v_emit;\n"
        "layout(rgba8, binding = 4) readonly uniform image2D v_normal;\n"
        "layout(rgba8, binding = 5) readonly uniform image2D v_metallic;\n"
        "layout(rgba8, binding = 6) writeonly uniform image2D result;\n"
        "uniform int size;\n"
        "uniform int mode;\n"
        "uniform int physical;\n"
        "uniform int spec_zero;\n"
        "uniform int alpha_textured;\n"
        "uniform vec3 emission;\n"
        "float byte_of(float v) { return round(clamp(v, 0.0, 1.0) * 255.0) / 255.0; }\n"
        "float to_linear(float c) { return (c < 0.04045) ? ((c < 0.0) ? 0.0 : c / 12.92) : pow((c + 0.055) / 1.055, 2.4); }\n"
        "float to_srgb(float c) { return (c < 0.0031308) ? ((c < 0.0) ? 0.0 : c * 12.92) : 1.055 * pow(c, 1.0 / 2.4) - 0.055; }\n"
        "void main()\n"
        "{\n"
        "	ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "	if (p.x >= size || p.y >= size) return;\n"
        "	vec4 o = vec4(1.0);\n"
        "	if (mode == 0) {\n"
        "		vec4 c = imageLoad(v_color, p);\n"
        "		o = vec4(c.rgb, alpha_textured != 0 ? imageLoad(v_alpha, p).r : 1.0);\n"
        "	}\n"
        "	else if (mode == 1) {\n"
        "		float v = imageLoad(v_hardness, p).r;\n"
        "		float r = v;\n"
        "		if (physical == 0) {\n"
        "			float har = round(v * 255.0) * (511.0 / 255.0);\n"
        "			r = (spec_zero != 0) ? 1.0 : pow(2.0 / (max(har, 1.0) + 2.0), 0.25);\n"
        "		}\n"
        "		o.rgb = vec3(r);\n"
        "	}\n"
        "	else if (mode == 2) {\n"
        "		float e = imageLoad(v_emit, p).r;\n"
        "		vec3 col = emission;\n"
        "		if (physical == 0) {\n"
        "			vec3 c = imageLoad(v_color, p).rgb;\n"
        "			col = vec3(to_linear(c.r), to_linear(c.g), to_linear(c.b));\n"
        "		}\n"
        "		o.rgb = vec3(to_srgb(col.r * e), to_srgb(col.g * e), to_srgb(col.b * e));\n"
        "	}\n"
        "	else if (mode == 3) {\n"
        "		vec4 n = imageLoad(v_normal, p);\n"
        "		o.rgb = (n.a != 0.0) ? n.rgb : vec3(128.0, 128.0, 255.0) / 255.0;\n"
        "	}\n"
        "	else {\n"
        "		o.rgb = vec3(imageLoad(v_metallic, p).r);\n"
        "	}\n"
        "	imageStore(result, p, vec4(byte_of(o.r), byte_of(o.g), byte_of(o.b), byte_of(o.a)));\n"
        "}\n";

GLuint g_pad = 0, g_pack = 0;
bool g_compute_failed = false;
GPUTimer *g_timer = NULL;

bool compute_ready()
{
	if (!g_pad && !g_compute_failed) {
		g_pad = GPU_compute_program(PAD_SRC, "BGE-Remix bake padding");
		g_pack = GPU_compute_program(PACK_SRC, "BGE-Remix bake images");
		if (!g_pad || !g_pack) {
			g_compute_failed = true;
			printf("BGE-Remix: bakes are finished on the CPU (no compute shaders)\n");
		}
	}
	return !g_compute_failed;
}

GLuint make_texture(int size, const char *label)
{
	GLuint t = 0;
	glCreateTextures(GL_TEXTURE_2D, 1, &t);
	glTextureStorage2D(t, 1, GL_RGBA8, size, size);
	glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	GPU_debug_label(GL_TEXTURE, t, label);
	return t;
}

/* what a bake draws into: one texture per value, one to pad through */
struct CardBake {
	int size;
	GPUOffScreen *ofs;
	GLuint chan[BAKE_CHANNELS];
	GLuint spare;
	CardBake() : size(0), ofs(NULL), spare(0)
	{
		memset(chan, 0, sizeof(chan));
	}
	~CardBake()
	{
		for (int i = 0; i < BAKE_CHANNELS; i++) {
			if (chan[i]) {
				glDeleteTextures(1, &chan[i]);
			}
		}
		if (spare) {
			glDeleteTextures(1, &spare);
		}
		if (ofs) {
			GPU_offscreen_free(ofs);
		}
	}
};

/* One value of the material over its UV map, into card.chan[channel]: RGBA,
 * alpha 1 where the surface was drawn. */
bool render_value(Scene *scene, Material *ma, DerivedMesh *dm, int slot, const float obmat[4][4], CardBake &card,
                  int channel, float scale, std::string &r_error)
{
	const int size = card.size;
	trace("shader", std::to_string(channel));
	GPUMaterial *mat = GPU_material_bake_create(scene, ma, channel, scale);
	if (!mat) {
		r_error = "its shader for baking could not be built";
		return false;
	}
	if (!card.ofs) {
		char err[256] = "";
		trace("offscreen", std::to_string(size));
		card.ofs = GPU_offscreen_create(size, size, 0, err);
		if (!card.ofs) {
			GPU_material_bake_free(mat);
			r_error = std::string("no offscreen buffer for baking (") + err + ")";
			return false;
		}
	}

	CleanArrays arrays;
	GPU_offscreen_bind(card.ofs, true);
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glViewport(0, 0, size, size);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_LIGHTING);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_TEXTURE);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadMatrixf(&obmat[0][0]);

	/* the view is the world: Global and Object coordinates as placed */
	float view[4][4], viewinv[4][4], ob[4][4];
	float obcol[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	unit_m4(view);
	unit_m4(viewinv);
	copy_m4_m4(ob, (float (*)[4])obmat);
	trace("bind", "");
	GPU_material_bind(mat, ~0, ~0, 0.0, 1, view, viewinv, NULL, false);
	GPU_material_bind_uniforms(mat, ob, view, obcol, 1.0f, NULL, NULL);
	g_slot = slot;
	g_material = mat;
	trace("draw", std::to_string(slot));
	dm->drawMappedFacesGLSL(dm, set_material, NULL, NULL);
	g_material = NULL;
	GPU_material_unbind(mat);

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_TEXTURE);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopAttrib();
	GPU_offscreen_unbind(card.ofs, true);

	/* kept on the card */
	if (!card.chan[channel]) {
		card.chan[channel] = make_texture(size, "BGE-Remix bake value");
	}
	glCopyImageSubData((GLuint)GPU_offscreen_color_texture(card.ofs), GL_TEXTURE_2D, 0, 0, 0, 0,
	                   card.chan[channel], GL_TEXTURE_2D, 0, 0, 0, 0, size, size, 1);
	GPU_material_bake_free(mat);
	trace("done", "");
	return true;
}

/* a value's pixels read back (bottom row first), for the CPU's finish */
void read_value(const CardBake &card, int channel, std::vector<unsigned char> &r_px)
{
	r_px.assign((size_t)card.size * card.size * 4, 0);
	if (card.chan[channel]) {
		GLint pack = 0;
		glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glGetTextureImage(card.chan[channel], 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)r_px.size(), r_px.data());
		glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)pack);
	}
}

/* the seams of a value padded on the card (as dilate() below) */
void pad_on_card(CardBake &card, int channel)
{
	if (!card.spare) {
		card.spare = make_texture(card.size, "BGE-Remix bake padding");
	}
	GLuint a = card.chan[channel], b = card.spare;
	glUseProgram(g_pad);
	glUniform1i(glGetUniformLocation(g_pad, "size"), card.size);
	for (int pass = 0; pass < DILATE_PASSES; pass++) {
		glBindImageTexture(0, a, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
		glBindImageTexture(1, b, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
		GPU_compute_dispatch_2d(card.size, card.size, 8);
		std::swap(a, b);
	}
	card.chan[channel] = a;
	card.spare = b;
	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
	glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
}

struct PackParams {
	bool physical, spec_zero, alpha_textured;
	float emission[3];
};

std::shared_ptr<BakedImage> make_image(const std::string &name, int size);

/* one of Remix's images, made on the card and read back */
std::shared_ptr<BakedImage> pack_on_card(const CardBake &card, int slot, const PackParams &pp,
                                         const std::string &name)
{
	const int size = card.size;
	GLuint out = make_texture(size, "BGE-Remix bake image");
	glUseProgram(g_pack);
	glUniform1i(glGetUniformLocation(g_pack, "size"), size);
	glUniform1i(glGetUniformLocation(g_pack, "mode"), slot);
	glUniform1i(glGetUniformLocation(g_pack, "physical"), pp.physical ? 1 : 0);
	glUniform1i(glGetUniformLocation(g_pack, "spec_zero"), pp.spec_zero ? 1 : 0);
	glUniform1i(glGetUniformLocation(g_pack, "alpha_textured"), pp.alpha_textured ? 1 : 0);
	glUniform3fv(glGetUniformLocation(g_pack, "emission"), 1, pp.emission);
	for (int c = 0; c < BAKE_CHANNELS; c++) {
		if (card.chan[c]) {
			glBindImageTexture((GLuint)c, card.chan[c], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
		}
	}
	glBindImageTexture(6, out, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	GPU_compute_dispatch_2d(size, size, 8);
	for (int c = 0; c <= 6; c++) {
		glBindImageTexture((GLuint)c, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
	}
	std::shared_ptr<BakedImage> img = make_image(name, size);
	GLint pack = 0;
	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glGetTextureImage(out, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)img->rgba.size(), img->rgba.data());
	glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)pack);
	glDeleteTextures(1, &out);
	return img;
}

/* Spread the drawn pixels into the empty ones around them, so texture
 * filtering at the UV islands' edges does not pick up the background
 * (the CPU's way, when compute shaders are not there). */
void dilate(std::vector<unsigned char> &px, int w, int h)
{
	std::vector<unsigned char> next;
	for (int pass = 0; pass < DILATE_PASSES; pass++) {
		next = px;
		bool changed = false;
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				unsigned char *p = &next[((size_t)y * w + x) * 4];
				if (px[((size_t)y * w + x) * 4 + 3] != 0) {
					continue;
				}
				int sum[3] = {0, 0, 0}, n = 0;
				for (int dy = -1; dy <= 1; dy++) {
					for (int dx = -1; dx <= 1; dx++) {
						const int nx = x + dx, ny = y + dy;
						if ((dx || dy) && nx >= 0 && ny >= 0 && nx < w && ny < h) {
							const unsigned char *q = &px[((size_t)ny * w + nx) * 4];
							if (q[3] != 0) {
								sum[0] += q[0];
								sum[1] += q[1];
								sum[2] += q[2];
								n++;
							}
						}
					}
				}
				if (n) {
					p[0] = (unsigned char)(sum[0] / n);
					p[1] = (unsigned char)(sum[1] / n);
					p[2] = (unsigned char)(sum[2] / n);
					p[3] = 255;
					changed = true;
				}
			}
		}
		px.swap(next);
		if (!changed) {
			break;
		}
	}
}

/* tests: BGE_REMIX_BAKE_DUMP=<folder> writes every baked image there as
 * <name>.ppm (RGB, top row first) */
void dump_image(const BakedImage &img)
{
	const char *dir = getenv("BGE_REMIX_BAKE_DUMP");
	if (!dir || !dir[0] || img.width <= 0 || img.rgba.size() < (size_t)img.width * img.height * 4) {
		return;
	}
	std::string name;
	for (char c : img.name) {
		name += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
	}
	const std::string path = std::string(dir) + "/" + name + ".ppm";
	FILE *f = fopen(path.c_str(), "wb");
	if (!f) {
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", img.width, img.height);
	for (int y = img.height - 1; y >= 0; y--) {
		for (int x = 0; x < img.width; x++) {
			fwrite(&img.rgba[((size_t)y * img.width + x) * 4], 1, 3, f);
		}
	}
	fclose(f);
}

std::shared_ptr<BakedImage> make_image(const std::string &name, int size)
{
	std::shared_ptr<BakedImage> img(new BakedImage());
	img->name = name;
	img->width = img->height = size;
	img->rgba.assign((size_t)size * size * 4, 255);
	return img;
}

bool is_bakeable(const Material *ma)
{
	if (!ma || (ma->use_nodes && ma->nodetree)) {
		return false;
	}
	/* the RTX Remix panel's own values come first (remix_material.cc) */
	return true;
}

}  /* namespace */

unsigned bake_slots(const Material *ma)
{
	unsigned slots = 0;
	if (!is_bakeable(ma)) {
		return 0;
	}
	const bool physical = BKE_material_is_physical(ma);
	for (int i = 0; i < MAX_MTEX; i++) {
		const MTex *mtex = ma->mtex[i];
		if (!mtex || !mtex->tex || (ma->septex & (1 << i)) || !GPU_texture_is_procedural(mtex->tex)) {
			continue;
		}
		if (mtex->mapto & (MAP_COL | MAP_ALPHA)) {
			slots |= 1u << BAKE_ALBEDO;
		}
		if (!physical && (mtex->mapto & MAP_REF)) {
			slots |= 1u << BAKE_ALBEDO;
		}
		if (physical && (mtex->mapto & MAP_RAYMIRR)) {
			slots |= 1u << BAKE_METALLIC;
		}
		if (mtex->mapto & MAP_HAR) {
			slots |= 1u << BAKE_ROUGHNESS;
		}
		if (mtex->mapto & MAP_EMIT) {
			slots |= 1u << BAKE_EMISSIVE;
		}
		if (mtex->mapto & MAP_NORM) {
			slots |= 1u << BAKE_NORMAL;
		}
	}
	return slots;
}

uint64_t bake_signature(const Material *ma)
{
	uint64_t h = 1469598103934665603ull;
	if (!ma) {
		return h;
	}
	h = fnv(&ma->r, sizeof(float) * 3, h);
	h = fnv_v(ma->alpha, h);
	h = fnv_v(ma->ref, h);
	h = fnv_v(ma->spec, h);
	h = fnv_v(ma->emit, h);
	h = fnv_v(ma->har, h);
	h = fnv_v(ma->mode, h);
	h = fnv_v(ma->septex, h);
	{
		/* P8.7: the physical values */
		MaterialPhysical ph;
		BKE_material_physical_get(ma, &ph);
		h = fnv(&ph, sizeof(ph), h);
		h = fnv_v(BKE_material_is_physical(ma), h);
	}
	for (int i = 0; i < MAX_MTEX; i++) {
		const MTex *mtex = ma->mtex[i];
		if (!mtex || !mtex->tex) {
			continue;
		}
		const Tex *tex = mtex->tex;
		h = fnv_v(i, h);
		h = fnv_v(mtex->texco, h);
		h = fnv_v(mtex->mapto, h);
		h = fnv_v(mtex->blendtype, h);
		h = fnv_v(mtex->texflag, h);
		h = fnv(mtex->size, sizeof(mtex->size), h);
		h = fnv(mtex->ofs, sizeof(mtex->ofs), h);
		h = fnv_v(mtex->projx, h);
		h = fnv_v(mtex->projy, h);
		h = fnv_v(mtex->projz, h);
		h = fnv(&mtex->r, sizeof(float) * 3, h);
		h = fnv_v(mtex->def_var, h);
		h = fnv_v(mtex->colfac, h);
		h = fnv_v(mtex->norfac, h);
		h = fnv_v(mtex->difffac, h);
		h = fnv_v(mtex->hardfac, h);
		h = fnv_v(mtex->alphafac, h);
		h = fnv_v(mtex->emitfac, h);
		h = fnv_v(mtex->object, h);
		if (mtex->object && mtex->texco == TEXCO_OBJECT) {
			h = fnv(mtex->object->obmat, sizeof(mtex->object->obmat), h);
		}
		h = fnv_v(tex->type, h);
		h = fnv_v(tex->stype, h);
		h = fnv_v(tex->flag, h);
		h = fnv_v(tex->noisesize, h);
		h = fnv_v(tex->turbul, h);
		h = fnv_v(tex->bright, h);
		h = fnv_v(tex->contrast, h);
		h = fnv_v(tex->saturation, h);
		h = fnv_v(tex->rfac, h);
		h = fnv_v(tex->gfac, h);
		h = fnv_v(tex->bfac, h);
		h = fnv_v(tex->mg_H, h);
		h = fnv_v(tex->mg_lacunarity, h);
		h = fnv_v(tex->mg_octaves, h);
		h = fnv_v(tex->mg_offset, h);
		h = fnv_v(tex->mg_gain, h);
		h = fnv_v(tex->dist_amount, h);
		h = fnv_v(tex->ns_outscale, h);
		h = fnv_v(tex->vn_w1, h);
		h = fnv_v(tex->vn_w2, h);
		h = fnv_v(tex->vn_w3, h);
		h = fnv_v(tex->vn_w4, h);
		h = fnv_v(tex->vn_mexp, h);
		h = fnv_v(tex->vn_distm, h);
		h = fnv_v(tex->vn_coltype, h);
		h = fnv_v(tex->noisedepth, h);
		h = fnv_v(tex->noisetype, h);
		h = fnv_v(tex->noisebasis, h);
		h = fnv_v(tex->noisebasis2, h);
		h = fnv_v(tex->nabla, h);
		h = fnv_v(tex->ima, h);
		h = fnv_v(bake_size(ma), h);
		if (tex->coba) {
			h = fnv_v(tex->coba->tot, h);
			h = fnv_v(tex->coba->ipotype, h);
			h = fnv_v(tex->coba->color_mode, h);
			h = fnv(tex->coba->data, sizeof(CBData) * std::max<int>(0, std::min<int>(tex->coba->tot, 32)), h);
		}
	}
	return h;
}

int bake_size(const Material *ma)
{
	const char *env = getenv("BGE_REMIX_BAKE_SIZE");
	int n = env ? atoi(env) : 0;
	if (n < 16 && ma && ma->id.properties) {
		const IDProperty *p = IDP_GetPropertyFromGroup(ma->id.properties, "bge_remix_bake_size");
		n = (p && p->type == IDP_INT) ? IDP_Int(p) : 0;
	}
	return (n >= 16 && n <= 8192) ? n : 1024;
}

DerivedMesh *bake_mesh_from(Mesh *me)
{
	if (!me || !me->totvert) {
		return NULL;
	}
	DerivedMesh *dm = CDDM_from_mesh(me);
	const int totvert = dm->getNumVerts(dm);
	float (*orco)[3] = (float (*)[3])MEM_mallocN(sizeof(float[3]) * totvert, "bake orco");
	dm->getVertCos(dm, orco);
	BKE_mesh_orco_verts_transform(me, orco, totvert, 0);
	DM_add_vert_layer(dm, CD_ORCO, CD_ASSIGN, orco);
	/* the material slots, as DM_update_materials sets them for an object:
	 * Blender's GLSL drawing sizes its tables by them (CDDM_from_mesh leaves
	 * them empty, and drawing then writes past an empty table) */
	dm->totmat = me->totcol + 1;
	dm->mat = (Material **)MEM_calloc_arrayN(dm->totmat, sizeof(*dm->mat), "bake DerivedMesh.mat");
	for (int i = 0; i < me->totcol; i++) {
		dm->mat[i] = me->mat ? me->mat[i] : NULL;
	}
	return dm;
}

namespace {

/* how each of Remix's images is asked of the texture cache */
TextureRequest slot_request(int slot)
{
	TextureRequest t;
	switch (slot) {
		case BAKE_ALBEDO: t.kind = BRB_TEXTURE_COLOR; break;
		case BAKE_EMISSIVE: t.kind = BRB_TEXTURE_COLOR; break;
		case BAKE_NORMAL: t.kind = BRB_TEXTURE_NORMAL; break;
		default: t.kind = BRB_TEXTURE_SCALAR; t.channel = 0; break; /* roughness, metallic */
	}
	return t;
}

const char *slot_suffix(int slot)
{
	switch (slot) {
		case BAKE_ALBEDO: return "_albedo";
		case BAKE_ROUGHNESS: return "_roughness";
		case BAKE_EMISSIVE: return "_emissive";
		case BAKE_NORMAL: return "_normal";
		default: return "_metallic";
	}
}

/* P8.7: everything a bake depends on: the material and its textures (the
 * signature), the mesh (its shape, polygons, UV maps), where it stands, the
 * slot and the way of baking */
uint64_t bake_key(uint64_t signature, const Material *ma, DerivedMesh *dm, int slot, const float obmat[4][4])
{
	static const char version[] = "BGE-Remix bake 2 (OpenGL 4.5)";
	uint64_t h = fnv(version, sizeof(version), 1469598103934665603ull);
	h = fnv_v(signature, h);
	h = fnv_v(slot, h);
	h = fnv(obmat, sizeof(float) * 16, h);
	const int nv = dm->getNumVerts(dm);
	if (nv > 0) {
		std::vector<float> cos((size_t)nv * 3);
		dm->getVertCos(dm, (float (*)[3])cos.data());
		h = fnv(cos.data(), cos.size() * sizeof(float), h);
	}
	const int np = dm->getNumPolys(dm), nl = dm->getNumLoops(dm);
	const MPoly *mp = dm->getPolyArray(dm);
	for (int i = 0; mp && i < np; i++) {
		h = fnv_v(mp[i].loopstart, h);
		h = fnv_v(mp[i].totloop, h);
		h = fnv_v(mp[i].mat_nr, h);
	}
	const MLoop *ml = dm->getLoopArray(dm);
	for (int i = 0; ml && i < nl; i++) {
		h = fnv_v(ml[i].v, h);
	}
	const int layers = CustomData_number_of_layers(&dm->loopData, CD_MLOOPUV);
	for (int n = 0; n < layers; n++) {
		const MLoopUV *uv = (const MLoopUV *)CustomData_get_layer_n(&dm->loopData, CD_MLOOPUV, n);
		const char *uvname = CustomData_get_layer_name(&dm->loopData, CD_MLOOPUV, n);
		if (uvname) {
			h = fnv(uvname, strlen(uvname), h);
		}
		for (int i = 0; uv && i < nl; i++) {
			h = fnv(uv[i].uv, sizeof(uv[i].uv), h);
		}
	}
	h = fnv_v(CustomData_get_active_layer(&dm->loopData, CD_MLOOPUV), h);
	for (int i = 0; i < MAX_MTEX; i++) {
		const MTex *mtex = ma->mtex[i];
		if (mtex && mtex->tex) {
			h = fnv(mtex->uvname, strlen(mtex->uvname), h);
		}
	}
	return h ? h : 1;
}

}  /* namespace */

bool bake_material(Scene *scene, Material *ma, DerivedMesh *dm, int slot, const float obmat[4][4],
                   const std::string &name, BakeResult &r)
{
	r = BakeResult();
	r.signature = bake_signature(ma);
	const unsigned slots = bake_slots(ma);
	if (!slots || !dm) {
		return false;
	}
	const bool physical = BKE_material_is_physical(ma);
	MaterialPhysical ph;
	BKE_material_physical_get(ma, &ph);
	if (!CustomData_has_layer(&dm->loopData, CD_MLOOPUV) && !CustomData_has_layer(&dm->faceData, CD_MTFACE)) {
		r.error = "the mesh has no UV map, so its procedural textures cannot be baked";
		return false;
	}
	const int size = bake_size(ma);
	trace("material", name);

	bool alpha_textured = false;
	for (int i = 0; i < MAX_MTEX; i++) {
		const MTex *mtex = ma->mtex[i];
		alpha_textured = alpha_textured || (mtex && mtex->tex && !(ma->septex & (1 << i)) &&
		                                    (mtex->mapto & MAP_ALPHA));
	}
	alpha_textured = alpha_textured && (slots & (1u << BAKE_ALBEDO));
	/* the most emit can reach: the material's, plus every emit slot's */
	float emit_scale = std::max(physical ? ph.emission_strength : ma->emit, 0.0f);
	for (int i = 0; i < MAX_MTEX; i++) {
		const MTex *mtex = ma->mtex[i];
		if (mtex && mtex->tex && !(ma->septex & (1 << i)) && (mtex->mapto & MAP_EMIT)) {
			emit_scale += std::fabs(mtex->emitfac);
		}
	}
	emit_scale = std::max(emit_scale, 1e-3f);
	r.has_alpha = alpha_textured;
	if (slots & (1u << BAKE_EMISSIVE)) {
		r.emissive_intensity = emit_scale;
	}

	/* P8.7: the same content baked before: its files are in the cache */
	const uint64_t key = bake_key(r.signature, ma, dm, slot, obmat);
	{
		bool all = true;
		std::shared_ptr<const BakedImage> found[BAKE_SLOTS];
		for (int s = 0; s < BAKE_SLOTS && all; s++) {
			if (!(slots & (1u << s))) {
				continue;
			}
			std::shared_ptr<BakedImage> img(new BakedImage());
			img->name = name + slot_suffix(s);
			img->width = img->height = size;
			img->key = key;
			img->cached = true;
			TextureRequest req = slot_request(s);
			req.baked = img;
			all = TextureCache::cached(req);
			found[s] = img;
		}
		if (all) {
			for (int s = 0; s < BAKE_SLOTS; s++) {
				r.images[s] = found[s];
			}
			r.from_cache = true;
			r.gpu_ms = 0.0;
			trace("from the cache", name);
			return true;
		}
	}

	/* which values to draw */
	const bool want_color = (slots & (1u << BAKE_ALBEDO)) || ((slots & (1u << BAKE_EMISSIVE)) && !physical);
	bool want[BAKE_CHANNELS] = {false};
	want[GPU_BAKE_COLOR] = want_color;
	want[GPU_BAKE_ALPHA] = alpha_textured;
	want[GPU_BAKE_HARDNESS] = (slots & (1u << BAKE_ROUGHNESS)) != 0;
	want[GPU_BAKE_EMIT] = (slots & (1u << BAKE_EMISSIVE)) != 0;
	want[GPU_BAKE_NORMAL] = (slots & (1u << BAKE_NORMAL)) != 0;
	want[GPU_BAKE_METALLIC] = (slots & (1u << BAKE_METALLIC)) != 0;

	const bool card_finish = compute_ready();
	if (!g_timer) {
		g_timer = GPU_timer_create("BGE-Remix bake");
	}
	GLint program = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	GPU_timer_begin(g_timer);
	CardBake card;
	card.size = size;
	for (int c = 0; c < BAKE_CHANNELS; c++) {
		if (!want[c]) {
			continue;
		}
		if (!render_value(scene, ma, dm, slot, obmat, card, c, c == GPU_BAKE_EMIT ? emit_scale : 1.0f, r.error)) {
			GPU_timer_end(g_timer);
			glUseProgram((GLuint)program);
			return false;
		}
		if (card_finish) {
			pad_on_card(card, c);
		}
	}

	if (card_finish) {
		/* Remix's images, made on the card */
		PackParams pp;
		pp.physical = physical;
		pp.spec_zero = ma->spec < 0.01f;
		pp.alpha_textured = alpha_textured;
		copy_v3_v3(pp.emission, ph.emission);
		for (int s = 0; s < BAKE_SLOTS; s++) {
			if (slots & (1u << s)) {
				std::shared_ptr<BakedImage> img = pack_on_card(card, s, pp, name + slot_suffix(s));
				img->key = key;
				r.images[s] = img;
			}
		}
		GPU_timer_end(g_timer);
		glUseProgram((GLuint)program);
	}
	else {
		/* the CPU's way: each value read back, padded and made into images */
		GPU_timer_end(g_timer);
		std::vector<unsigned char> color, value;
		if (want_color) {
			read_value(card, GPU_BAKE_COLOR, color);
		}
		if (slots & (1u << BAKE_ALBEDO)) {
			std::shared_ptr<BakedImage> img = make_image(name + "_albedo", size);
			std::vector<unsigned char> px = color;
			if (alpha_textured) {
				read_value(card, GPU_BAKE_ALPHA, value);
				dilate(value, size, size);
			}
			dilate(px, size, size);
			for (size_t i = 0; i < (size_t)size * size; i++) {
				img->rgba[i * 4 + 0] = px[i * 4 + 0];
				img->rgba[i * 4 + 1] = px[i * 4 + 1];
				img->rgba[i * 4 + 2] = px[i * 4 + 2];
				img->rgba[i * 4 + 3] = alpha_textured ? value[i * 4 + 0] : 255;
			}
			r.images[BAKE_ALBEDO] = img;
		}
		if (slots & (1u << BAKE_ROUGHNESS)) {
			read_value(card, GPU_BAKE_HARDNESS, value);
			dilate(value, size, size);
			std::shared_ptr<BakedImage> img = make_image(name + "_roughness", size);
			for (size_t i = 0; i < (size_t)size * size; i++) {
				float rough;
				if (physical) {
					rough = value[i * 4] / 255.0f;
				}
				else {
					const float har = value[i * 4] * (511.0f / 255.0f);
					rough = (ma->spec < 0.01f) ? 1.0f : BKE_material_hardness_to_roughness(har);
				}
				img->rgba[i * 4 + 0] = img->rgba[i * 4 + 1] = img->rgba[i * 4 + 2] = to_byte(rough);
			}
			r.images[BAKE_ROUGHNESS] = img;
		}
		if (slots & (1u << BAKE_METALLIC)) {
			read_value(card, GPU_BAKE_METALLIC, value);
			dilate(value, size, size);
			std::shared_ptr<BakedImage> img = make_image(name + "_metallic", size);
			for (size_t i = 0; i < (size_t)size * size; i++) {
				img->rgba[i * 4 + 0] = img->rgba[i * 4 + 1] = img->rgba[i * 4 + 2] = value[i * 4];
			}
			r.images[BAKE_METALLIC] = img;
		}
		if (slots & (1u << BAKE_EMISSIVE)) {
			read_value(card, GPU_BAKE_EMIT, value);
			dilate(value, size, size);
			std::vector<unsigned char> px = color;
			if (!physical) {
				dilate(px, size, size);
			}
			std::shared_ptr<BakedImage> img = make_image(name + "_emissive", size);
			for (size_t i = 0; i < (size_t)size * size; i++) {
				const float e = value[i * 4] / 255.0f;
				for (int c = 0; c < 3; c++) {
					const float col = physical ? ph.emission[c] : srgb_to_linear(px[i * 4 + c] / 255.0f);
					img->rgba[i * 4 + c] = to_byte(linear_to_srgb(col * e));
				}
			}
			r.images[BAKE_EMISSIVE] = img;
		}
		if (slots & (1u << BAKE_NORMAL)) {
			read_value(card, GPU_BAKE_NORMAL, value);
			dilate(value, size, size);
			std::shared_ptr<BakedImage> img = make_image(name + "_normal", size);
			for (size_t i = 0; i < (size_t)size * size; i++) {
				const bool drawn = value[i * 4 + 3] != 0;
				img->rgba[i * 4 + 0] = drawn ? value[i * 4 + 0] : 128;
				img->rgba[i * 4 + 1] = drawn ? value[i * 4 + 1] : 128;
				img->rgba[i * 4 + 2] = drawn ? value[i * 4 + 2] : 255;
			}
			r.images[BAKE_NORMAL] = img;
		}
		for (int s = 0; s < BAKE_SLOTS; s++) {
			if (r.images[s]) {
				const_cast<BakedImage *>(r.images[s].get())->key = key;
			}
		}
	}
	r.gpu_ms = GPU_timer_ms(g_timer);
	for (int i = 0; i < BAKE_SLOTS; i++) {
		if (r.images[i]) {
			dump_image(*r.images[i]);
		}
	}
	return true;
}

void apply_bake(const BakeResult &r, MaterialSpec &s)
{
	brb_material_desc &d = s.desc;
	for (int slot = 0; slot < BAKE_SLOTS; slot++) {
		if (!r.images[slot]) {
			continue;
		}
		TextureRequest t = slot_request(slot);
		t.baked = r.images[slot];
		switch (slot) {
			case BAKE_ALBEDO:
				s.textures[TEX_ALBEDO] = t;
				/* the bake holds the colour times the reflectivity */
				d.albedo[0] = d.albedo[1] = d.albedo[2] = 1.0f;
				if (r.has_alpha && (d.alpha_blend || d.alpha_test)) {
					d.opacity = 1.0f;
				}
				break;
			case BAKE_ROUGHNESS:
				s.textures[TEX_ROUGHNESS] = t;
				break;
			case BAKE_EMISSIVE:
				s.textures[TEX_EMISSIVE] = t;
				d.emissive[0] = d.emissive[1] = d.emissive[2] = 1.0f;
				d.emissive_intensity = r.emissive_intensity;
				break;
			case BAKE_NORMAL:
				s.textures[TEX_NORMAL] = t;
				break;
			case BAKE_METALLIC:
				s.textures[TEX_METALLIC] = t;
				break;
		}
	}
}

}  /* namespace brmx */
