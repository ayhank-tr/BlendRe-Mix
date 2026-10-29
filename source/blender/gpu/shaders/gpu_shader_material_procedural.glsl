/* BGE-Remix P8.5: Blender Internal's procedural textures in GLSL.
 *
 * A port of blenlib's noise.c and the render module's render_texture.c, so
 * a procedural texture looks the same in the game, the 3D view and an F12
 * render. This library is compiled only into the shaders of materials that
 * use a procedural texture (gpu_codegen.c).
 *
 * The lookup tables of noise.c (the permutation, hashvectf and hashpntf) are
 * in one 768x1 float texture ("tab"):
 *   texels   0..255  permutation (red)
 *   texels 256..511  hashvectf (rgb)
 *   texels 512..767  hashpntf (rgb)
 *
 * Only functions called from C are written with a "void" return type; the
 * code generator reads those declarations. */

#ifdef BIT_OPERATIONS

/* ---------------------------------------------------------------------- */
/* tables */

int bi_perm(sampler2D tab, int i)
{
	return int(texelFetch(tab, ivec2(i & 255, 0), 0).r + 0.5);
}

vec3 bi_hashvect(sampler2D tab, int i)
{
	return texelFetch(tab, ivec2(256 + (i & 255), 0), 0).rgb;
}

vec3 bi_hashpnt(sampler2D tab, int i)
{
	return texelFetch(tab, ivec2(512 + (i & 255), 0), 0).rgb;
}

/* HASHPNT(x, y, z) */
vec3 bi_hashpnt3(sampler2D tab, int x, int y, int z)
{
	return bi_hashpnt(tab, bi_perm(tab, (bi_perm(tab, (bi_perm(tab, z & 255) + y) & 255) + x) & 255));
}

float bi_lerp(float t, float a, float b)
{
	return (a + t * (b - a));
}

/* ---------------------------------------------------------------------- */
/* improved Perlin (newPerlin) */

float bi_npfade(float t)
{
	return (t * t * t * (t * (t * 6.0 - 15.0) + 10.0));
}

float bi_grad(int hash_val, float x, float y, float z)
{
	int h = hash_val & 15;
	float u = (h < 8) ? x : y;
	float v = (h < 4) ? y : (((h == 12) || (h == 14)) ? x : z);
	return (((h & 1) == 0) ? u : -u) + (((h & 2) == 0) ? v : -v);
}

float bi_new_perlin(sampler2D tab, vec3 p)
{
	float u = floor(p.x), v = floor(p.y), w = floor(p.z);
	int X = int(u) & 255, Y = int(v) & 255, Z = int(w) & 255;
	float x = p.x - u, y = p.y - v, z = p.z - w;
	u = bi_npfade(x);
	v = bi_npfade(y);
	w = bi_npfade(z);
	int A = bi_perm(tab, X) + Y;
	int AA = bi_perm(tab, A) + Z;
	int AB = bi_perm(tab, A + 1) + Z;
	int B = bi_perm(tab, X + 1) + Y;
	int BA = bi_perm(tab, B) + Z;
	int BB = bi_perm(tab, B + 1) + Z;
	return bi_lerp(w, bi_lerp(v, bi_lerp(u, bi_grad(bi_perm(tab, AA), x, y, z),
	                                        bi_grad(bi_perm(tab, BA), x - 1.0, y, z)),
	                             bi_lerp(u, bi_grad(bi_perm(tab, AB), x, y - 1.0, z),
	                                        bi_grad(bi_perm(tab, BB), x - 1.0, y - 1.0, z))),
	                  bi_lerp(v, bi_lerp(u, bi_grad(bi_perm(tab, AA + 1), x, y, z - 1.0),
	                                        bi_grad(bi_perm(tab, BA + 1), x - 1.0, y, z - 1.0)),
	                             bi_lerp(u, bi_grad(bi_perm(tab, AB + 1), x, y - 1.0, z - 1.0),
	                                        bi_grad(bi_perm(tab, BB + 1), x - 1.0, y - 1.0, z - 1.0))));
}

/* ---------------------------------------------------------------------- */
/* Blender's original noise (orgBlenderNoise) */

float bi_org_blender_noise(sampler2D tab, vec3 p)
{
	float n = 0.5;
	vec3 f = floor(p);
	vec3 o = p - f;
	int ix = int(f.x), iy = int(f.y), iz = int(f.z);
	vec3 j = o - vec3(1.0);

	vec3 c0 = o * o, c1 = j * j;
	c0 = vec3(1.0) - 3.0 * c0 + 2.0 * c0 * o;
	c1 = vec3(1.0) - 3.0 * c1 - 2.0 * c1 * j;

	int b00 = bi_perm(tab, bi_perm(tab, ix & 255) + (iy & 255));
	int b10 = bi_perm(tab, bi_perm(tab, (ix + 1) & 255) + (iy & 255));
	int b01 = bi_perm(tab, bi_perm(tab, ix & 255) + ((iy + 1) & 255));
	int b11 = bi_perm(tab, bi_perm(tab, (ix + 1) & 255) + ((iy + 1) & 255));
	int b20 = iz & 255, b21 = (iz + 1) & 255;
	vec3 h;
	float i;

	i = (c0.x * c0.y * c0.z);
	h = bi_hashvect(tab, bi_perm(tab, b20 + b00));
	n += i * (h.x * o.x + h.y * o.y + h.z * o.z);
	i = (c0.x * c0.y * c1.z);
	h = bi_hashvect(tab, bi_perm(tab, b21 + b00));
	n += i * (h.x * o.x + h.y * o.y + h.z * j.z);
	i = (c0.x * c1.y * c0.z);
	h = bi_hashvect(tab, bi_perm(tab, b20 + b01));
	n += i * (h.x * o.x + h.y * j.y + h.z * o.z);
	i = (c0.x * c1.y * c1.z);
	h = bi_hashvect(tab, bi_perm(tab, b21 + b01));
	n += i * (h.x * o.x + h.y * j.y + h.z * j.z);
	i = c1.x * c0.y * c0.z;
	h = bi_hashvect(tab, bi_perm(tab, b20 + b10));
	n += i * (h.x * j.x + h.y * o.y + h.z * o.z);
	i = c1.x * c0.y * c1.z;
	h = bi_hashvect(tab, bi_perm(tab, b21 + b10));
	n += i * (h.x * j.x + h.y * o.y + h.z * j.z);
	i = c1.x * c1.y * c0.z;
	h = bi_hashvect(tab, bi_perm(tab, b20 + b11));
	n += i * (h.x * j.x + h.y * j.y + h.z * o.z);
	i = (c1.x * c1.y * c1.z);
	h = bi_hashvect(tab, bi_perm(tab, b21 + b11));
	n += i * (h.x * j.x + h.y * j.y + h.z * j.z);

	return clamp(n, 0.0, 1.0);
}

