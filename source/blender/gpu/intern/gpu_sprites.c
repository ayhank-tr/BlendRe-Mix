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

/** \file blender/gpu/intern/gpu_sprites.c
 *  \ingroup gpu
 *
 * BGE-Remix P8 Part C (P8.20): halos and particles (GPU_sprites.h).
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"
#include "BLI_math.h"
#include "BLI_noise.h"

#include "DNA_object_force.h"
#include "DNA_particle_types.h"
#include "DNA_scene_types.h"

#include "GPU_glew.h"
#include "GPU_glcheck.h"
#include "GPU_layer.h"
#include "GPU_lights.h"
#include "GPU_sprites.h"

#include "gpu_private.h"

/* storage buffer bindings (the material shaders use 6..13) */
#define SPR_BIND_HASH 14
#define SPR_BIND_POINTS 15
#define SPR_BIND_PARTICLES 16
#define SPR_BIND_TRIS 17
#define SPR_BIND_COLLIDERS 18

#define SPR_MAX_COLLIDERS 16

/* ------------------------------------------------------------------------ */
/* shaders */

/* Blender Internal's halo, a pixel at (xn, yn) from its centre (pixels) */
static const char *halo_glsl =
        "layout(std430, binding = 14) readonly buffer BgeHashVect { float hv[]; };\n"
        "float hvf(int i) { return hv[((i % 768) + 768) % 768]; }\n"
        "vec4 halo_shade(float xn, float yn, float rad, vec3 rgb, float alpha, float hard, int ringc, int linec,\n"
        "                int starc, int seed, bool xalpha, bool flarecirc, vec3 linecol, vec3 ringcol)\n"
        "{\n"
        "	float dist = xn * xn + yn * yn;\n"
        "	float radsq = rad * rad;\n"
        "	if (dist >= radsq) return vec4(0.0);\n"
        "	float radist = sqrt(dist), ringf = 0.0, linef = 0.0;\n"
        "	int ofs = seed;\n"
        "	for (int a = ringc; a > 0; a--, ofs += 2) {\n"
        "		float fac = abs(hvf(ofs + 1) * (rad * abs(hvf(ofs)) - radist));\n"
        "		if (fac < 1.0) ringf += 1.0 - fac;\n"
        "	}\n"
        "	dist = dist / radsq;\n"
        "	if (flarecirc) dist = 0.5 + abs(dist - 0.5);\n"
        "	if (hard >= 30.0) {\n"
        "		dist = sqrt(dist);\n"
        "		if (hard >= 40.0) {\n"
        "			dist = sin(dist * 1.5707963);\n"
        "			if (hard >= 50.0) dist = sqrt(dist);\n"
        "		}\n"
        "	}\n"
        "	else if (hard < 20.0) dist *= dist;\n"
        "	dist = (dist < 1.0) ? 1.0 - dist : 0.0;\n"
        "	if (linec > 0) {\n"
        "		ofs = seed;\n"
        "		for (int a = linec; a > 0; a--, ofs += 3) {\n"
        "			float fac = abs(xn * hvf(ofs) + yn * hvf(ofs + 1));\n"
        "			if (fac < 1.0) linef += 1.0 - fac;\n"
        "		}\n"
        "		linef *= dist;\n"
        "	}\n"
        "	if (starc > 0) {\n"
        "		float angle = atan(yn, xn) * (1.0 + 0.25 * float(starc));\n"
        "		float co = cos(angle), si = sin(angle);\n"
        "		float ster = abs((co * xn + si * yn) * (co * yn - si * xn));\n"
        "		if (ster > 1.0) {\n"
        "			ster = rad / ster;\n"
        "			if (ster < 1.0) dist *= sqrt(ster);\n"
        "		}\n"
        "	}\n"
        "	if (dist <= 0.00001) return vec4(0.0);\n"
        "	dist *= alpha;\n"
        "	ringf *= dist;\n"
        "	linef *= alpha;\n"
        "	vec4 col = vec4(dist * rgb, xalpha ? dist * dist : dist);\n"
        "	if (linef != 0.0) { col.rgb += linef * linecol; col.a += xalpha ? linef * linef : linef; }\n"
        "	if (ringf != 0.0) { col.rgb += ringf * ringcol; col.a += xalpha ? ringf * ringf : ringf; }\n"
        "	col.a = min(col.a, 1.0);\n"
        "	return col;\n"
        "}\n";

