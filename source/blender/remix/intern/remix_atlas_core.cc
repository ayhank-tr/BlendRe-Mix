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

/** \file remix_atlas_core.cc
 *  \ingroup remix
 *
 * BGE-Remix P10.4: the atlases' pure parts (see BRMX_atlas.h): their
 * layouts, the corner-matched tiles and the flipbook frames made from
 * bakes, the mipmaps, and the surfaces split along the UV grid. Nothing of
 * Blender's here.
 */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

#include "BRMX_atlas.h"
#include "BRMX_bake.h"

namespace brmx {

namespace {

/* the corner patterns of the atlas's grid, row by row from the bottom:
 * every 2 x 2 pattern of corner colours exactly once, wrapping round (found
 * by a search; P10.4) */
const unsigned char TORUS_2[16] = {
        0, 0, 0, 1,
        0, 0, 1, 0,
        1, 0, 1, 1,
        0, 1, 1, 1,
};

const unsigned char TORUS_3[81] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1,
        0, 0, 1, 0, 2, 1, 1, 2, 0,
        2, 0, 1, 0, 1, 0, 1, 0, 2,
        0, 1, 0, 1, 1, 1, 2, 0, 2,
        0, 2, 2, 1, 0, 2, 1, 1, 2,
        2, 1, 0, 2, 1, 1, 1, 1, 2,
        2, 0, 2, 2, 1, 2, 2, 1, 1,
        2, 1, 0, 1, 2, 2, 2, 2, 0,
        0, 1, 2, 2, 0, 2, 1, 1, 2,
};

/* the corners' places in a tile (south-west, south-east, north-east,
 * north-west) */
const float CORNER[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

float srgb_to_linear(float c)
{
	return (c <= 0.04045f) ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb(float c)
{
	c = std::min(std::max(c, 0.0f), 1.0f);
	return (c <= 0.0031308f) ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

unsigned char to_byte(float v)
{
	return (unsigned char)std::max(0, std::min(255, (int)lroundf(v * 255.0f)));
}

/* an image as floats, 4 a pixel: colour in linear light, a normal map as
 * its vectors (-1..1), anything else as it is (0..1) */
void decode(int kind, const unsigned char *rgba, size_t n, std::vector<float> &r)
{
	r.resize(n * 4);
	for (size_t i = 0; i < n * 4; i++) {
		const float v = rgba[i] / 255.0f;
		const int c = (int)(i & 3);
		if (kind == BRB_TEXTURE_COLOR && c < 3) {
			r[i] = srgb_to_linear(v);
		}
		else if (kind == BRB_TEXTURE_NORMAL && c < 3) {
			r[i] = v * 2.0f - 1.0f;
		}
		else {
			r[i] = v;
		}
	}
}

void normalize_px(float *p)
{
	const float l = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
	if (l > 1e-8f) {
		p[0] /= l;
		p[1] /= l;
		p[2] /= l;
	}
	else {
		p[0] = p[1] = 0.0f;
		p[2] = 1.0f;
	}
}

void encode(int kind, const float *px, size_t n, unsigned char *r)
{
	for (size_t i = 0; i < n; i++) {
		const float *p = px + i * 4;
		unsigned char *o = r + i * 4;
		if (kind == BRB_TEXTURE_NORMAL) {
			float v[3] = {p[0], p[1], p[2]};
			normalize_px(v);
			o[0] = to_byte(v[0] * 0.5f + 0.5f);
			o[1] = to_byte(v[1] * 0.5f + 0.5f);
			o[2] = to_byte(v[2] * 0.5f + 0.5f);
			o[3] = 255;
		}
		else if (kind == BRB_TEXTURE_COLOR) {
			o[0] = to_byte(linear_to_srgb(p[0]));
			o[1] = to_byte(linear_to_srgb(p[1]));
			o[2] = to_byte(linear_to_srgb(p[2]));
			o[3] = to_byte(p[3]);
		}
		else {
			for (int c = 0; c < 4; c++) {
				o[c] = to_byte(p[c]);
			}
		}
	}
}

/* a seamless image read at (x, y) in its pixels (their centres at + 0.5),
 * bilinear, wrapping round */
void sample_wrap(const std::vector<float> &px, int w, int h, float x, float y, float r[4])
{
	x -= 0.5f;
	y -= 0.5f;
	const float fx = floorf(x), fy = floorf(y);
	const float ax = x - fx, ay = y - fy;
	int x0 = (int)fx % w, y0 = (int)fy % h;
	if (x0 < 0) {
		x0 += w;
	}
	if (y0 < 0) {
		y0 += h;
	}
	const int x1 = (x0 + 1) % w, y1 = (y0 + 1) % h;
	const float *p00 = &px[((size_t)y0 * w + x0) * 4], *p10 = &px[((size_t)y0 * w + x1) * 4];
	const float *p01 = &px[((size_t)y1 * w + x0) * 4], *p11 = &px[((size_t)y1 * w + x1) * 4];
	for (int c = 0; c < 4; c++) {
		r[c] = (1.0f - ax) * (1.0f - ay) * p00[c] + ax * (1.0f - ay) * p10[c] + (1.0f - ax) * ay * p01[c] +
		       ax * ay * p11[c];
	}
}

/* the mean of each channel over several images */
void channel_means(const std::vector<std::vector<float> > &imgs, double r_mu[4])
{
	double sum[4] = {0.0, 0.0, 0.0, 0.0};
	size_t n = 0;
	for (size_t k = 0; k < imgs.size(); k++) {
		const std::vector<float> &p = imgs[k];
		for (size_t i = 0; i + 3 < p.size(); i += 4) {
			for (int c = 0; c < 4; c++) {
				sum[c] += p[i + c];
			}
			n++;
		}
	}
	for (int c = 0; c < 4; c++) {
		r_mu[c] = n ? sum[c] / (double)n : 0.0;
	}
}

/* several values blended by weights w (adding up to 1): random patterns
 * keep their contrast (the blend's spread scaled back by 1 / |w|); regular
 * ones are blended plainly (their patterns in step) */
void blend(int kind, bool keep_contrast, const float *const *v, const float *w, int n, const double mu[4],
           float r[4])
{
	float w2 = 0.0f;
	for (int k = 0; k < n; k++) {
		w2 += w[k] * w[k];
	}
	const float gain = (keep_contrast && w2 > 1e-8f) ? 1.0f / sqrtf(w2) : 1.0f;
	if (kind == BRB_TEXTURE_NORMAL) {
		/* the slopes (x, y) blended, z made up again */
		float x = 0.0f, y = 0.0f, z = 0.0f;
		for (int k = 0; k < n; k++) {
			x += w[k] * v[k][0];
			y += w[k] * v[k][1];
			z += w[k] * v[k][2];
		}
		if (keep_contrast) {
			x *= gain;
			y *= gain;
			const float l = x * x + y * y;
			if (l > 0.98f) {
				const float s = sqrtf(0.98f / l);
				x *= s;
				y *= s;
			}
			z = sqrtf(std::max(1.0f - x * x - y * y, 0.0f));
		}
		r[0] = x;
		r[1] = y;
		r[2] = z;
		r[3] = 1.0f;
		normalize_px(r);
		return;
	}
	for (int c = 0; c < 4; c++) {
		float s = 0.0f;
		for (int k = 0; k < n; k++) {
			s += w[k] * v[k][c];
		}
		if (keep_contrast && c < 3) {
			s = (float)mu[c] + (s - (float)mu[c]) * gain;
		}
		r[c] = std::min(std::max(s, 0.0f), 1.0f);
	}
}

/* the levels' sizes */
int level_size(int n, int l)
{
	return std::max(1, n >> l);
}

int level_count(int w, int h)
{
	int n = 1;
	while (w > 1 || h > 1) {
		w = std::max(w / 2, 1);
		h = std::max(h / 2, 1);
		n++;
	}
	return n;
}

/* averaged edges: the texels listed (several groups, each texel index
 * group-wise) set to their group's mean */
struct EdgeGroups {
	std::map<int, std::vector<std::vector<size_t> > > groups; /* class -> lists of texels, each the same length */
	void add(int cls, const std::vector<size_t> &texels)
	{
		groups[cls].push_back(texels);
	}
	void apply(std::vector<float> &px, bool normals)
	{
		for (std::map<int, std::vector<std::vector<size_t> > >::iterator it = groups.begin(); it != groups.end();
		     ++it)
		{
			std::vector<std::vector<size_t> > &lists = it->second;
			if (lists.empty()) {
				continue;
			}
			const size_t len = lists[0].size();
			for (size_t i = 0; i < len; i++) {
				float m[4] = {0.0f, 0.0f, 0.0f, 0.0f};
				int n = 0;
				for (size_t k = 0; k < lists.size(); k++) {
					if (i >= lists[k].size()) {
						continue;
					}
					const float *p = &px[lists[k][i] * 4];
					for (int c = 0; c < 4; c++) {
						m[c] += p[c];
					}
					n++;
				}
				if (!n) {
					continue;
				}
				for (int c = 0; c < 4; c++) {
					m[c] /= (float)n;
				}
				if (normals) {
					normalize_px(m);
				}
				for (size_t k = 0; k < lists.size(); k++) {
					if (i < lists[k].size()) {
						memcpy(&px[lists[k][i] * 4], m, sizeof(m));
					}
				}
			}
		}
		groups.clear();
	}
};

/* one level's edges averaged (see BRMX_atlas.h); l >= 1 */
void average_edges(const AtlasLayout &L, int kind, int l, int w, int h, std::vector<float> &px)
{
	const bool normals = kind == BRB_TEXTURE_NORMAL;
	EdgeGroups eg;
	if (L.kind == ATLAS_TILES) {
		const int P = L.pitch >> l;
		if (P < 2 || (L.gutter >> l) != 0) {
			return;
		}
		const int C = L.colours;
		/* the edges: west and east columns by (south, north) colours,
		 * south and north rows by (west, east) colours; then the corners
		 * by colour */
		for (int pass = 0; pass < 3; pass++) {
			for (size_t t = 0; t < L.cells.size(); t++) {
				int cn[4];
				atlas_tile_corners((int)t, C, cn);
				const int col = (L.cells[t].x - L.gutter) / L.pitch, row = (L.cells[t].y - L.gutter) / L.pitch;
				const int x0 = col * P, y0 = row * P;
				if (x0 + P > w || y0 + P > h) {
					continue;
				}
				std::vector<size_t> a, b;
				if (pass == 0) {
					for (int i = 1; i < P - 1; i++) {
						a.push_back((size_t)(y0 + i) * w + x0);
						b.push_back((size_t)(y0 + i) * w + x0 + P - 1);
					}
					eg.add(cn[0] * C + cn[3], a);
					eg.add(cn[1] * C + cn[2], b);
				}
				else if (pass == 1) {
					for (int i = 1; i < P - 1; i++) {
						a.push_back((size_t)y0 * w + x0 + i);
						b.push_back((size_t)(y0 + P - 1) * w + x0 + i);
					}
					eg.add(cn[0] * C + cn[1], a);
					eg.add(cn[3] * C + cn[2], b);
				}
				else {
					const size_t corner[4] = {(size_t)y0 * w + x0, (size_t)y0 * w + x0 + P - 1,
					                          (size_t)(y0 + P - 1) * w + x0 + P - 1, (size_t)(y0 + P - 1) * w + x0};
					for (int k = 0; k < 4; k++) {
						eg.add(cn[k], std::vector<size_t>(1, corner[k]));
					}
				}
			}
			eg.apply(px, normals);
		}
		return;
	}
	/* frames and repeating images: each cell's opposite edges agree */
	for (size_t k = 0; k < L.cells.size(); k++) {
		const AtlasCell &cell = L.cells[k];
		if (!cell.repeat) {
			continue;
		}
		if (L.kind == ATLAS_PACKED && (L.gutter >> l) != 0) {
			continue;
		}
		const int x0 = cell.x >> l, y0 = cell.y >> l;
		const int x1 = std::min((cell.x + cell.w) >> l, w), y1 = std::min((cell.y + cell.h) >> l, h);
		if (x1 - x0 < 2 || y1 - y0 < 2) {
			continue;
		}
		std::vector<size_t> a, b;
		for (int y = y0; y < y1; y++) {
			a.push_back((size_t)y * w + x0);
			b.push_back((size_t)y * w + x1 - 1);
		}
		eg.add((int)k * 2, a);
		eg.add((int)k * 2, b);
		eg.apply(px, normals);
		a.clear();
		b.clear();
		for (int x = x0; x < x1; x++) {
			a.push_back((size_t)y0 * w + x);
			b.push_back((size_t)(y1 - 1) * w + x);
		}
		eg.add((int)k * 2 + 1, a);
		eg.add((int)k * 2 + 1, b);
		eg.apply(px, normals);
	}
}

}  /* namespace */

/* ------------------------------------------------------------------------- */
/* tiles */

int atlas_tile_index(int sw, int se, int ne, int nw, int colours)
{
	return sw + colours * (se + colours * (ne + colours * nw));
}

void atlas_tile_corners(int tile, int colours, int r_corners[4])
{
	for (int k = 0; k < 4; k++) {
		r_corners[k] = tile % colours;
		tile /= colours;
	}
}

int atlas_corner_colour(int i, int j, unsigned seed, int colours)
{
	/* (bge_remix.atlas.corner_colour does the same in 32 bits) */
	uint32_t h = (uint32_t)i * 0x8da6b343u + (uint32_t)j * 0xd8163841u + (uint32_t)seed * 0xcb1ab31fu + 0x9e3779b9u;
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return colours > 0 ? (int)(h % (uint32_t)colours) : 0;
}

int atlas_square_tile(int i, int j, unsigned seed, int colours)
{
	return atlas_tile_index(atlas_corner_colour(i, j, seed, colours), atlas_corner_colour(i + 1, j, seed, colours),
	                        atlas_corner_colour(i + 1, j + 1, seed, colours),
	                        atlas_corner_colour(i, j + 1, seed, colours), colours);
}

const unsigned char *atlas_torus(int colours)
{
	return colours == 2 ? TORUS_2 : (colours == 3 ? TORUS_3 : NULL);
}

void atlas_tiles_layout(int colours, int pitch, unsigned seed, AtlasLayout &r)
{
	r = AtlasLayout();
	const unsigned char *g = atlas_torus(colours);
	if (!g || pitch < 8) {
		return;
	}
	const int n = colours * colours;
	r.kind = ATLAS_TILES;
	r.colours = colours;
	r.seed = seed;
	r.cols = r.rows = n;
	r.pitch = pitch;
	r.gutter = std::max(1, pitch / 32);
	r.width = r.height = n * pitch;
	r.cells.resize((size_t)n * n);
	for (int row = 0; row < n; row++) {
		for (int col = 0; col < n; col++) {
			const int sw = g[row * n + col], se = g[row * n + (col + 1) % n];
			const int ne = g[((row + 1) % n) * n + (col + 1) % n], nw = g[((row + 1) % n) * n + col];
			AtlasCell &cell = r.cells[atlas_tile_index(sw, se, ne, nw, colours)];
			cell.x = col * pitch + r.gutter;
			cell.y = row * pitch + r.gutter;
			cell.w = cell.h = pitch - 2 * r.gutter;
		}
	}
}

void atlas_flipbook_layout(int frames, int fps, int pitch, AtlasLayout &r)
{
	r = AtlasLayout();
	int side = 1;
	while (side * side < frames) {
		side++;
	}
	if (frames < 2 || side * side != frames || pitch < 8) {
		return;
	}
	r.kind = ATLAS_FLIPBOOK;
	r.frames = frames;
	r.fps = std::max(1, std::min(fps, 255));
	r.cols = r.rows = side;
	r.pitch = pitch;
	r.gutter = 0;
	r.width = r.height = side * pitch;
	r.cells.resize((size_t)frames);
	for (int k = 0; k < frames; k++) {
		/* frame k where Remix looks for it: column k % cols, row k / cols
		 * from the texture's first row (its bottom here) */
		r.cells[k].x = (k % side) * pitch;
		r.cells[k].y = (k / side) * pitch;
		r.cells[k].w = r.cells[k].h = pitch;
	}
}

bool atlas_compose_tiles(const AtlasLayout &L, int kind, bool regular, const std::vector<const BakedImage *> &src,
                         BakedImage &r_out)
{
	if (L.kind != ATLAS_TILES || (int)src.size() < L.colours) {
		return false;
	}
	const int sw = src[0] ? src[0]->width : 0, sh = src[0] ? src[0]->height : 0;
	std::vector<std::vector<float> > S(src.size());
	for (size_t k = 0; k < src.size(); k++) {
		if (!src[k] || src[k]->width != sw || src[k]->height != sh || sw <= 0 || sh <= 0 ||
		    src[k]->rgba.size() < (size_t)sw * sh * 4)
		{
			return false;
		}
		decode(kind, src[k]->rgba.data(), (size_t)sw * sh, S[k]);
	}
	double mu[4];
	channel_means(S, mu);
	const int P = L.pitch, g = L.gutter, c = P - 2 * g;
	const float gamma = regular ? 2.0f : 3.0f;
	/* each colour's tile read at the cell's pixels (the gap: past the edges,
	 * wrapping round), and the corners' weights there (the gap: those of the
	 * nearest edge point) */
	std::vector<std::vector<float> > R(src.size(), std::vector<float>((size_t)P * P * 4));
	std::vector<float> W((size_t)P * P * 4);
	for (int y = 0; y < P; y++) {
		for (int x = 0; x < P; x++) {
			const float px = (x - g + 0.5f) / (float)c, py = (y - g + 0.5f) / (float)c;
			for (size_t k = 0; k < src.size(); k++) {
				sample_wrap(S[k], sw, sh, px * sw, py * sh, &R[k][((size_t)y * P + x) * 4]);
			}
			const float qx = std::min(std::max(px, 0.0f), 1.0f), qy = std::min(std::max(py, 0.0f), 1.0f);
			float *w = &W[((size_t)y * P + x) * 4], sum = 0.0f;
			for (int k = 0; k < 4; k++) {
				const float a = std::max(0.0f, (1.0f - fabsf(qx - CORNER[k][0])) * (1.0f - fabsf(qy - CORNER[k][1])));
				w[k] = powf(a, gamma);
				sum += w[k];
			}
			for (int k = 0; k < 4; k++) {
				w[k] = sum > 0.0f ? w[k] / sum : 0.25f;
			}
		}
	}
	std::vector<float> out((size_t)L.width * L.height * 4, 0.0f);
	for (size_t t = 0; t < L.cells.size(); t++) {
		int cn[4];
		atlas_tile_corners((int)t, L.colours, cn);
		const int x0 = L.cells[t].x - g, y0 = L.cells[t].y - g;
		for (int y = 0; y < P; y++) {
			for (int x = 0; x < P; x++) {
				const size_t i = (size_t)y * P + x;
				const float *v[4];
				for (int k = 0; k < 4; k++) {
					v[k] = &R[cn[k]][i * 4];
				}
				blend(kind, !regular, v, &W[i * 4], 4, mu, &out[((size_t)(y0 + y) * L.width + (x0 + x)) * 4]);
			}
		}
	}
	r_out.width = L.width;
	r_out.height = L.height;
	r_out.rgba.assign((size_t)L.width * L.height * 4, 255);
	encode(kind, out.data(), (size_t)L.width * L.height, r_out.rgba.data());
	return true;
}

bool atlas_compose_flipbook(const AtlasLayout &L, int kind, const BakedImage &src, float shift_u, float shift_v,
                            BakedImage &r_out)
{
	const int sw = src.width, sh = src.height;
	if (L.kind != ATLAS_FLIPBOOK || sw <= 0 || sh <= 0 || src.rgba.size() < (size_t)sw * sh * 4) {
		return false;
	}
	/* BGE-Remix P10.4 (fix round 1): one bake, moved: frame k (t = k /
	 * frames) is the seamless tile moved by t x shift, cross-faded (its
	 * contrast kept) into the tile moved by (t - 1) x shift: frame 0 is the
	 * tile itself, and after the last frame the second copy has reached it,
	 * so the loop closes exactly (a flow-map loop). Before, each frame was
	 * baked twice: 31 bakes, 23 s */
	std::vector<float> S;
	decode(kind, src.rgba.data(), (size_t)sw * sh, S);
	std::vector<std::vector<float> > one(1, S);
	double mu[4];
	channel_means(one, mu);
	one.clear();
	std::vector<float> out((size_t)L.width * L.height * 4, 0.0f);
	const int P = L.pitch;
	for (int k = 0; k < L.frames; k++) {
		const float t = (float)k / (float)L.frames;
		const float w[2] = {1.0f - t, t};
		const float au = t * shift_u * sw, av = t * shift_v * sh;
		const float bu = (t - 1.0f) * shift_u * sw, bv = (t - 1.0f) * shift_v * sh;
		const AtlasCell &cell = L.cells[k];
		for (int y = 0; y < P; y++) {
			for (int x = 0; x < P; x++) {
				float va[4], vb[4] = {0.0f, 0.0f, 0.0f, 0.0f};
				const float sx = (x + 0.5f) / (float)P * sw, sy = (y + 0.5f) / (float)P * sh;
				sample_wrap(S, sw, sh, sx - au, sy - av, va);
				if (k > 0) {
					sample_wrap(S, sw, sh, sx - bu, sy - bv, vb);
				}
				const float *v[2] = {va, vb};
				blend(kind, true, v, w, k > 0 ? 2 : 1, mu, &out[((size_t)(cell.y + y) * L.width + cell.x + x) * 4]);
			}
		}
	}
	r_out.width = L.width;
	r_out.height = L.height;
	r_out.rgba.assign((size_t)L.width * L.height * 4, 255);
	encode(kind, out.data(), (size_t)L.width * L.height, r_out.rgba.data());
	return true;
}

/* ------------------------------------------------------------------------- */
/* mipmaps */

size_t atlas_levels_size(int width, int height, int count)
{
	size_t n = 0;
	for (int l = 0; l < count; l++) {
		n += (size_t)level_size(width, l) * level_size(height, l) * 4;
	}
	return n;
}

void atlas_mips(const AtlasLayout &L, int kind, const unsigned char *rgba, int width, int height,
                std::vector<unsigned char> &r_levels, int &r_count)
{
	r_count = level_count(width, height);
	r_levels.resize(atlas_levels_size(width, height, r_count));
	memcpy(r_levels.data(), rgba, (size_t)width * height * 4);
	std::vector<float> cur, next;
	decode(kind, rgba, (size_t)width * height, cur);
	size_t at = (size_t)width * height * 4;
	int w = width, h = height;
	for (int l = 1; l < r_count; l++) {
		const int nw = std::max(w / 2, 1), nh = std::max(h / 2, 1);
		next.assign((size_t)nw * nh * 4, 0.0f);
		for (int y = 0; y < nh; y++) {
			for (int x = 0; x < nw; x++) {
				float *o = &next[((size_t)y * nw + x) * 4];
				for (int dy = 0; dy < 2; dy++) {
					for (int dx = 0; dx < 2; dx++) {
						const int sx = std::min(x * 2 + dx, w - 1), sy = std::min(y * 2 + dy, h - 1);
						const float *p = &cur[((size_t)sy * w + sx) * 4];
						for (int c = 0; c < 4; c++) {
							o[c] += p[c] * 0.25f;
						}
					}
				}
				if (kind == BRB_TEXTURE_NORMAL) {
					normalize_px(o);
				}
			}
		}
		average_edges(L, kind, l, nw, nh, next);
		encode(kind, next.data(), (size_t)nw * nh, &r_levels[at]);
		at += (size_t)nw * nh * 4;
		cur.swap(next);
		w = nw;
		h = nh;
	}
}

/* ------------------------------------------------------------------------- */
/* splitting a surface along the UV grid */

namespace {

/* a point of a clipped triangle: its weights of the triangle's corners, and
 * the triangle's edge it lies on (ea, eb: its corners; ea == eb: on a
 * corner; -1: inside) */
struct PV {
	double b[3];
	int ea, eb;
};

struct Tri {
	const brb_vertex *v[3];
	double uv[3][2];
};

double coord(const Tri &t, const PV &p, int axis)
{
	return p.b[0] * t.uv[0][axis] + p.b[1] * t.uv[1][axis] + p.b[2] * t.uv[2][axis];
}

/* the original edge both points lie on, or false */
bool common_edge(const PV &p, const PV &q, int &r_a, int &r_b)
{
	int s[4], n = 0;
	const int lists[4] = {p.ea, p.eb, q.ea, q.eb};
	for (int k = 0; k < 4; k++) {
		if (lists[k] < 0) {
			return false;
		}
		bool seen = false;
		for (int i = 0; i < n; i++) {
			seen = seen || s[i] == lists[k];
		}
		if (!seen) {
			s[n++] = lists[k];
		}
	}
	if (n != 2) {
		return false;
	}
	r_a = s[0];
	r_b = s[1];
	return true;
}

/* vertex a before vertex b, whatever triangle they are in (so two
 * triangles sharing an edge cut it at exactly the same point) */
bool vertex_less(const brb_vertex &a, const brb_vertex &b)
{
	if (a.uv[0] != b.uv[0]) {
		return a.uv[0] < b.uv[0];
	}
	if (a.uv[1] != b.uv[1]) {
		return a.uv[1] < b.uv[1];
	}
	for (int k = 0; k < 3; k++) {
		if (a.position[k] != b.position[k]) {
			return a.position[k] < b.position[k];
		}
	}
	return false;
}

/* the polygon's part on one side of the line (axis) = k: keep_above */
void clip(const Tri &t, const std::vector<PV> &in, int axis, double k, bool keep_above, std::vector<PV> &out)
{
	out.clear();
	const size_t n = in.size();
	for (size_t i = 0; i < n; i++) {
		const PV &p = in[i], &q = in[(i + 1) % n];
		const double cp = coord(t, p, axis), cq = coord(t, q, axis);
		const bool pin = keep_above ? cp >= k : cp <= k;
		const bool qin = keep_above ? cq >= k : cq <= k;
		if (pin) {
			out.push_back(p);
		}
		if (pin != qin && ((cp < k && cq > k) || (cp > k && cq < k))) {
			PV x;
			int a, b;
			if (common_edge(p, q, a, b)) {
				/* on the triangle's edge: cut from its own corners, in an
				 * order that does not depend on the triangle */
				if (vertex_less(*t.v[b], *t.v[a])) {
					std::swap(a, b);
				}
				const double ua = t.uv[a][axis], ub = t.uv[b][axis];
				const double s = (ub != ua) ? (k - ua) / (ub - ua) : 0.0;
				x.b[0] = x.b[1] = x.b[2] = 0.0;
				x.b[a] = 1.0 - s;
				x.b[b] = s;
				x.ea = std::min(a, b);
				x.eb = std::max(a, b);
			}
			else {
				const double s = (k - cp) / (cq - cp);
				for (int c = 0; c < 3; c++) {
					x.b[c] = p.b[c] + s * (q.b[c] - p.b[c]);
				}
				x.ea = x.eb = -1;
			}
			out.push_back(x);
		}
	}
}

/* the lines the surface is cut along on one axis inside lo..hi, and so the
 * intervals between them */
void intervals(double lo, double hi, bool repeat, std::vector<double> &r)
{
	r.clear();
	r.push_back(lo);
	if (repeat) {
		for (double k = floor(lo) + 1.0; k < hi; k += 1.0) {
			if (k > lo) {
				r.push_back(k);
			}
		}
	}
	else {
		for (int k = 0; k <= 1; k++) {
			if (k > lo && k < hi) {
				r.push_back((double)k);
			}
		}
	}
	r.push_back(hi);
}

const size_t MAX_PIECES = 4096; /* squares one triangle may be cut into */

}  /* namespace */

bool atlas_split(const AtlasLayout &L, std::vector<brb_vertex> &v, std::vector<uint32_t> &idx,
                 const std::vector<int> *tri_pick, std::vector<uint32_t> *r_source,
                 std::vector<AtlasSquare> *r_squares)
{
	if ((L.kind != ATLAS_TILES && L.kind != ATLAS_PACKED) || L.cells.empty() || L.width <= 0 || L.height <= 0) {
		return false;
	}
	std::vector<brb_vertex> ov;
	std::vector<uint32_t> oi, src;
	ov.reserve(v.size());
	oi.reserve(idx.size());
	/* (vertex, cell, square) -> the vertex made for it */
	std::map<std::pair<uint64_t, uint64_t>, uint32_t> corners;
	std::map<std::pair<int, int>, AtlasSquare> squares;
	std::vector<PV> poly, tmp, strip;
	std::vector<double> us, vs;
	const int own = (L.kind == ATLAS_PACKED) ? std::max(0, std::min(L.own_cell, (int)L.cells.size() - 1)) : 0;
	for (size_t t = 0; t + 2 < idx.size(); t += 3) {
		Tri tri;
		bool ok = true;
		for (int k = 0; k < 3; k++) {
			ok = ok && idx[t + k] < v.size();
		}
		if (!ok) {
			continue;
		}
		double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300};
		for (int k = 0; k < 3; k++) {
			tri.v[k] = &v[idx[t + k]];
			for (int a = 0; a < 2; a++) {
				double c = tri.v[k]->uv[a];
				if (!(c == c) || fabs(c) > 1e6) {
					c = 0.0; /* (no UVs worth the name) */
				}
				tri.uv[k][a] = c;
				lo[a] = std::min(lo[a], c);
				hi[a] = std::max(hi[a], c);
			}
		}
		const int pick = (tri_pick && t / 3 < tri_pick->size()) ? (*tri_pick)[t / 3] : 0;
		/* the cell each square shows */
		const AtlasCell *cell_of_pick = (pick > 0 && pick <= (int)L.cells.size()) ? &L.cells[pick - 1] : NULL;
		const bool repeat = (L.kind == ATLAS_TILES) || L.cells[cell_of_pick ? pick - 1 : own].repeat;
		intervals(lo[0], hi[0], repeat, us);
		intervals(lo[1], hi[1], repeat, vs);
		const bool too_many = (us.size() - 1) * (vs.size() - 1) > MAX_PIECES;
		if (too_many) {
			/* (left whole, in the square of its middle) */
			us.resize(2);
			us[1] = hi[0];
			vs.resize(2);
			vs[1] = hi[1];
		}
		for (size_t iu = 0; iu + 1 < us.size(); iu++) {
			/* the strip between two lines on u */
			PV c0 = {{1, 0, 0}, 0, 0}, c1 = {{0, 1, 0}, 1, 1}, c2 = {{0, 0, 1}, 2, 2};
			poly.clear();
			poly.push_back(c0);
			poly.push_back(c1);
			poly.push_back(c2);
			if (!too_many) {
				if (iu > 0) {
					clip(tri, poly, 0, us[iu], true, tmp);
					poly.swap(tmp);
				}
				if (iu + 2 < us.size()) {
					clip(tri, poly, 0, us[iu + 1], false, tmp);
					poly.swap(tmp);
				}
			}
			if (poly.size() < 3) {
				continue;
			}
			for (size_t iv = 0; iv + 1 < vs.size(); iv++) {
				strip = poly;
				if (!too_many) {
					if (iv > 0) {
						clip(tri, strip, 1, vs[iv], true, tmp);
						strip.swap(tmp);
					}
					if (iv + 2 < vs.size()) {
						clip(tri, strip, 1, vs[iv + 1], false, tmp);
						strip.swap(tmp);
					}
				}
				if (strip.size() < 3) {
					continue;
				}
				/* the square: where the piece's middle is */
				double cu = 0.0, cv = 0.0;
				for (size_t k = 0; k < strip.size(); k++) {
					cu += coord(tri, strip[k], 0);
					cv += coord(tri, strip[k], 1);
				}
				cu /= (double)strip.size();
				cv /= (double)strip.size();
				const int si = repeat ? (int)floor(cu) : 0, sj = repeat ? (int)floor(cv) : 0;
				int cell_index;
				if (cell_of_pick) {
					cell_index = pick - 1;
				}
				else if (L.kind == ATLAS_TILES) {
					cell_index = atlas_square_tile(si, sj, L.seed, L.colours);
				}
				else {
					cell_index = own;
				}
				if (cell_index < 0 || cell_index >= (int)L.cells.size()) {
					cell_index = 0;
				}
				const AtlasCell &cell = L.cells[cell_index];
				AtlasSquare &sq = squares[std::make_pair(si, sj)];
				sq.i = si;
				sq.j = sj;
				sq.cell = cell_index;
				sq.picked = cell_of_pick != NULL;
				/* the piece's vertices */
				std::vector<uint32_t> made(strip.size());
				for (size_t k = 0; k < strip.size(); k++) {
					const PV &p = strip[k];
					int corner = -1;
					for (int c = 0; c < 3; c++) {
						if (p.b[c] == 1.0) {
							corner = c;
						}
					}
					const std::pair<uint64_t, uint64_t> ckey(
					        corner >= 0 ? (((uint64_t)idx[t + corner] << 32) | (uint64_t)(uint32_t)cell_index) : 0,
					        ((uint64_t)(uint32_t)si << 32) | (uint64_t)(uint32_t)sj);
					if (corner >= 0) {
						std::map<std::pair<uint64_t, uint64_t>, uint32_t>::iterator f = corners.find(ckey);
						if (f != corners.end()) {
							made[k] = f->second;
							continue;
						}
					}
					brb_vertex o;
					memset(&o, 0, sizeof(o));
					int dominant = 0;
					for (int c = 0; c < 3; c++) {
						if (p.b[c] > p.b[dominant]) {
							dominant = c;
						}
					}
					for (int a = 0; a < 3; a++) {
						o.position[a] = (float)(p.b[0] * tri.v[0]->position[a] + p.b[1] * tri.v[1]->position[a] +
						                        p.b[2] * tri.v[2]->position[a]);
						o.normal[a] = (float)(p.b[0] * tri.v[0]->normal[a] + p.b[1] * tri.v[1]->normal[a] +
						                      p.b[2] * tri.v[2]->normal[a]);
					}
					if (corner >= 0) {
						memcpy(o.position, tri.v[corner]->position, sizeof(o.position));
						memcpy(o.normal, tri.v[corner]->normal, sizeof(o.normal));
					}
					{
						const float l = sqrtf(o.normal[0] * o.normal[0] + o.normal[1] * o.normal[1] +
						                      o.normal[2] * o.normal[2]);
						if (l > 1e-8f) {
							for (int a = 0; a < 3; a++) {
								o.normal[a] /= l;
							}
						}
						else {
							memcpy(o.normal, tri.v[dominant]->normal, sizeof(o.normal));
						}
					}
					uint32_t col = 0;
					for (int ch = 0; ch < 4; ch++) {
						double s = 0.0;
						for (int c = 0; c < 3; c++) {
							s += p.b[c] * (double)((tri.v[c]->color >> (ch * 8)) & 0xFFu);
						}
						col |= (uint32_t)std::max(0, std::min(255, (int)floor(s + 0.5))) << (ch * 8);
					}
					o.color = col;
					/* the UV inside the square, moved into the cell */
					double fu = coord(tri, p, 0), fv = coord(tri, p, 1);
					fu = repeat ? fu - si : fu;
					fv = repeat ? fv - sj : fv;
					fu = std::min(std::max(fu, 0.0), 1.0);
					fv = std::min(std::max(fv, 0.0), 1.0);
					o.uv[0] = (float)((cell.x + fu * cell.w) / (double)L.width);
					o.uv[1] = (float)((cell.y + fv * cell.h) / (double)L.height);
					made[k] = (uint32_t)ov.size();
					ov.push_back(o);
					src.push_back(idx[t + dominant]);
					if (corner >= 0) {
						corners[ckey] = made[k];
					}
				}
				for (size_t k = 1; k + 1 < made.size(); k++) {
					oi.push_back(made[0]);
					oi.push_back(made[k]);
					oi.push_back(made[k + 1]);
				}
			}
		}
	}
	v.swap(ov);
	idx.swap(oi);
	if (r_source) {
		r_source->swap(src);
	}
	if (r_squares) {
		r_squares->clear();
		for (std::map<std::pair<int, int>, AtlasSquare>::const_iterator it = squares.begin(); it != squares.end();
		     ++it)
		{
			r_squares->push_back(it->second);
		}
	}
	return true;
}

}  /* namespace brmx */