/* ---------------------------------------------------------------------- */
/* original Perlin (noise3_perlin), with its +10000 */

float bi_surve(float t)
{
	return t * t * (3.0 - 2.0 * t);
}

float bi_org_perlin_noise(sampler2D tab, vec3 vec)
{
	float t;
	t = vec.x + 10000.0;
	int bx0 = int(t) & 255, bx1 = (bx0 + 1) & 255;
	float rx0 = t - floor(t), rx1 = rx0 - 1.0;
	t = vec.y + 10000.0;
	int by0 = int(t) & 255, by1 = (by0 + 1) & 255;
	float ry0 = t - floor(t), ry1 = ry0 - 1.0;
	t = vec.z + 10000.0;
	int bz0 = int(t) & 255, bz1 = (bz0 + 1) & 255;
	float rz0 = t - floor(t), rz1 = rz0 - 1.0;

	int i = bi_perm(tab, bx0);
	int j = bi_perm(tab, bx1);
	int b00 = bi_perm(tab, i + by0);
	int b10 = bi_perm(tab, j + by0);
	int b01 = bi_perm(tab, i + by1);
	int b11 = bi_perm(tab, j + by1);

	float sx = bi_surve(rx0), sy = bi_surve(ry0), sz = bi_surve(rz0);
	vec3 q;
	float u, v, a, b, c, d;

	q = bi_hashvect(tab, b00 + bz0); u = rx0 * q.x + ry0 * q.y + rz0 * q.z;
	q = bi_hashvect(tab, b10 + bz0); v = rx1 * q.x + ry0 * q.y + rz0 * q.z;
	a = bi_lerp(sx, u, v);
	q = bi_hashvect(tab, b01 + bz0); u = rx0 * q.x + ry1 * q.y + rz0 * q.z;
	q = bi_hashvect(tab, b11 + bz0); v = rx1 * q.x + ry1 * q.y + rz0 * q.z;
	b = bi_lerp(sx, u, v);
	c = bi_lerp(sy, a, b);

	q = bi_hashvect(tab, b00 + bz1); u = rx0 * q.x + ry0 * q.y + rz1 * q.z;
	q = bi_hashvect(tab, b10 + bz1); v = rx1 * q.x + ry0 * q.y + rz1 * q.z;
	a = bi_lerp(sx, u, v);
	q = bi_hashvect(tab, b01 + bz1); u = rx0 * q.x + ry1 * q.y + rz1 * q.z;
	q = bi_hashvect(tab, b11 + bz1); v = rx1 * q.x + ry1 * q.y + rz1 * q.z;
	b = bi_lerp(sx, u, v);
	d = bi_lerp(sy, a, b);

	return 1.5 * bi_lerp(sz, c, d);
}

/* ---------------------------------------------------------------------- */
/* Voronoi / Worley */

float bi_voronoi_dist(vec3 d, float e, int dtype)
{
	vec3 a = abs(d);
	if (dtype == 1) {
		return dot(d, d);
	}
	else if (dtype == 2) {
		return a.x + a.y + a.z;
	}
	else if (dtype == 3) {
		float t = (a.x > a.y) ? a.x : a.y;
		return (a.z > t) ? a.z : t;
	}
	else if (dtype == 4) {
		float s = sqrt(a.x) + sqrt(a.y) + sqrt(a.z);
		return s * s;
	}
	else if (dtype == 5) {
		vec3 s = d * d;
		return sqrt(sqrt(dot(s, s)));
	}
	else if (dtype == 6) {
		return pow(pow(a.x, e) + pow(a.y, e) + pow(a.z, e), 1.0 / e);
	}
	return sqrt(dot(d, d));
}

/* da: the four nearest distances; pa0..pa3: their feature points
 * (returns the nearest distance) */