/* the halos' sprites: points (object space) or particles, and the flares */
static const char *halo_vert =
        "layout(std430, binding = 15) readonly buffer BgeHaloPoints { vec4 pts[]; };\n"
        "layout(std430, binding = 16) readonly buffer BgeParticles { vec4 prt[]; };\n"
        "uniform int src;\n"          /* 0 points, 1 particles */
        "uniform int flares;\n"       /* 0 the halos, n: their flares (n sprites each) */
        "uniform mat4 model, view, proj;\n"
        "uniform vec4 vp;\n"          /* the viewport */
        "uniform float hasize;\n"
        "uniform vec4 h_col;\n"       /* rgb, alpha */
        "uniform vec4 h_par;\n"       /* add, hard, xalpha, - */
        "uniform ivec4 h_cnt;\n"      /* rings, lines, stars, seed */
        "uniform vec4 f_par;\n"       /* flare size, boost, sub size, seed2 */
        "uniform vec4 fade0, fade1;\n" /* particles: colour at birth and at death */
        "uniform float size_end;\n"
        "uniform sampler2D scenedepth;\n"
        "uniform float scenedepth_on;\n"
        "out vec2 v_center;\n"
        "flat out float v_rad, v_hard;\n"
        "flat out vec4 v_col;\n"      /* rgb, alpha */
        "flat out ivec4 v_cnt;\n"     /* rings, lines, stars, seed */
        "flat out int v_circ;\n"
        "void hide() { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); }\n"
        "void main()\n"
        "{\n"
        "	int corner = gl_VertexID;\n"
        "	int inst = gl_InstanceID, item = 0;\n"
        "	if (flares > 0) { item = inst % flares; inst /= flares; }\n"
        "	vec3 p;\n"
        "	float s = 1.0, a = 1.0;\n"
        "	int seed = h_cnt.w + inst;\n"
        "	if (src == 0) {\n"
        "		vec4 r = pts[inst];\n"
        "		p = (model * vec4(r.xyz, 1.0)).xyz;\n"
        "		s = r.w;\n"
        "	}\n"
        "	else {\n"
        "		vec4 pa = prt[3 * inst], pv = prt[3 * inst + 1], pi = prt[3 * inst + 2];\n"
        "		if (pi.w < 0.0 || pa.w < 0.0 || pa.w >= pv.w) { hide(); return; }\n"
        "		float t = clamp(pa.w / max(pv.w, 1e-6), 0.0, 1.0);\n"
        "		p = pa.xyz;\n"
        "		s = pi.x * mix(1.0, size_end, t);\n"
        "		a = mix(fade0.a, fade1.a, t);\n"
        "		seed = h_cnt.w + int(pi.w);\n"
        "	}\n"
        "	seed = seed % 256;\n"
        "	if (s <= 0.0 || a <= 0.0) { hide(); return; }\n"
        "	vec4 pv = view * vec4(p, 1.0);\n"
        "	vec4 c = proj * pv, c1 = proj * (pv + vec4(hasize * s, 0.0, 0.0, 0.0));\n"
        "	if (c.w <= 1e-6 || c1.w <= 1e-6) { hide(); return; }\n"
        "	vec2 sc = vp.xy + (c.xy / c.w * 0.5 + 0.5) * vp.zw;\n"
        "	vec2 sc1 = vp.xy + (c1.xy / c1.w * 0.5 + 0.5) * vp.zw;\n"
        "	float rad = abs(sc1.x - sc.x);\n"
        "	vec4 col = vec4(h_col.rgb, h_col.a * a);\n"
        "	float hard = h_par.y;\n"
        "	ivec4 cnt = ivec4(h_cnt.xyz, seed);\n"
        "	int circ = 0;\n"
        "	float depth = c.z / c.w;\n"
        "	if (flares > 0) {\n"
        /* how much of the halo shows: its pixels weighted as Blender
         * Internal counts them (all radials / r^3), squared */
        "		float visifac = 1.0966;\n"
        "		if (scenedepth_on > 0.5) {\n"
        "			float zc = depth * 0.5 + 0.5, sum = 0.0, all = 0.0;\n"
        "			for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) {\n"
        "				vec2 o = (vec2(i, j) + 0.5) / 8.0 * 2.0 - 1.0;\n"
        "				float r = length(o);\n"
        "				if (r >= 1.0) continue;\n"
        "				float w = 1.0 - r;\n"
        "				all += w;\n"
        "				vec2 uv = (sc - vp.xy + o * rad) / vp.zw;\n"
        "				if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) continue;\n"
        "				if (texture(scenedepth, uv).r >= zc - 1e-5) sum += w;\n"
        "			}\n"
        "			float f = (all > 0.0) ? sum / all : 1.0;\n"
        "			visifac *= f * f;\n"
        "		}\n"
        "		depth = -1.0;\n"
        "		if (item == 0) {\n"
        "			rad *= f_par.x * visifac;\n"
        "			col.a *= visifac;\n"
        "		}\n"
        "		else {\n"
        "			int b = (item + 1) / 2;\n"
        "			int rc = int(f_par.w) + 7 * (b - 1);\n"
        "			col.rgb = vec3(abs(hvf(rc)), abs(hvf(rc + 1)), abs(hvf(rc + 2)));\n"
        "			col.a = f_par.y * abs(col.a * visifac * hvf(rc + 3));\n"
        "			hard = 20.0 + abs(70.0 * hvf(rc + 7));\n"
        "			int type = int(abs(3.9 * hvf(rc + 6)));\n"
        "			float frad = f_par.z * sqrt(abs(2.0 * rad * f_par.x * visifac * hvf(rc + 4)));\n"
        "			if (type == 3) { frad *= 3.0; frad += vp.z / 10.0; }\n"
        "			vec2 half_ = vp.xy + 0.5 * vp.zw;\n"
        "			vec2 v = 1.4 * hvf(rc + 5) * (sc - half_);\n"
        "			float vz = 32.0 * sqrt(dot(v, v) + 1.0);\n"
        "			sc = half_ + v + (1.2 + hvf(rc + 8)) * vp.z * v / vz;\n"
        "			rad = frad;\n"
        "			cnt.xy = ivec2(0);\n"
        "			bool second = (item & 1) == 0;\n"
        "			if (second) col.a *= 0.5;\n"
        "			circ = (((second ? 2 : 1) & type) != 0) ? 1 : 0;\n"
        "		}\n"
        "	}\n"
        "	if (rad < 0.5 || col.a <= 0.0) { hide(); return; }\n"
        "	const vec2 corners[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),\n"
        "	                                vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));\n"
        "	vec2 q = sc + corners[corner] * (rad + 1.0);\n"
        "	gl_Position = vec4((q - vp.xy) / vp.zw * 2.0 - 1.0, depth, 1.0);\n"
        "	v_center = sc;\n"
        "	v_rad = rad;\n"
        "	v_hard = hard;\n"
        "	v_col = col;\n"
        "	v_cnt = cnt;\n"
        "	v_circ = circ;\n"
        "}\n";

