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

/** \file remix_atlas_pack.cc
 *  \ingroup remix
 *
 * BGE-Remix P10.4: small images packed into pages with xatlas (MIT,
 * extern/xatlas): each image a rectangle at its own size (no scaling, no
 * turning), each with `padding` pixels round it for its gap (so two
 * padding apart, and padding from the page's edges), on pages of `page` x
 * `page` pixels.
 * Used by bge_remix.atlas.pack_images through _bge_remix.atlas_pack. The
 * result is checked (inside the page, apart from the others); where xatlas
 * gives something else, a plain shelf packing is used instead.
 */

#include <algorithm>
#include <cmath>
#include <vector>

#include "xatlas.h"

extern "C" int BRMX_atlas_pack(const int *sizes, int count, int page, int padding, int *r_places);

namespace {

struct Place {
	int page, x, y;
};

/* every rectangle on its page, its gap inside it, apart from the others' gaps */
bool valid(const int *sizes, int count, int page, int padding, const std::vector<Place> &p)
{
	for (int i = 0; i < count; i++) {
		const int w = sizes[i * 2], h = sizes[i * 2 + 1];
		if (p[i].page < 0 || p[i].x < padding || p[i].y < padding || p[i].x + w + padding > page ||
		    p[i].y + h + padding > page)
		{
			return false;
		}
		for (int j = 0; j < i; j++) {
			if (p[j].page != p[i].page) {
				continue;
			}
			const int wj = sizes[j * 2], hj = sizes[j * 2 + 1];
			const int gap = 2 * padding;
			const bool apart = p[i].x >= p[j].x + wj + gap || p[j].x >= p[i].x + w + gap ||
			                   p[i].y >= p[j].y + hj + gap || p[j].y >= p[i].y + h + gap;
			if (!apart) {
				return false;
			}
		}
	}
	return true;
}

/* tallest first, in rows */
void shelves(const int *sizes, int count, int page, int padding, std::vector<Place> &p)
{
	std::vector<int> order(count);
	for (int i = 0; i < count; i++) {
		order[i] = i;
	}
	std::stable_sort(order.begin(), order.end(),
	                 [sizes](int a, int b) { return sizes[a * 2 + 1] > sizes[b * 2 + 1]; });
	int pg = 0, x = padding, y = padding, row = 0;
	for (int k = 0; k < count; k++) {
		const int i = order[k], w = sizes[i * 2], h = sizes[i * 2 + 1];
		if (x + w + padding > page) {
			x = padding;
			y += row + 2 * padding;
			row = 0;
		}
		if (y + h + padding > page) {
			pg++;
			x = y = padding;
			row = 0;
		}
		p[i].page = pg;
		p[i].x = x;
		p[i].y = y;
		x += w + 2 * padding;
		row = std::max(row, h);
	}
}

bool with_xatlas(const int *sizes, int count, int page, int padding, std::vector<Place> &p)
{
	xatlas::Atlas *atlas = xatlas::Create();
	std::vector<float> uv((size_t)count * 8);
	std::vector<uint32_t> index((size_t)count * 6), material((size_t)count * 2);
	for (int i = 0; i < count; i++) {
		const float w = (float)sizes[i * 2], h = (float)sizes[i * 2 + 1];
		const float q[8] = {0.0f, 0.0f, w, 0.0f, w, h, 0.0f, h};
		std::copy(q, q + 8, &uv[(size_t)i * 8]);
		const uint32_t b = (uint32_t)i * 4;
		const uint32_t t[6] = {b, b + 1, b + 2, b, b + 2, b + 3};
		std::copy(t, t + 6, &index[(size_t)i * 6]);
		/* (each image its own chart: they all overlap at the origin) */
		material[(size_t)i * 2] = material[(size_t)i * 2 + 1] = (uint32_t)i;
	}
	xatlas::UvMeshDecl decl;
	decl.vertexUvData = uv.data();
	decl.indexData = index.data();
	decl.faceMaterialData = material.data();
	decl.vertexCount = (uint32_t)count * 4;
	decl.vertexStride = sizeof(float) * 2;
	decl.indexCount = (uint32_t)index.size();
	decl.indexFormat = xatlas::IndexFormat::UInt32;
	bool ok = xatlas::AddUvMesh(atlas, decl) == xatlas::AddMeshError::Success;
	if (ok) {
		xatlas::ChartOptions co;
		xatlas::PackOptions po;
		po.padding = (uint32_t)padding;
		po.texelsPerUnit = 1.0f;
		/* (xatlas leaves no gap at the page's edges: the page made smaller
		 * by the gap, then moved in by it) */
		po.resolution = (uint32_t)(page - 2 * padding);
		po.bilinear = false;
		po.bruteForce = true;
		po.rotateCharts = false;
		po.rotateChartsToAxis = false;
		xatlas::Generate(atlas, co, po);
		ok = atlas->meshCount == 1 && atlas->meshes[0].vertexCount > 0;
	}
	if (ok) {
		/* each image's place: the lowest corner of its chart's vertices */
		std::vector<float> lx(count, 1e30f), ly(count, 1e30f), hx(count, -1e30f), hy(count, -1e30f);
		std::vector<int> pg(count, -1);
		const xatlas::Mesh &m = atlas->meshes[0];
		for (uint32_t v = 0; v < m.vertexCount; v++) {
			const xatlas::Vertex &vx = m.vertexArray[v];
			const int i = (int)(vx.xref / 4);
			if (i < 0 || i >= count || vx.atlasIndex < 0) {
				continue;
			}
			lx[i] = std::min(lx[i], vx.uv[0]);
			ly[i] = std::min(ly[i], vx.uv[1]);
			hx[i] = std::max(hx[i], vx.uv[0]);
			hy[i] = std::max(hy[i], vx.uv[1]);
			pg[i] = vx.atlasIndex;
		}
		for (int i = 0; i < count && ok; i++) {
			/* at its own size: anything else (scaled, turned) is refused */
			const float w = hx[i] - lx[i], h = hy[i] - ly[i];
			ok = pg[i] >= 0 && w > sizes[i * 2] - 1.5f && w < sizes[i * 2] + 1.5f && h > sizes[i * 2 + 1] - 1.5f &&
			     h < sizes[i * 2 + 1] + 1.5f;
			p[i].page = pg[i];
			p[i].x = (int)floorf(lx[i]) + padding;
			p[i].y = (int)floorf(ly[i]) + padding;
		}
	}
	xatlas::Destroy(atlas);
	return ok && valid(sizes, count, page, padding, p);
}

}  /* namespace */

/* r_places: page, x, y for each image (bottom-left corners, pixels);
 * returns the pages used, 0 when an image does not fit a page */
int BRMX_atlas_pack(const int *sizes, int count, int page, int padding, int *r_places)
{
	if (count <= 0 || page <= 0) {
		return 0;
	}
	for (int i = 0; i < count; i++) {
		if (sizes[i * 2] <= 0 || sizes[i * 2 + 1] <= 0 || sizes[i * 2] + 2 * padding > page ||
		    sizes[i * 2 + 1] + 2 * padding > page)
		{
			return 0;
		}
	}
	std::vector<Place> p((size_t)count);
	if (!with_xatlas(sizes, count, page, padding, p)) {
		shelves(sizes, count, page, padding, p);
		if (!valid(sizes, count, page, padding, p)) {
			return 0;
		}
	}
	int pages = 0;
	for (int i = 0; i < count; i++) {
		r_places[i * 3] = p[i].page;
		r_places[i * 3 + 1] = p[i].x;
		r_places[i * 3 + 2] = p[i].y;
		pages = std::max(pages, p[i].page + 1);
	}
	return pages;
}