float bi_voronoi(sampler2D tab, vec3 p, float me, int dtype,
                out vec4 da, out vec3 pa0, out vec3 pa1, out vec3 pa2, out vec3 pa3)
{
	int xi = int(floor(p.x));
	int yi = int(floor(p.y));
	int zi = int(floor(p.z));
	da = vec4(1e10);
	pa0 = pa1 = pa2 = pa3 = vec3(0.0);
	for (int xx = xi - 1; xx <= xi + 1; xx++) {
		for (int yy = yi - 1; yy <= yi + 1; yy++) {
			for (int zz = zi - 1; zz <= zi + 1; zz++) {
				vec3 h = bi_hashpnt3(tab, xx, yy, zz);
				vec3 fp = vec3(h.x + float(xx), h.y + float(yy), h.z + float(zz));
				float d = bi_voronoi_dist(p - fp, me, dtype);
				if (d < da.x) {
					da = vec4(d, da.x, da.y, da.z);
					pa3 = pa2; pa2 = pa1; pa1 = pa0; pa0 = fp;
				}
				else if (d < da.y) {
					da = vec4(da.x, d, da.y, da.z);
					pa3 = pa2; pa2 = pa1; pa1 = fp;
				}
				else if (d < da.z) {
					da.w = da.z; da.z = d;
					pa3 = pa2; pa2 = fp;
				}
				else if (d < da.w) {
					da.w = d;
					pa3 = fp;
				}
			}
		}
	}
	return da.x;
}

vec4 bi_voronoi_da(sampler2D tab, vec3 p)
{
	vec4 da;
	vec3 pa0, pa1, pa2, pa3;
	bi_voronoi(tab, p, 1.0, 0, da, pa0, pa1, pa2, pa3);
	return da;
}

/* ---------------------------------------------------------------------- */
/* cell noise */

float bi_cellnoise_u(vec3 p)
{
	p = (p + vec3(0.000001)) * 1.00001;
	int xi = int(floor(p.x));
	int yi = int(floor(p.y));
	int zi = int(floor(p.z));
	uint n = uint(xi + yi * 1301 + zi * 314159);
	n ^= (n << 13);
	return float(n * (n * n * 15731u + 789221u) + 1376312589u) / 4294967296.0;
}

vec3 bi_cellnoise_v(sampler2D tab, vec3 p)
{
	p = (p + vec3(0.000001)) * 1.00001;
	return bi_hashpnt3(tab, int(floor(p.x)), int(floor(p.y)), int(floor(p.z)));
}

/* ---------------------------------------------------------------------- */
/* noise bases */

/* unsigned, for BLI_gNoise and BLI_gTurbulence */
float bi_noise_u(sampler2D tab, int basis, vec3 p)
{
	if (basis == 1) return 0.5 + 0.5 * bi_org_perlin_noise(tab, p);
	if (basis == 2) return 0.5 + 0.5 * bi_new_perlin(tab, p);
	if (basis >= 3 && basis <= 8) {
		vec4 da = bi_voronoi_da(tab, p);
		if (basis == 3) return da.x;
		if (basis == 4) return da.y;
		if (basis == 5) return da.z;
		if (basis == 6) return da.w;
		if (basis == 7) return da.y - da.x;
		return min(10.0 * (da.y - da.x), 1.0);
	}
	if (basis == 14) return bi_cellnoise_u(p);
	return bi_org_blender_noise(tab, p);
}

/* signed, for the Musgrave functions */
float bi_noise_s(sampler2D tab, int basis, vec3 p)
{
	if (basis == 1) return bi_org_perlin_noise(tab, p);
	if (basis == 2) return bi_new_perlin(tab, p);
	if (basis >= 3 && basis <= 8) {
		vec4 da = bi_voronoi_da(tab, p);
		if (basis == 3) return 2.0 * da.x - 1.0;
		if (basis == 4) return 2.0 * da.y - 1.0;
		if (basis == 5) return 2.0 * da.z - 1.0;
		if (basis == 6) return 2.0 * da.w - 1.0;
		if (basis == 7) return 2.0 * (da.y - da.x) - 1.0;
		float t = 10.0 * (da.y - da.x);
		if (t > 1.0) return 1.0;
		return 2.0 * t - 1.0;
	}
	if (basis == 14) return 2.0 * bi_cellnoise_u(p) - 1.0;
	return 2.0 * bi_org_blender_noise(tab, p) - 1.0;
}

float bi_g_noise(sampler2D tab, float noisesize, vec3 p, bool hard, int basis)
{
	if (basis != 1 && basis != 2 && !(basis >= 3 && basis <= 8) && basis != 14) {
		p += vec3(1.0);
	}
	if (noisesize != 0.0) {
		p *= 1.0 / noisesize;
	}
	float n = bi_noise_u(tab, basis, p);
	return hard ? abs(2.0 * n - 1.0) : n;
}

float bi_g_turbulence(sampler2D tab, float noisesize, vec3 p, int oct, bool hard, int basis)
{
	float sum = 0.0, amp = 1.0, fscale = 1.0;
	if (basis != 1 && basis != 2 && !(basis >= 3 && basis <= 8) && basis != 14) {
		p += vec3(1.0);
	}
	if (noisesize != 0.0) {
		p *= 1.0 / noisesize;
	}
	for (int i = 0; i <= oct; i++) {
		float t = bi_noise_u(tab, basis, fscale * p);
		if (hard) t = abs(2.0 * t - 1.0);
		sum += t * amp;
		amp *= 0.5;
		fscale *= 2.0;
	}
	return sum * (float(1 << oct) / float((1 << (oct + 1)) - 1));
}

/* ---------------------------------------------------------------------- */
/* Musgrave fractals */

float bi_mg_fbm(sampler2D tab, vec3 p, float H, float lacunarity, float octaves, int basis)
{
	float value = 0.0, pwr = 1.0, pwHL = pow(lacunarity, -H);
	for (int i = 0; i < int(octaves); i++) {
		value += bi_noise_s(tab, basis, p) * pwr;
		pwr *= pwHL;
		p *= lacunarity;
	}
	float rmd = octaves - floor(octaves);
	if (rmd != 0.0) value += rmd * bi_noise_s(tab, basis, p) * pwr;
	return value;
}