static const char *halo_frag =
        "uniform vec4 h_par;\n"       /* add, hard, xalpha, - */
        "uniform vec3 linecol, ringcol;\n"
        "in vec2 v_center;\n"
        "flat in float v_rad, v_hard;\n"
        "flat in vec4 v_col;\n"
        "flat in ivec4 v_cnt;\n"
        "flat in int v_circ;\n"
        "out vec4 fragcolor;\n"
        "void main()\n"
        "{\n"
        "	vec2 d = gl_FragCoord.xy - v_center;\n"
        "	vec4 c = halo_shade(d.x, d.y, v_rad, v_col.rgb, v_col.a, v_hard, v_cnt.x, v_cnt.y, v_cnt.z, v_cnt.w,\n"
        "	                    h_par.z > 0.5, v_circ != 0, linecol, ringcol);\n"
        "	if (c.a <= 0.0 && dot(c.rgb, vec3(1.0)) <= 0.0) discard;\n"
        /* Blender Internal's addalphaAddfac: what is behind keeps
         * 1 - alpha * (1 - add) */
        "	fragcolor = vec4(c.rgb, c.a * (1.0 - h_par.x));\n"
        "}\n";

/* particles: three vec4 each (place and age; velocity and lifetime; size,
 * angle, turn speed, spawn number (-1 never)) */
static const char *particle_cs =
        "layout(local_size_x = 64) in;\n"
        "struct Collider { mat4 fwd, inv; vec4 half_; };\n"
        "layout(std430, binding = 16) buffer BgeParticles { vec4 prt[]; };\n"
        "layout(std430, binding = 17) readonly buffer BgeEmitter { vec4 tri[]; };\n"   /* 3 per triangle, w: area so far */
        "layout(std430, binding = 18) readonly buffer BgeColliders { Collider col[]; };\n"
        "uniform int maxp, ntris, ncol, collide;\n"
        "uniform uint seed;\n"
        "uniform float time, dt;\n"
        "uniform mat4 model;\n"
        "uniform vec3 evel;\n"
        "uniform vec4 d0;\n"          /* rate, life min, life max, speed */
        "uniform vec4 d1;\n"          /* cone (radians), from motion, gravity, drag */
        "uniform vec4 d2;\n"          /* size min, size max, turn speed (radians/s), restitution */
        "uint hash(uint x)\n"
        "{\n"
        "	x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;\n"
        "	return x;\n"
        "}\n"
        "float rnd(uint n, uint k) { return float(hash(n * 9781u + k * 6271u + seed * 26699u) & 0xffffffu) / 16777216.0; }\n"
        "void bounce(inout vec3 p, inout vec3 v)\n"
        "{\n"
        "	for (int c = 0; c < ncol; c++) {\n"
        "		vec3 lp = (col[c].inv * vec4(p, 1.0)).xyz;\n"
        "		vec3 h = col[c].half_.xyz, n = vec3(0.0);\n"
        "		int shape = int(col[c].half_.w + 0.5);\n"
        "		if (shape == 1) {\n"
        "			float r = length(lp);\n"
        "			if (r >= h.x || r <= 1e-6) continue;\n"
        "			n = lp / r;\n"
        "			lp = n * h.x;\n"
        "		}\n"
        "		else if (shape == 2) {\n"
        "			if (lp.z >= 0.0 || lp.z < -0.5 || any(greaterThan(abs(lp.xy), h.xy))) continue;\n"
        "			n = vec3(0.0, 0.0, 1.0);\n"
        "			lp.z = 0.0;\n"
        "		}\n"
        "		else {\n"
        "			vec3 d = h - abs(lp);\n"
        "			if (any(lessThanEqual(d, vec3(0.0)))) continue;\n"
        "			if (d.x <= d.y && d.x <= d.z) { n = vec3(sign(lp.x), 0.0, 0.0); lp.x = n.x * h.x; }\n"
        "			else if (d.y <= d.z) { n = vec3(0.0, sign(lp.y), 0.0); lp.y = n.y * h.y; }\n"
        "			else { n = vec3(0.0, 0.0, sign(lp.z)); lp.z = n.z * h.z; }\n"
        "		}\n"
        "		p = (col[c].fwd * vec4(lp, 1.0)).xyz;\n"
        "		vec3 wn = normalize(transpose(mat3(col[c].inv)) * n);\n"
        "		float vn = dot(v, wn);\n"
        "		if (vn < 0.0) v -= (1.0 + d2.w) * vn * wn;\n"
        "	}\n"
        "}\n"
        "void move(inout vec3 p, inout vec3 v, float h)\n"
        "{\n"
        "	v.z -= d1.z * h;\n"
        "	v *= max(1.0 - d1.w * h, 0.0);\n"
        "	p += v * h;\n"
        "	if (collide != 0) bounce(p, v);\n"
        "}\n"
        "void main()\n"
        "{\n"
        "	int i = int(gl_GlobalInvocationID.x);\n"
        "	if (i >= maxp) return;\n"
        "	vec4 pa = prt[3 * i], pv = prt[3 * i + 1], pi = prt[3 * i + 2];\n"
        "	float rate = d0.x;\n"
        "	float nmax = floor(time * rate);\n"
        "	if (rate <= 0.0 || ntris <= 0 || nmax < float(i)) { prt[3 * i + 2] = vec4(0.0, 0.0, 0.0, -1.0); return; }\n"
        "	float n = float(i) + float(maxp) * floor((nmax - float(i)) / float(maxp));\n"
        "	vec3 p = pa.xyz, v = pv.xyz;\n"
        "	if (n != pi.w) {\n"
        /* born: a place on the emitter by area, a direction in the cone
         * around its normal */
        "		uint un = uint(n);\n"
        "		float total = tri[3 * ntris - 1].w;\n"
        "		float pick = rnd(un, 1u) * total;\n"
        "		int lo = 0, hi = ntris - 1;\n"
        "		while (lo < hi) { int m = (lo + hi) / 2; if (tri[3 * m + 2].w < pick) lo = m + 1; else hi = m; }\n"
        "		vec3 a = tri[3 * lo].xyz, b = tri[3 * lo + 1].xyz, c = tri[3 * lo + 2].xyz;\n"
        "		float r1 = sqrt(rnd(un, 2u)), r2 = rnd(un, 3u);\n"
        "		vec3 lp = (1.0 - r1) * a + r1 * (1.0 - r2) * b + r1 * r2 * c;\n"
        "		vec3 ln = normalize(cross(b - a, c - a));\n"
        "		p = (model * vec4(lp, 1.0)).xyz;\n"
        "		vec3 wn = normalize(transpose(inverse(mat3(model))) * ln);\n"
        "		vec3 t1 = normalize(abs(wn.z) < 0.9 ? cross(wn, vec3(0.0, 0.0, 1.0)) : cross(wn, vec3(1.0, 0.0, 0.0)));\n"
        "		vec3 t2 = cross(wn, t1);\n"
        "		float ca = mix(1.0, cos(d1.x), rnd(un, 4u)), sa = sqrt(max(1.0 - ca * ca, 0.0));\n"
        "		float ph = 6.2831853 * rnd(un, 5u);\n"
        "		vec3 dir = ca * wn + sa * (cos(ph) * t1 + sin(ph) * t2);\n"
        "		v = dir * d0.w + evel * d1.y;\n"
        "		float life = mix(d0.y, d0.z, rnd(un, 6u));\n"
        "		float age = time - n / rate;\n"
        "		float turn = d2.z * ((rnd(un, 7u) < 0.5) ? -1.0 : 1.0);\n"
        "		pi = vec4(mix(d2.x, d2.y, rnd(un, 8u)), 6.2831853 * rnd(un, 9u), turn, n);\n"
        "		pv.w = life;\n"
        "		pa.w = 0.0;\n"
        /* the part of this frame it has lived, in up to 4 steps */
        "		float h = min(age, 1.0) / 4.0;\n"
        "		for (int k = 0; k < 4; k++) move(p, v, h);\n"
        "		pa.w = age;\n"
        "	}\n"
        "	else {\n"
        "		move(p, v, dt);\n"
        "		pa.w += dt;\n"
        "		pi.y += pi.z * dt;\n"
        "	}\n"
        "	prt[3 * i] = vec4(p, pa.w);\n"
        "	prt[3 * i + 1] = vec4(v, pv.w);\n"
        "	prt[3 * i + 2] = pi;\n"
        "}\n";

/* billboards and lines */
static const char *bill_vert =
        "layout(std430, binding = 16) readonly buffer BgeParticles { vec4 prt[]; };\n"
        "uniform mat4 view, proj;\n"
        "uniform int mode;\n"          /* 0 billboards, 1 lines */
        "uniform int align;\n"
        "uniform vec4 fade0, fade1;\n"
        "uniform float size_end;\n"
        "uniform vec3 line;\n"         /* back, front, by speed */
        "uniform ivec3 split;\n"       /* columns, rows, mode */
        "uniform uint seed;\n"
        "out vec2 v_uv;\n"
        "out float v_alpha;\n"
        "void hide() { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); }\n"
        "void main()\n"
        "{\n"
        "	int i = gl_InstanceID;\n"
        "	vec4 pa = prt[3 * i], pv = prt[3 * i + 1], pi = prt[3 * i + 2];\n"
        "	if (pi.w < 0.0 || pa.w < 0.0 || pa.w >= pv.w) { hide(); return; }\n"
        "	float t = clamp(pa.w / max(pv.w, 1e-6), 0.0, 1.0);\n"
        "	v_alpha = mix(fade0.a, fade1.a, t);\n"
        "	if (mode == 1) {\n"
        "		float sp = length(pv.xyz);\n"
        "		vec3 d = (sp > 1e-6) ? pv.xyz / sp : vec3(0.0, 0.0, 1.0);\n"
        "		float k = (line.z > 0.5) ? sp : 1.0;\n"
        "		vec3 p = pa.xyz + d * ((gl_VertexID == 0) ? -line.x * k : line.y * k);\n"
        "		gl_Position = proj * view * vec4(p, 1.0);\n"
        "		v_uv = vec2(0.0);\n"
        "		return;\n"
        "	}\n"
        "	float s = 0.5 * pi.x * mix(1.0, size_end, t);\n"
        "	const vec2 corners[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),\n"
        "	                                vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));\n"
        "	vec2 cr = corners[gl_VertexID];\n"
        "	float ca = cos(pi.y), sa = sin(pi.y);\n"
        "	vec2 rc = vec2(ca * cr.x - sa * cr.y, sa * cr.x + ca * cr.y);\n"
        "	vec3 x, y;\n"
        "	mat4 vinv = inverse(view);\n"
        "	if (align == 1) {\n"
        "		vec3 to = normalize(vinv[3].xyz - pa.xyz);\n"
        "		y = vec3(0.0, 0.0, 1.0);\n"
        "		x = normalize(cross(y, to));\n"
        "		if (any(isnan(x))) x = vinv[0].xyz;\n"
        "		rc = cr;\n"
        "	}\n"
        "	else if (align == 2 && dot(pv.xyz, pv.xyz) > 1e-10) {\n"
        "		y = normalize(pv.xyz);\n"
        "		vec3 to = normalize(vinv[3].xyz - pa.xyz);\n"
        "		x = normalize(cross(y, to));\n"
        "		if (any(isnan(x))) x = vinv[0].xyz;\n"
        "		rc = cr;\n"
        "	}\n"
        "	else {\n"
        "		x = vinv[0].xyz;\n"
        "		y = vinv[1].xyz;\n"
        "	}\n"
        "	vec3 p = pa.xyz + (x * rc.x + y * rc.y) * s;\n"
        "	gl_Position = proj * view * vec4(p, 1.0);\n"
        "	vec2 uv = cr * 0.5 + 0.5;\n"
        "	int cells = max(split.x, 1) * max(split.y, 1);\n"
        "	int cell = 0;\n"
        "	if (split.z == 1) cell = min(int(t * float(cells)), cells - 1);\n"
        "	else if (split.z == 2) cell = int(uint(pi.w) * 2654435761u % uint(cells));\n"
        "	int cx = cell % max(split.x, 1), cy = cell / max(split.x, 1);\n"
        "	v_uv = (vec2(float(cx), float(max(split.y, 1) - 1 - cy)) + uv) / vec2(max(split.x, 1), max(split.y, 1));\n"
        "}\n";