float bi_mg_multi_fractal(sampler2D tab, vec3 p, float H, float lacunarity, float octaves, int basis)
{
	float value = 1.0, pwr = 1.0, pwHL = pow(lacunarity, -H);
	for (int i = 0; i < int(octaves); i++) {
		value *= (pwr * bi_noise_s(tab, basis, p) + 1.0);
		pwr *= pwHL;
		p *= lacunarity;
	}
	float rmd = octaves - floor(octaves);
	if (rmd != 0.0) value *= (rmd * bi_noise_s(tab, basis, p) * pwr + 1.0);
	return value;
}

float bi_mg_hetero_terrain(sampler2D tab, vec3 p, float H, float lacunarity, float octaves, float offset, int basis)
{
	float pwHL = pow(lacunarity, -H);
	float pwr = pwHL;
	float value = offset + bi_noise_s(tab, basis, p);
	float increment;
	p *= lacunarity;
	for (int i = 1; i < int(octaves); i++) {
		increment = (bi_noise_s(tab, basis, p) + offset) * pwr * value;
		value += increment;
		pwr *= pwHL;
		p *= lacunarity;
	}
	float rmd = octaves - floor(octaves);
	if (rmd != 0.0) {
		increment = (bi_noise_s(tab, basis, p) + offset) * pwr * value;
		value += rmd * increment;
	}
	return value;
}

float bi_mg_hybrid_multi_fractal(sampler2D tab, vec3 p, float H, float lacunarity, float octaves, float offset,
                                 float gain, int basis)
{
	float pwHL = pow(lacunarity, -H);
	float pwr = pwHL;
	float result = bi_noise_s(tab, basis, p) + offset;
	float weight = gain * result;
	float signal;
	int i;
	p *= lacunarity;
	for (i = 1; (weight > 0.001) && (i < int(octaves)); i++) {
		if (weight > 1.0) weight = 1.0;
		signal = (bi_noise_s(tab, basis, p) + offset) * pwr;
		pwr *= pwHL;
		result += weight * signal;
		weight *= gain * signal;
		p *= lacunarity;
	}
	float rmd = octaves - floor(octaves);
	if (rmd != 0.0) result += rmd * ((bi_noise_s(tab, basis, p) + offset) * pwr);
	return result;
}

float bi_mg_ridged_multi_fractal(sampler2D tab, vec3 p, float H, float lacunarity, float octaves, float offset,
                                 float gain, int basis)
{
	float pwHL = pow(lacunarity, -H);
	float pwr = pwHL;
	float signal = offset - abs(bi_noise_s(tab, basis, p));
	signal *= signal;
	float result = signal;
	float weight;
	for (int i = 1; i < int(octaves); i++) {
		p *= lacunarity;
		weight = clamp(signal * gain, 0.0, 1.0);
		signal = offset - abs(bi_noise_s(tab, basis, p));
		signal *= signal;
		signal *= weight;
		result += signal * pwr;
		pwr *= pwHL;
	}
	return result;
}

float bi_mg_vl_noise(sampler2D tab, vec3 p, float distortion, int nbas1, int nbas2)
{
	vec3 rv;
	rv.x = bi_noise_s(tab, nbas1, p + vec3(13.5)) * distortion;
	rv.y = bi_noise_s(tab, nbas1, p) * distortion;
	rv.z = bi_noise_s(tab, nbas1, p - vec3(13.5)) * distortion;
	return bi_noise_s(tab, nbas2, p + rv);
}

/* ---------------------------------------------------------------------- */
/* the texture types (render_texture.c) */

/* p0: type, stype, noisebasis, noisebasis2
 * p1: noisesize, noisedepth, hard (noisetype), turbul
 * p2: bright, contrast, saturation, no clamp
 * p3: rfac, gfac, bfac, flip blend
 * p4: mg_H, mg_lacunarity, mg_octaves, mg_offset
 * p5: mg_gain, ns_outscale, dist_amount, vn_mexp
 * p6: vn_w1, vn_w2, vn_w3, vn_w4
 * p7: vn_distm, vn_coltype, 0, 0 */

float bi_bricont(float tin, vec4 p2)
{
	tin = (tin - 0.5) * p2.y + p2.x - 0.5;
	if (p2.w == 0.0) {
		tin = clamp(tin, 0.0, 1.0);
	}
	return tin;
}

vec3 bi_bricont_rgb(vec3 rgb, vec4 p2, vec4 p3)
{
	rgb = p3.xyz * ((rgb - vec3(0.5)) * p2.y + vec3(p2.x - 0.5));
	if (p2.w == 0.0) {
		rgb = max(rgb, vec3(0.0));
	}
	if (p2.z != 1.0) {
		vec4 hsv;
		rgb_to_hsv(vec4(rgb, 1.0), hsv);
		hsv.y *= p2.z;
		vec4 col;
		hsv_to_rgb(hsv, col);
		rgb = col.rgb;
		if ((p2.z > 1.0) && (p2.w == 0.0)) {
			rgb = max(rgb, vec3(0.0));
		}
	}
	return rgb;
}

float bi_tex_sin(float a)
{
	return 0.5 + 0.5 * sin(a);
}

float bi_tex_saw(float a)
{
	const float b = 6.28318530717958647692;
	int n = int(a / b);
	a -= float(n) * b;
	if (a < 0.0) a += b;
	return a / b;
}

float bi_tex_tri(float a)
{
	const float b = 6.28318530717958647692;
	return 1.0 - 2.0 * abs(floor((a * (1.0 / b)) + 0.5) - (a * (1.0 / b)));
}