static const char *bill_frag =
        "uniform vec4 color;\n"
        "uniform sampler2D tex;\n"
        "uniform int has_tex;\n"
        "uniform int oit;\n"        /* the pipeline's order-independent pass (P8.21) */
        "in vec2 v_uv;\n"
        "in float v_alpha;\n"
        "layout(location = 0) out vec4 fragcolor;\n"
        "layout(location = 1) out vec4 reveal;\n"
        "void main()\n"
        "{\n"
        "	vec4 c = color;\n"
        "	if (has_tex != 0) c *= texture(tex, v_uv);\n"
        "	c.a *= v_alpha;\n"
        "	if (c.a <= 0.002) discard;\n"
        "	fragcolor = vec4(c.rgb * c.a, c.a);\n"
        "	if (oit != 0) {\n"
        "		float a = clamp(c.a, 0.0, 1.0);\n"
        "		float w = clamp(pow(min(1.0, a * 10.0) + 0.01, 3.0) * 1e8 * pow(1.0 - gl_FragCoord.z * 0.9, 3.0),\n"
        "		                1e-2, 3e3);\n"
        "		fragcolor *= w;\n"
        "		reveal = vec4(a);\n"
        "	}\n"
        "}\n";

/* ------------------------------------------------------------------------ */
/* programs and shared buffers */

static struct {
	GLuint halo, bill, sim;
	GLuint hash;    /* hashvectf (768 floats) */
	GLuint colliders;
	GLuint vao;
	bool failed;
} spr = {0};

static GLuint spr_shader(GLenum type, const char *const *src, int n, const char *label)
{
	GLuint sh = glCreateShader(type);
	glShaderSource(sh, n, src, NULL);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetShaderInfoLog(sh, sizeof(log), NULL, log);
		printf("BGE-Remix %s: does not build:\n%s\n", label, log);
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}

static GLuint spr_program(const char *vs_body, const char *fs_body, bool halo, const char *label)
{
	const char *version = "#version 450 core\n";
	const char *vs[3] = {version, halo ? halo_glsl : "", vs_body};
	const char *fs[3] = {version, halo ? halo_glsl : "", fs_body};
	GLuint v = spr_shader(GL_VERTEX_SHADER, vs, 3, label);
	GLuint f = v ? spr_shader(GL_FRAGMENT_SHADER, fs, 3, label) : 0;
	if (!v || !f) {
		if (v) glDeleteShader(v);
		return 0;
	}
	GLuint p = glCreateProgram();
	glAttachShader(p, v);
	glAttachShader(p, f);
	glLinkProgram(p);
	glDeleteShader(v);
	glDeleteShader(f);
	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		printf("BGE-Remix %s: does not link:\n%s\n", label, log);
		glDeleteProgram(p);
		return 0;
	}
	GPU_debug_label(GL_PROGRAM, p, label);
	return p;
}

static bool spr_init(void)
{
	if (spr.failed) {
		return false;
	}
	if (spr.halo) {
		return true;
	}
	if (!GLEW_VERSION_4_3) {
		spr.failed = true;
		return false;
	}
	spr.halo = spr_program(halo_vert, halo_frag, true, "BGE-Remix halos");
	spr.bill = spr_program(bill_vert, bill_frag, false, "BGE-Remix particle billboards");
	spr.sim = GPU_compute_program(particle_cs, "BGE-Remix particles");
	if (!spr.halo || !spr.bill || !spr.sim) {
		spr.failed = true;
		printf("BGE-Remix: halos and particles are not drawn (their shaders do not build)\n");
		return false;
	}
	const unsigned char *perm;
	const float *hashvect, *hashpnt;
	BLI_noise_tables(&perm, &hashvect, &hashpnt);
	glCreateBuffers(1, &spr.hash);
	glNamedBufferStorage(spr.hash, sizeof(float) * 768, hashvect, 0);
	GPU_debug_label(GL_BUFFER, spr.hash, "BGE-Remix halo random vectors");
	glCreateBuffers(1, &spr.colliders);
	glNamedBufferStorage(spr.colliders, sizeof(float) * 36 * SPR_MAX_COLLIDERS, NULL, GL_DYNAMIC_STORAGE_BIT);
	GPU_debug_label(GL_BUFFER, spr.colliders, "BGE-Remix particle colliders");
	glGenVertexArrays(1, &spr.vao);
	return true;
}

void GPU_sprites_exit(void)
{
	if (spr.halo) glDeleteProgram(spr.halo);
	if (spr.bill) glDeleteProgram(spr.bill);
	if (spr.sim) glDeleteProgram(spr.sim);
	if (spr.hash) glDeleteBuffers(1, &spr.hash);
	if (spr.colliders) glDeleteBuffers(1, &spr.colliders);
	if (spr.vao) glDeleteVertexArrays(1, &spr.vao);
	memset(&spr, 0, sizeof(spr));
}

/* the matrices and viewport of the current view */
static void spr_view(GLuint p, int r_vp[4])
{
	float view[4][4], proj[4][4];
	GPU_matrix_get(GPU_MATRIX_MODELVIEW, view);
	GPU_matrix_get(GPU_MATRIX_PROJECTION, proj);
	glUniformMatrix4fv(glGetUniformLocation(p, "view"), 1, GL_FALSE, &view[0][0]);
	glUniformMatrix4fv(glGetUniformLocation(p, "proj"), 1, GL_FALSE, &proj[0][0]);
	glGetIntegerv(GL_VIEWPORT, r_vp);
}

typedef struct SprState {
	GPUStateScope scope;
	GLint program;
} SprState;

static void spr_begin(SprState *st, GLuint program, bool depth_test, bool oit)
{
	glGetIntegerv(GL_CURRENT_PROGRAM, &st->program);
	GPU_state_scope_begin(&st->scope);
	if (!oit) {
		/* (in the order-independent pass the pipeline's blending stays) */
		GPU_state_blend(GPU_LAYER_BLEND_NONE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	}
	GPU_state_depth(depth_test, false);
	glDisable(GL_CULL_FACE);
	glUseProgram(program);
	glBindVertexArray(spr.vao);
}

static void spr_end(SprState *st)
{
	glBindVertexArray(0);
	glUseProgram((GLuint)st->program);
	GPU_state_scope_end(&st->scope);
}

/* ------------------------------------------------------------------------ */
/* halos */

unsigned int GPU_halo_points_create(const float (*co)[3], int count)
{
	if (count <= 0 || !GLEW_VERSION_4_3) {
		return 0;
	}
	float *data = MEM_mallocN(sizeof(float) * 4 * (size_t)count, "halo points");
	for (int i = 0; i < count; i++) {
		copy_v3_v3(&data[4 * i], co[i]);
		data[4 * i + 3] = 1.0f;
	}
	GLuint buf = 0;
	glCreateBuffers(1, &buf);
	glNamedBufferStorage(buf, sizeof(float) * 4 * (size_t)count, data, 0);
	GPU_debug_label(GL_BUFFER, buf, "BGE-Remix halo points");
	MEM_freeN(data);
	return buf;
}

void GPU_halo_points_free(unsigned int buffer)
{
	if (buffer) {
		glDeleteBuffers(1, &buffer);
	}
}

static void halo_uniforms(GLuint p, const GPUHaloStyle *s, int src, int flares)
{
	glUniform1i(glGetUniformLocation(p, "src"), src);
	glUniform1i(glGetUniformLocation(p, "flares"), flares);
	glUniform1f(glGetUniformLocation(p, "hasize"), s->size);
	glUniform4f(glGetUniformLocation(p, "h_col"), s->color[0], s->color[1], s->color[2], s->alpha);
	glUniform4f(glGetUniformLocation(p, "h_par"), s->add, s->hard, s->xalpha ? 1.0f : 0.0f, 0.0f);
	glUniform4i(glGetUniformLocation(p, "h_cnt"), s->rings, s->lines, s->stars, s->seed);
	glUniform4f(glGetUniformLocation(p, "f_par"), s->flare_size, s->flare_boost, s->sub_size, (float)s->flare_seed);
	glUniform3fv(glGetUniformLocation(p, "linecol"), 1, s->line_color);
	glUniform3fv(glGetUniformLocation(p, "ringcol"), 1, s->ring_color);
	int vp[4];
	spr_view(p, vp);
	glUniform4f(glGetUniformLocation(p, "vp"), (float)vp[0], (float)vp[1], (float)vp[2], (float)vp[3]);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SPR_BIND_HASH, spr.hash);
	/* the scene's depth (drawn first), for the flares */
	glUniform1i(glGetUniformLocation(p, "scenedepth"), GPU_lights_unit_scenedepth());
	glUniform1f(glGetUniformLocation(p, "scenedepth_on"), gpu_scene_depth_is_on() ? 1.0f : 0.0f);
}

void GPU_halos_draw(unsigned int points, int count, const float model[4][4], const GPUHaloStyle *style, bool flares)
{
	if (!points || count <= 0 || !spr_init() || (flares && style->flares <= 0)) {
		return;
	}
	SprState st;
	spr_begin(&st, spr.halo, !flares, false);
	const int nfl = flares ? 1 + 2 * (style->flares - 1) : 0;
	halo_uniforms(spr.halo, style, 0, nfl);
	glUniformMatrix4fv(glGetUniformLocation(spr.halo, "model"), 1, GL_FALSE, &model[0][0]);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SPR_BIND_POINTS, points);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count * (flares ? nfl : 1));
	spr_end(&st);
}

/* ------------------------------------------------------------------------ */
/* particles */