float bi_waveform(int wf, float a)
{
	if (wf == 1) return bi_tex_saw(a);
	if (wf == 2) return bi_tex_tri(a);
	return bi_tex_sin(a);
}

float bi_blend(vec3 t, int stype, bool flip)
{
	float x = flip ? t.y : t.x;
	float y = flip ? t.x : t.y;
	float tin;
	if (stype == 0) {
		tin = (1.0 + x) / 2.0;
	}
	else if (stype == 1) {
		tin = (1.0 + x) / 2.0;
		tin = (tin < 0.0) ? 0.0 : tin * tin;
	}
	else if (stype == 2) {
		tin = (1.0 + x) / 2.0;
		if (tin <= 0.0) tin = 0.0;
		else if (tin >= 1.0) tin = 1.0;
		else {
			float s = tin * tin;
			tin = (3.0 * s - 2.0 * s * tin);
		}
	}
	else if (stype == 3) {
		tin = (2.0 + x + y) / 4.0;
	}
	else if (stype == 6) {
		tin = atan(y, x) / 6.28318530717958647692 + 0.5;
	}
	else {
		tin = 1.0 - sqrt(x * x + y * y + t.z * t.z);
		if (tin < 0.0) tin = 0.0;
		if (stype == 5) tin *= tin;
	}
	return tin;
}

float bi_wood_int(sampler2D tab, vec3 t, vec4 p0, vec4 p1)
{
	int wf = int(p0.w);
	int wt = int(p0.y);
	if (wf > 2 || wf < 0) wf = 0;
	bool hard = p1.z != 0.0;
	float wi = 0.0;
	if (wt == 0) {
		wi = bi_waveform(wf, (t.x + t.y + t.z) * 10.0);
	}
	else if (wt == 1) {
		wi = bi_waveform(wf, sqrt(dot(t, t)) * 20.0);
	}
	else if (wt == 2) {
		wi = p1.w * bi_g_noise(tab, p1.x, t, hard, int(p0.z));
		wi = bi_waveform(wf, (t.x + t.y + t.z) * 10.0 + wi);
	}
	else if (wt == 3) {
		wi = p1.w * bi_g_noise(tab, p1.x, t, hard, int(p0.z));
		wi = bi_waveform(wf, sqrt(dot(t, t)) * 20.0 + wi);
	}
	return wi;
}

float bi_marble_int(sampler2D tab, vec3 t, vec4 p0, vec4 p1)
{
	int wf = int(p0.w);
	int mt = int(p0.y);
	if (wf > 2 || wf < 0) wf = 0;
	float n = 5.0 * (t.x + t.y + t.z);
	float mi = n + p1.w * bi_g_turbulence(tab, p1.x, t, int(p1.y), p1.z != 0.0, int(p0.z));
	if (mt >= 0) {
		mi = bi_waveform(wf, mi);
		if (mt == 1) mi = sqrt(mi);
		else if (mt == 2) mi = sqrt(sqrt(mi));
	}
	return mi;
}