void GPU_particle_description(const Scene *scene, const ParticleSystem *psys, float gravity, bool colliders,
                              float bounce, GPUParticleDesc *r)
{
	const ParticleSettings *part = psys->part;
	memset(r, 0, sizeof(*r));
	const float fps = (scene && scene->r.frs_sec > 0) ?
	                      (float)scene->r.frs_sec / max_ff(scene->r.frs_sec_base, 0.001f) : 24.0f;
	/* emission: totpart over the emission frames, again and again */
	const float frames = max_ff(part->end - part->sta, 1.0f);
	r->spawn_rate = (float)part->totpart / frames * fps;
	r->lifetime_max = max_ff(part->lifetime, 1.0f) / fps;
	r->lifetime_min = r->lifetime_max * (1.0f - min_ff(max_ff(part->randlife, 0.0f), 1.0f));
	r->max_particles = (int)max_ff((float)part->totpart, r->spawn_rate * r->lifetime_max * 1.1f + 16.0f);
	r->speed_normal = part->normfac;
	r->cone_angle_deg = (float)(atan2((double)part->randfac, (double)max_ff(fabsf(part->normfac), 0.001f)) *
	                            180.0 / M_PI);
	r->cone_angle_deg = min_ff(r->cone_angle_deg, 90.0f);
	r->speed_from_motion = part->obfac;
	const float gweight = part->effector_weights ? part->effector_weights->global_gravity : 1.0f;
	r->gravity = gravity * gweight;
	r->drag = part->dragfac;
	r->size_max = max_ff(part->size, 0.001f);
	r->size_min = r->size_max * (1.0f - min_ff(max_ff(part->randsize, 0.0f), 1.0f));
	r->size_end_scale = 1.0f;
	r->color_start[0] = r->color_start[1] = r->color_start[2] = r->color_start[3] = 1.0f;
	r->color_end[0] = r->color_end[1] = r->color_end[2] = 1.0f;
	r->color_end[3] = 0.0f;  /* fade out at the end of life */
	r->rotation_speed = part->avemode ? (float)(part->avefac / (2.0 * M_PI)) : 0.0f;
	r->collide = colliders ? 1 : 0;
	r->restitution = bounce;
}

struct GPUParticles {
	GPUParticleDesc desc;
	GLuint buf, tris;
	int max, ntris;
	unsigned int seed;
};

GPUParticles *GPU_particles_create(const GPUParticleDesc *desc, const float *tris, int tri_count, unsigned int seed)
{
	if (!desc || desc->max_particles <= 0 || tri_count <= 0 || !spr_init()) {
		return NULL;
	}
	GPUParticles *ps = MEM_callocN(sizeof(*ps), "GPUParticles");
	ps->desc = *desc;
	ps->max = min_ii(desc->max_particles, 1 << 20);
	ps->seed = seed;
	/* every slot unborn */
	float *init = MEM_callocN(sizeof(float) * 12 * (size_t)ps->max, "particles");
	for (int i = 0; i < ps->max; i++) {
		init[12 * i + 11] = -1.0f;
	}
	glCreateBuffers(1, &ps->buf);
	glNamedBufferStorage(ps->buf, sizeof(float) * 12 * (size_t)ps->max, init, 0);
	GPU_debug_label(GL_BUFFER, ps->buf, "BGE-Remix particles");
	MEM_freeN(init);
	/* the emitter: corners, w the area up to and including the triangle */
	float *t = MEM_mallocN(sizeof(float) * 12 * (size_t)tri_count, "emitter");
	double area = 0.0;
	for (int i = 0; i < tri_count; i++) {
		const float *a = &tris[9 * i], *b = a + 3, *c = a + 6;
		area += (double)area_tri_v3(a, b, c);
		for (int k = 0; k < 3; k++) {
			copy_v3_v3(&t[12 * i + 4 * k], &tris[9 * i + 3 * k]);
			t[12 * i + 4 * k + 3] = (float)area;
		}
	}
	if (area <= 0.0) {
		/* a flat or empty emitter: its triangles by count */
		for (int i = 0; i < tri_count; i++) {
			for (int k = 0; k < 3; k++) {
				t[12 * i + 4 * k + 3] = (float)(i + 1);
			}
		}
	}
	ps->ntris = tri_count;
	glCreateBuffers(1, &ps->tris);
	glNamedBufferStorage(ps->tris, sizeof(float) * 12 * (size_t)tri_count, t, 0);
	GPU_debug_label(GL_BUFFER, ps->tris, "BGE-Remix particle emitter");
	MEM_freeN(t);
	return ps;
}

void GPU_particles_free(GPUParticles *ps)
{
	if (ps) {
		glDeleteBuffers(1, &ps->buf);
		glDeleteBuffers(1, &ps->tris);
		MEM_freeN(ps);
	}
}

int GPU_particles_max(const GPUParticles *ps)
{
	return ps ? ps->max : 0;
}

void GPU_particles_step(GPUParticles *ps, double time, float dt, const float model[4][4], const float velocity[3],
                        const GPUParticleCollider *colliders, int collider_count)
{
	if (!ps || !spr_init()) {
		return;
	}
	const GPUParticleDesc *d = &ps->desc;
	const int ncol = min_ii(collider_count, SPR_MAX_COLLIDERS);
	if (ncol > 0) {
		float data[36 * SPR_MAX_COLLIDERS];
		for (int c = 0; c < ncol; c++) {
			float inv[4][4];
			invert_m4_m4(inv, (float (*)[4])colliders[c].obmat);
			memcpy(&data[36 * c], colliders[c].obmat, sizeof(float) * 16);
			memcpy(&data[36 * c + 16], inv, sizeof(float) * 16);
			copy_v3_v3(&data[36 * c + 32], colliders[c].half);
			data[36 * c + 35] = (float)colliders[c].shape;
		}
		glNamedBufferSubData(spr.colliders, 0, sizeof(float) * 36 * ncol, data);
	}
	GLint program = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	const GLuint p = spr.sim;
	glUseProgram(p);
	glUniform1i(glGetUniformLocation(p, "maxp"), ps->max);
	glUniform1i(glGetUniformLocation(p, "ntris"), ps->ntris);
	glUniform1i(glGetUniformLocation(p, "ncol"), ncol);
	glUniform1i(glGetUniformLocation(p, "collide"), d->collide && ncol > 0);
	glUniform1ui(glGetUniformLocation(p, "seed"), ps->seed);
	glUniform1f(glGetUniformLocation(p, "time"), (float)time);
	glUniform1f(glGetUniformLocation(p, "dt"), max_ff(dt, 0.0f));
	glUniformMatrix4fv(glGetUniformLocation(p, "model"), 1, GL_FALSE, &model[0][0]);
	glUniform3fv(glGetUniformLocation(p, "evel"), 1, velocity);
	glUniform4f(glGetUniformLocation(p, "d0"), d->spawn_rate, d->lifetime_min, d->lifetime_max, d->speed_normal);
	glUniform4f(glGetUniformLocation(p, "d1"), DEG2RADF(d->cone_angle_deg), d->speed_from_motion, d->gravity, d->drag);
	glUniform4f(glGetUniformLocation(p, "d2"), d->size_min, d->size_max, d->rotation_speed * 2.0f * (float)M_PI,
	            d->restitution);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SPR_BIND_PARTICLES, ps->buf);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SPR_BIND_TRIS, ps->tris);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SPR_BIND_COLLIDERS, spr.colliders);
	glDispatchCompute((GLuint)((ps->max + 63) / 64), 1, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
	glUseProgram((GLuint)program);
}

static void particle_fade(GLuint p, const GPUParticleDesc *d)
{
	glUniform4fv(glGetUniformLocation(p, "fade0"), 1, d->color_start);
	glUniform4fv(glGetUniformLocation(p, "fade1"), 1, d->color_end);
	glUniform1f(glGetUniformLocation(p, "size_end"), d->size_end_scale);
}

void GPU_particles_draw_halos(GPUParticles *ps, const GPUHaloStyle *style, bool flares)
{
	if (!ps || !spr_init() || (flares && style->flares <= 0)) {
		return;
	}
	SprState st;
	spr_begin(&st, spr.halo, !flares, false);
	const int nfl = flares ? 1 + 2 * (style->flares - 1) : 0;
	halo_uniforms(spr.halo, style, 1, nfl);
	particle_fade(spr.halo, &ps->desc);
	float ident[4][4];
	unit_m4(ident);
	glUniformMatrix4fv(glGetUniformLocation(spr.halo, "model"), 1, GL_FALSE, &ident[0][0]);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SPR_BIND_PARTICLES, ps->buf);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 6, ps->max * (flares ? nfl : 1));
	spr_end(&st);
}

void GPU_particles_draw_billboards(GPUParticles *ps, const float color[4], unsigned int texture, int align,
                                   int split_x, int split_y, int split_mode)
{
	if (!ps || !spr_init()) {
		return;
	}
	SprState st;
	const int oit = GPU_oit_get();
	spr_begin(&st, spr.bill, true, oit != 0);
	const GLuint p = spr.bill;
	int vp[4];
	spr_view(p, vp);
	particle_fade(p, &ps->desc);
	glUniform1i(glGetUniformLocation(p, "oit"), oit);
	glUniform1i(glGetUniformLocation(p, "mode"), 0);
	glUniform1i(glGetUniformLocation(p, "align"), align);
	glUniform3i(glGetUniformLocation(p, "split"), max_ii(split_x, 1), max_ii(split_y, 1), split_mode);
	glUniform1ui(glGetUniformLocation(p, "seed"), ps->seed);
	glUniform4fv(glGetUniformLocation(p, "color"), 1, color);
	glUniform1i(glGetUniformLocation(p, "has_tex"), texture ? 1 : 0);
	glUniform1i(glGetUniformLocation(p, "tex"), 0);
	glBindTextureUnit(0, texture);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SPR_BIND_PARTICLES, ps->buf);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 6, ps->max);
	glBindTextureUnit(0, 0);
	spr_end(&st);
}

void GPU_particles_draw_lines(GPUParticles *ps, const float color[4], float back, float front, bool by_speed)
{
	if (!ps || !spr_init()) {
		return;
	}
	SprState st;
	spr_begin(&st, spr.bill, true, false);
	const GLuint p = spr.bill;
	int vp[4];
	spr_view(p, vp);
	particle_fade(p, &ps->desc);
	glUniform1i(glGetUniformLocation(p, "oit"), 0);
	glUniform1i(glGetUniformLocation(p, "mode"), 1);
	glUniform3f(glGetUniformLocation(p, "line"), back, front, by_speed ? 1.0f : 0.0f);
	glUniform4fv(glGetUniformLocation(p, "color"), 1, color);
	glUniform1i(glGetUniformLocation(p, "has_tex"), 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SPR_BIND_PARTICLES, ps->buf);
	glDrawArraysInstanced(GL_LINES, 0, 2, ps->max);
	spr_end(&st);
}

int GPU_particles_read(GPUParticles *ps, float (*r_places)[4], int max)
{
	if (!ps || max <= 0) {
		return 0;
	}
	const int n = min_ii(ps->max, max);
	float *data = MEM_mallocN(sizeof(float) * 12 * (size_t)n, "particle places");
	glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
	glGetNamedBufferSubData(ps->buf, 0, sizeof(float) * 12 * (size_t)n, data);
	for (int i = 0; i < n; i++) {
		const float *pa = &data[12 * i], *pv = pa + 4, *pi = pa + 8;
		copy_v3_v3(r_places[i], pa);
		const bool live = pi[3] >= 0.0f && pa[3] >= 0.0f && pa[3] < pv[3];
		r_places[i][3] = live ? max_ff(pi[0], 1e-4f) : 0.0f;
	}
	MEM_freeN(data);
	return n;
}