vec3 bi_magic(vec3 t, float depth, float turbul)
{
	int n = int(depth);
	float turb = turbul / 5.0;
	float x = sin((t.x + t.y + t.z) * 5.0);
	float y = cos((-t.x + t.y - t.z) * 5.0);
	float z = -cos((-t.x - t.y + t.z) * 5.0);
	if (n > 0) {
		x *= turb; y *= turb; z *= turb;
		y = -cos(x - y + z); y *= turb;
		if (n > 1) {
			x = cos(x - y - z); x *= turb;
			if (n > 2) {
				z = sin(-x - y - z); z *= turb;
				if (n > 3) {
					x = -cos(-x + y - z); x *= turb;
					if (n > 4) {
						y = -sin(-x + y + z); y *= turb;
						if (n > 5) {
							y = -cos(-x + y + z); y *= turb;
							if (n > 6) {
								x = cos(x + y + z); x *= turb;
								if (n > 7) {
									z = sin(x + y - z); z *= turb;
									if (n > 8) {
										x = -cos(-x - y + z); x *= turb;
										if (n > 9) {
											y = -sin(x - y + z); y *= turb;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	if (turb != 0.0) {
		turb *= 2.0;
		x /= turb; y /= turb; z /= turb;
	}
	return vec3(0.5 - x, 0.5 - y, 0.5 - z);
}

/* "Noise" is random on every render sample; this is a fixed noise with the
 * same distribution (a hash of the position) */
float bi_texnoise(vec3 t, float depth)
{
	vec3 q = floor(t * 4096.0);
	uint n = uint(int(q.x) + int(q.y) * 1301 + int(q.z) * 314159);
	n ^= (n << 13);
	n = n * (n * n * 15731u + 789221u) + 1376312589u;
	n ^= (n >> 16);
	n *= 2246822519u;
	n ^= (n >> 13);
	int ran = int(n >> 1);
	float div = 3.0;
	int shift = 29;
	int val = ((ran >> shift) & 3);
	int loop = int(depth);
	while (loop-- > 0) {
		shift -= 2;
		val *= ((ran >> shift) & 3);
		div *= 3.0;
	}
	return float(val) / div;
}

float bi_voronoi_tex(sampler2D tab, vec3 t, vec4 p5, vec4 p6, vec4 p7, out vec3 rgb)
{
	vec4 aw = abs(p6);
	float sc = aw.x + aw.y + aw.z + aw.w;
	if (sc != 0.0) sc = p5.y / sc;
	vec4 da;
	vec3 pa0, pa1, pa2, pa3;
	bi_voronoi(tab, t, p5.w, int(p7.x), da, pa0, pa1, pa2, pa3);
	float tin = sc * abs(dot(p6, da));
	rgb = vec3(0.0);
	int coltype = int(p7.y);
	if (coltype != 0) {
		rgb = aw.x * bi_cellnoise_v(tab, pa0);
		rgb += aw.y * bi_cellnoise_v(tab, pa1);
		rgb += aw.z * bi_cellnoise_v(tab, pa2);
		rgb += aw.w * bi_cellnoise_v(tab, pa3);
		if (coltype >= 2) {
			float t1 = (da.y - da.x) * 10.0;
			if (t1 > 1.0) t1 = 1.0;
			if (coltype == 3) t1 *= tin; else t1 *= sc;
			rgb *= t1;
		}
		else {
			rgb *= sc;
		}
	}
	return tin;
}

/* the intensity, and the colour when the type gives one (rgb.a = 1) or not
 * (rgb.a = 0) */
float bi_multitex(sampler2D tab, vec3 t, vec4 p0, vec4 p1, vec4 p2, vec4 p3, vec4 p4, vec4 p5, vec4 p6, vec4 p7,
                  out vec4 trgb)
{
	int type = int(p0.x);
	int stype = int(p0.y);
	int basis = int(p0.z);
	bool hard = p1.z != 0.0;
	float tin = 0.0;
	trgb = vec4(0.0);

	if (type == 1) {  /* clouds */
		tin = bi_g_turbulence(tab, p1.x, t, int(p1.y), hard, basis);
		if (stype == 1) {
			vec3 rgb = vec3(tin,
			                bi_g_turbulence(tab, p1.x, t.yxz, int(p1.y), hard, basis),
			                bi_g_turbulence(tab, p1.x, t.yzx, int(p1.y), hard, basis));
			trgb = vec4(bi_bricont_rgb(rgb, p2, p3), 1.0);
			return tin;
		}
		return bi_bricont(tin, p2);
	}
	else if (type == 2) {  /* wood */
		return bi_bricont(bi_wood_int(tab, t, p0, p1), p2);
	}
	else if (type == 3) {  /* marble */
		return bi_bricont(bi_marble_int(tab, t, p0, p1), p2);
	}
	else if (type == 4) {  /* magic */
		vec3 rgb = bi_magic(t, p1.y, p1.w);
		tin = (1.0 / 3.0) * (rgb.r + rgb.g + rgb.b);
		trgb = vec4(bi_bricont_rgb(rgb, p2, p3), 1.0);
		return tin;
	}
	else if (type == 5) {  /* blend */
		return bi_bricont(bi_blend(t, stype, p3.w != 0.0), p2);
	}
	else if (type == 6) {  /* stucci */
		float b2 = bi_g_noise(tab, p1.x, t, hard, basis);
		float ofs = p1.w / 200.0;
		if (stype != 0) ofs *= (b2 * b2);
		tin = bi_g_noise(tab, p1.x, t + vec3(0.0, 0.0, ofs), hard, basis);
		if (stype == 2) tin = 1.0 - tin;
		return max(tin, 0.0);
	}
	else if (type == 7) {  /* noise */
		return bi_bricont(bi_texnoise(t, p1.y), p2);
	}
	else if (type == 11) {  /* musgrave */
		vec3 s = t * (1.0 / p1.x);
		if (stype == 0) tin = bi_mg_multi_fractal(tab, s, p4.x, p4.y, p4.z, basis);
		else if (stype == 3) tin = bi_mg_fbm(tab, s, p4.x, p4.y, p4.z, basis);
		else if (stype == 1) tin = bi_mg_ridged_multi_fractal(tab, s, p4.x, p4.y, p4.z, p4.w, p5.x, basis);
		else if (stype == 2) tin = bi_mg_hybrid_multi_fractal(tab, s, p4.x, p4.y, p4.z, p4.w, p5.x, basis);
		else tin = bi_mg_hetero_terrain(tab, s, p4.x, p4.y, p4.z, p4.w, basis);
		return bi_bricont(p5.y * tin, p2);
	}
	else if (type == 12) {  /* voronoi */
		vec3 rgb;
		tin = bi_voronoi_tex(tab, t * (1.0 / p1.x), p5, p6, p7, rgb);
		if (int(p7.y) != 0) {
			trgb = vec4(bi_bricont_rgb(rgb, p2, p3), 1.0);
			return tin;
		}
		return bi_bricont(tin, p2);
	}
	else if (type == 13) {  /* distorted noise */
		tin = bi_mg_vl_noise(tab, t * (1.0 / p1.x), p5.z, basis, int(p0.w));
		return bi_bricont(tin, p2);
	}
	return 0.0;
}

/* the intensity before brightness and contrast, for the normal of the
 * Texture node (tex_normal_derivate); 0 for the types without one */
float bi_raw_intensity(sampler2D tab, vec3 t, vec4 p0, vec4 p1, vec4 p4, vec4 p5, vec4 p6, vec4 p7)
{
	int type = int(p0.x);
	int stype = int(p0.y);
	int basis = int(p0.z);
	bool hard = p1.z != 0.0;
	if (type == 1) return bi_g_turbulence(tab, p1.x, t, int(p1.y), hard, basis);
	if (type == 2) return bi_wood_int(tab, t, p0, p1);
	if (type == 3) return bi_marble_int(tab, t, p0, p1);
	if (type == 11) {
		vec3 s = t * (1.0 / p1.x);
		float tin;
		if (stype == 0) tin = bi_mg_multi_fractal(tab, s, p4.x, p4.y, p4.z, basis);
		else if (stype == 3) tin = bi_mg_fbm(tab, s, p4.x, p4.y, p4.z, basis);
		else if (stype == 1) tin = bi_mg_ridged_multi_fractal(tab, s, p4.x, p4.y, p4.z, p4.w, p5.x, basis);
		else if (stype == 2) tin = bi_mg_hybrid_multi_fractal(tab, s, p4.x, p4.y, p4.z, p4.w, p5.x, basis);
		else tin = bi_mg_hetero_terrain(tab, s, p4.x, p4.y, p4.z, p4.w, basis);
		return p5.y * tin;
	}
	if (type == 12) {
		vec3 rgb;
		vec4 p5c = p5;
		return bi_voronoi_tex(tab, t * (1.0 / p1.x), p5c, p6, vec4(p7.x, 0.0, 0.0, 0.0), rgb);
	}
	if (type == 13) return bi_mg_vl_noise(tab, t * (1.0 / p1.x), p5.z, basis, int(p0.w));
	return 0.0;
}

bool bi_has_normal(vec4 p0)
{
	int type = int(p0.x);
	return type == 1 || type == 2 || type == 3 || type == 6 || type == 11 || type == 12 || type == 13;
}

vec4 bi_colorband(sampler2D band, float fac);
float bi_band_sum(sampler2D band, float fac);

/* The Texture node: value, colour and normal as the render's node gives
 * them (no MTex mapping; Stucci's value comes from its normal) */
float bi_texture_node(sampler2D tab, vec3 t, vec4 p0, vec4 p1, vec4 p2, vec4 p3, vec4 p4, vec4 p5, vec4 p6, vec4 p7,
                      bool use_band, sampler2D band, out vec4 color, out vec3 normal)
{
	float value;
	vec4 trgb;
	float tin = bi_multitex(tab, t, p0, p1, p2, p3, p4, p5, p6, p7, trgb);
	float nabla = p7.z;
	int type = int(p0.x);
	normal = vec3(0.0);

	if (type == 6) {
		/* stucci: its own offset, and its normal from the same samples */
		bool hard = p1.z != 0.0;
		int basis = int(p0.z);
		int stype = int(p0.y);
		float b2 = bi_g_noise(tab, p1.x, t, hard, basis);
		float ofs = p1.w / 200.0;
		if (stype != 0) ofs *= (b2 * b2);
		vec3 n = vec3(bi_g_noise(tab, p1.x, t + vec3(ofs, 0.0, 0.0), hard, basis),
		              bi_g_noise(tab, p1.x, t + vec3(0.0, ofs, 0.0), hard, basis),
		              bi_g_noise(tab, p1.x, t + vec3(0.0, 0.0, ofs), hard, basis));
		if (use_band) {
			float f0 = bi_band_sum(band, n.z);
			normal = (vec3(f0) - vec3(bi_band_sum(band, n.x), bi_band_sum(band, n.y), bi_band_sum(band, n.z))) / 3.0;
		}
		else {
			normal = vec3(n.z) - n;
		}
		if (stype == 2) normal = -normal;
		tin = clamp(0.5 + 0.7 * normal.x, 0.0, 1.0);
	}
	else if (bi_has_normal(p0)) {
		float r0 = bi_raw_intensity(tab, t, p0, p1, p4, p5, p6, p7);
		vec3 rn = vec3(bi_raw_intensity(tab, t + vec3(nabla, 0.0, 0.0), p0, p1, p4, p5, p6, p7),
		               bi_raw_intensity(tab, t + vec3(0.0, nabla, 0.0), p0, p1, p4, p5, p6, p7),
		               bi_raw_intensity(tab, t + vec3(0.0, 0.0, nabla), p0, p1, p4, p5, p6, p7));
		if (use_band) {
			float f0 = bi_band_sum(band, r0);
			normal = (vec3(f0) - vec3(bi_band_sum(band, rn.x), bi_band_sum(band, rn.y), bi_band_sum(band, rn.z))) / 3.0;
		}
		else {
			normal = vec3(r0) - rn;
		}
	}

	if (use_band) {
		color = bi_colorband(band, tin);
		value = color.a;
		color.a = 1.0;
	}
	else if (trgb.a != 0.0) {
		value = tin;
		color = vec4(trgb.rgb, 1.0);
	}
	else {
		value = tin;
		color = vec4(vec3(tin), 1.0);
	}
	return value;
}

/* the colour band (a 257-texel table), sampled at texel centres */
vec4 bi_colorband(sampler2D band, float fac)
{
	return texture2D(band, vec2((clamp(fac, 0.0, 1.0) * 256.0 + 0.5) / 257.0, 0.5));
}

float bi_band_sum(sampler2D band, float fac)
{
	vec4 c = bi_colorband(band, fac);
	return c.r + c.g + c.b;
}

float bi_luminance(vec3 rgb)
{
	return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
}

#endif  /* BIT_OPERATIONS */

/* ---------------------------------------------------------------------- */
/* called from gpu_material.c */

/* UV as the renderer gives it to a procedural: -1..1 */
void proc_texco_uv(vec3 uv, out vec3 co)
{
	co = vec3(uv.xy * 2.0 - vec2(1.0), 0.0);
}

/* the renderer's procedural mapping: projection (0 = none, 1..3 = X..Z),
 * then offset and size */
void proc_mapping(vec3 co, vec3 proj, vec3 size, vec3 ofs, out vec3 texvec)
{
	texvec.x = size.x * ((proj.x > 0.5) ? (co[int(proj.x) - 1] + ofs.x) : ofs.x);
	texvec.y = size.y * ((proj.y > 0.5) ? (co[int(proj.y) - 1] + ofs.y) : ofs.y);
	texvec.z = size.z * ((proj.z > 0.5) ? (co[int(proj.z) - 1] + ofs.z) : ofs.z);
}

/* window coordinates as the renderer gives them: -1..1 over the view
 * (camerafac: the camera border, as the Texture Coordinate node uses it) */
void proc_texco_window(vec3 co, vec4 camerafac, out vec3 window)
{
	vec4 projvec = gl_ProjectionMatrix * vec4(co, 1.0);
	vec2 win = (projvec.xy / projvec.w) * 0.5 + vec2(0.5);
	win = win * camerafac.xy + camerafac.zw;
	window = vec3(win * 2.0 - vec2(1.0), 0.0);
}

void proc_texture(
        vec3 texvec, vec4 p0, vec4 p1, vec4 p2, vec4 p3, vec4 p4, vec4 p5, vec4 p6, vec4 p7, sampler2D tab,
        out float tin, out vec4 trgb)
{
#ifdef BIT_OPERATIONS
	tin = bi_multitex(tab, texvec, p0, p1, p2, p3, p4, p5, p6, p7, trgb);
#else
	tin = 0.0;
	trgb = vec4(0.0);
#endif
}

/* with a colour band: its colour and alpha */
void proc_texture_band(
        vec3 texvec, vec4 p0, vec4 p1, vec4 p2, vec4 p3, vec4 p4, vec4 p5, vec4 p6, vec4 p7, sampler2D tab,
        sampler2D band, out float tin, out vec4 trgb)
{
#ifdef BIT_OPERATIONS
	vec4 unused;
	tin = bi_multitex(tab, texvec, p0, p1, p2, p3, p4, p5, p6, p7, unused);
	trgb = bi_colorband(band, tin);
#else
	tin = 0.0;
	trgb = vec4(0.0);
#endif
}

/* the Texture node of Blender Internal node materials */
void proc_texture_node(
        vec3 co, vec4 p0, vec4 p1, vec4 p2, vec4 p3, vec4 p4, vec4 p5, vec4 p6, vec4 p7, sampler2D tab,
        out float value, out vec4 color, out vec3 normal)
{
#ifdef BIT_OPERATIONS
	value = bi_texture_node(tab, co, p0, p1, p2, p3, p4, p5, p6, p7, false, tab, color, normal);
#else
	value = 0.0;
	color = vec4(0.0, 0.0, 0.0, 1.0);
	normal = vec3(0.0);
#endif
}

void proc_texture_node_band(
        vec3 co, vec4 p0, vec4 p1, vec4 p2, vec4 p3, vec4 p4, vec4 p5, vec4 p6, vec4 p7, sampler2D tab,
        sampler2D band, out float value, out vec4 color, out vec3 normal)
{
#ifdef BIT_OPERATIONS
	value = bi_texture_node(tab, co, p0, p1, p2, p3, p4, p5, p6, p7, true, band, color, normal);
#else
	value = 0.0;
	color = vec4(0.0, 0.0, 0.0, 1.0);
	normal = vec3(0.0);
#endif
}

/* bump: three samples per pixel, as the renderer's 3-tap bump does in 3D
 * (uvmode: the third channel is left out for UV coordinates) */
void proc_bump_tap3(
        vec3 texvec, vec4 p0, vec4 p1, vec4 p2, vec4 p3, vec4 p4, vec4 p5, vec4 p6, vec4 p7, sampler2D tab,
        float uvmode, float hScale, out float dBs, out float dBt)
{
#ifdef BIT_OPERATIONS
	vec3 dx = dFdx(texvec), dy = dFdy(texvec);
	vec3 ll = texvec, lr = texvec + dx, ul = texvec + dy;
	if (uvmode != 0.0) {
		ll.z = lr.z = ul.z = 0.0;
	}
	vec4 c;
	float Hll = bi_multitex(tab, ll, p0, p1, p2, p3, p4, p5, p6, p7, c);
	float Hlr = bi_multitex(tab, lr, p0, p1, p2, p3, p4, p5, p6, p7, c);
	float Hul = bi_multitex(tab, ul, p0, p1, p2, p3, p4, p5, p6, p7, c);
	dBs = hScale * (Hlr - Hll);
	dBt = hScale * (Hul - Hll);
#else
	dBs = dBt = 0.0;
#endif
}

void proc_bump_tap3_band(
        vec3 texvec, vec4 p0, vec4 p1, vec4 p2, vec4 p3, vec4 p4, vec4 p5, vec4 p6, vec4 p7, sampler2D tab,
        sampler2D band, float uvmode, float hScale, out float dBs, out float dBt)
{
#ifdef BIT_OPERATIONS
	vec3 dx = dFdx(texvec), dy = dFdy(texvec);
	vec3 ll = texvec, lr = texvec + dx, ul = texvec + dy;
	if (uvmode != 0.0) {
		ll.z = lr.z = ul.z = 0.0;
	}
	vec4 c;
	float Hll = bi_luminance(bi_colorband(band, bi_multitex(tab, ll, p0, p1, p2, p3, p4, p5, p6, p7, c)).rgb);
	float Hlr = bi_luminance(bi_colorband(band, bi_multitex(tab, lr, p0, p1, p2, p3, p4, p5, p6, p7, c)).rgb);
	float Hul = bi_luminance(bi_colorband(band, bi_multitex(tab, ul, p0, p1, p2, p3, p4, p5, p6, p7, c)).rgb);
	dBs = hScale * (Hlr - Hll);
	dBt = hScale * (Hul - Hll);
#else
	dBs = dBt = 0.0;
#endif
}
