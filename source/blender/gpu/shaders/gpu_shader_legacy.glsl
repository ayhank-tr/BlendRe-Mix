/* BGE-Remix P8.26: the old fixed-function look for the OpenGL emulation
 * (gpu_legacy.c). One program per sampler layout (LGL_T0..LGL_T3: 0 a 2D
 * texture, 1 a cube map, 2 a rectangle); everything else is the block below.
 * Compiled with "#version 450 core" and VERTEX or FRAGMENT defined. */

/* feature bits (flags.x) */
#define F_LIGHTING      1
#define F_TWO_SIDE      2
#define F_LOCAL_VIEWER  4
#define F_NORMALIZE     8
#define F_FLAT          16
#define F_POINT_SMOOTH  32
#define F_POLY_STIPPLE  64
#define F_COLOR_MAT     128
#define F_FOG           256
#define F_LINE_STIPPLE  512
#define F_ALPHA_TEST    1024

layout(std140, binding = 15) uniform LglState {
	mat4 mvp;
	mat4 mv;
	mat4 nmat;            /* the normal matrix in the upper 3x3 */
	mat4 texmat[4];
	vec4 clip[6];         /* eye-space planes */
	vec4 l_pos[8];        /* eye space */
	vec4 l_amb[8];
	vec4 l_dif[8];
	vec4 l_spec[8];
	vec4 l_spot[8];       /* direction (eye space), cos(cutoff) (-2: no spot) */
	vec4 l_att[8];        /* constant, linear, quadratic, spot exponent */
	vec4 m_emi[2];
	vec4 m_amb[2];
	vec4 m_dif[2];
	vec4 m_spec[2];
	vec4 m_shin;          /* front, back */
	vec4 scene_amb;
	ivec4 flags;          /* features, lamps on (bits), colour material (parameter), its faces (1 front, 2 back) */
	ivec4 modes;          /* alpha function (0x200 + n), fog mode (1 linear, 2 exp, 3 exp2), line pattern, line factor */
	vec4 fogv;            /* start, end, density, alpha reference */
	vec4 fog_color;
	ivec4 tex_on;         /* units with a texture on */
	ivec4 env[4];         /* mode (0 modulate, 1 replace, 2 decal, 3 blend, 4 add, 5 combine), base format, combine rgb, combine alpha */
	ivec4 env_src[4];     /* rgb sources, alpha sources (4 bits each), rgb operands, alpha operands */
	vec4 env_color[4];
	vec4 env_scale[4];    /* rgb scale, alpha scale */
	ivec4 gen[4];         /* s, t, r, q: 0 off, 1 object, 2 eye, 3 sphere, 4 reflection, 5 normal */
	vec4 gen_obj[16];     /* unit * 4 + coordinate */
	vec4 gen_eye[16];
	uvec4 pstip[8];       /* 32 rows: bit x on */
	vec4 px_scale;        /* pixel transfer */
	vec4 px_bias;
};

uniform int u_prim;       /* 0 points, 1 lines, 2 polygons */
uniform int u_mode;       /* 0 primitives, 1 pixels, 2 bitmap */
uniform vec4 u_point;     /* points: size, snapped (the old aliased points' centres) */
uniform vec4 u_vp;        /* the viewport */

#ifdef VERTEX

layout(location = 0) in vec4 a_pos;
layout(location = 1) in vec4 a_col;
layout(location = 2) in vec3 a_nor;
layout(location = 3) in vec4 a_tex0;
layout(location = 4) in vec4 a_tex1;
layout(location = 5) in vec4 a_tex2;
layout(location = 6) in vec4 a_tex3;
layout(location = 7) in float a_dist;

out vec4 v_front;
out vec4 v_back;
flat out vec4 v_front_flat;
flat out vec4 v_back_flat;
out vec4 v_tex[4];
noperspective out float v_dist;
out float v_fog;

vec4 lit(vec3 n, vec3 eye, int face, vec4 col)
{
	vec4 amb = m_amb[face], dif = m_dif[face], spec = m_spec[face], emi = m_emi[face];
	if ((flags.x & F_COLOR_MAT) != 0 && (flags.w & (1 << face)) != 0) {
		int p = flags.z;
		if (p == 0x1200) amb = col;
		else if (p == 0x1201) dif = col;
		else if (p == 0x1202) spec = col;
		else if (p == 0x1600) emi = col;
		else { amb = col; dif = col; }
	}
	vec3 c = emi.rgb + amb.rgb * scene_amb.rgb;
	vec3 V = (flags.x & F_LOCAL_VIEWER) != 0 ? normalize(-eye) : vec3(0.0, 0.0, 1.0);
	float shin = (face == 0) ? m_shin.x : m_shin.y;
	for (int i = 0; i < 8; i++) {
		if ((flags.y & (1 << i)) == 0) {
			continue;
		}
		vec3 L;
		float att = 1.0;
		if (l_pos[i].w != 0.0) {
			vec3 d = l_pos[i].xyz / l_pos[i].w - eye;
			float dist = length(d);
			L = (dist > 0.0) ? d / dist : vec3(0.0, 0.0, 1.0);
			att = 1.0 / max(l_att[i].x + l_att[i].y * dist + l_att[i].z * dist * dist, 1e-8);
		}
		else {
			L = normalize(l_pos[i].xyz);
		}
		if (l_spot[i].w > -1.5) {
			float cs = dot(-L, normalize(l_spot[i].xyz));
			att *= (cs >= l_spot[i].w) ? pow(max(cs, 0.0), l_att[i].w) : 0.0;
		}
		float nl = dot(n, L);
		float sp = 0.0;
		if (nl > 0.0) {
			float nh = max(dot(n, normalize(L + V)), 0.0);
			sp = (shin > 0.0) ? pow(nh, shin) : 1.0;
		}
		c += att * (amb.rgb * l_amb[i].rgb + max(nl, 0.0) * dif.rgb * l_dif[i].rgb + sp * spec.rgb * l_spec[i].rgb);
	}
	return vec4(clamp(c, 0.0, 1.0), clamp(dif.a, 0.0, 1.0));
}

void main()
{
	vec4 eye = mv * a_pos;
	if (u_mode != 0) {
		/* pixels and bitmaps: the corners are given in clip space */
		gl_Position = a_pos;
	}
	else {
		gl_Position = mvp * a_pos;
		if (u_prim == 0 && u_point.y > 0.5 && gl_Position.w > 0.0) {
			/* an aliased point's centre as the old rules put it: on a pixel's
			 * centre for odd sizes, on a corner for even ones */
			vec2 ndc = gl_Position.xy / gl_Position.w;
			vec2 win = (ndc * 0.5 + 0.5) * u_vp.zw + u_vp.xy;
			float size = max(floor(u_point.x + 0.5), 1.0);
			vec2 c = (mod(size, 2.0) == 0.0) ? floor(win + 0.5) : floor(win) + 0.5;
			gl_Position.xy = ((c - u_vp.xy) / u_vp.zw * 2.0 - 1.0) * gl_Position.w;
		}
	}
	gl_ClipDistance[0] = dot(clip[0], eye);
	gl_ClipDistance[1] = dot(clip[1], eye);
	gl_ClipDistance[2] = dot(clip[2], eye);
	gl_ClipDistance[3] = dot(clip[3], eye);
	gl_ClipDistance[4] = dot(clip[4], eye);
	gl_ClipDistance[5] = dot(clip[5], eye);

	vec4 front = a_col, back = a_col;
	vec3 n = mat3(nmat) * a_nor;
	if ((flags.x & F_NORMALIZE) != 0) {
		n = normalize(n);
	}
	vec3 e3 = eye.xyz / ((eye.w != 0.0) ? eye.w : 1.0);
	if ((flags.x & F_LIGHTING) != 0 && u_mode == 0) {
		front = lit(n, e3, 0, a_col);
		back = ((flags.x & F_TWO_SIDE) != 0) ? lit(-n, e3, 1, a_col) : front;
	}
	v_front = front;
	v_back = back;
	v_front_flat = front;
	v_back_flat = back;

	vec4 tc[4];
	tc[0] = a_tex0;
	tc[1] = a_tex1;
	tc[2] = a_tex2;
	tc[3] = a_tex3;
	for (int u = 0; u < 4; u++) {
		vec4 t = tc[u];
		if (gen[u] != ivec4(0)) {
			vec3 un = normalize(e3);
			vec3 nn = normalize(n);
			vec3 r = un - 2.0 * nn * dot(nn, un);
			for (int k = 0; k < 4; k++) {
				int g = gen[u][k];
				if (g == 1) t[k] = dot(gen_obj[u * 4 + k], a_pos);
				else if (g == 2) t[k] = dot(gen_eye[u * 4 + k], eye);
				else if (g == 3) {
					float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
					t[k] = (k == 0) ? r.x / m + 0.5 : (k == 1) ? r.y / m + 0.5 : t[k];
				}
				else if (g == 4) t[k] = (k < 3) ? r[k] : t[k];
				else if (g == 5) t[k] = (k < 3) ? nn[k] : t[k];
			}
		}
		v_tex[u] = (u_mode != 0) ? t : texmat[u] * t;
	}
	v_dist = a_dist;
	v_fog = abs(e3.z);
}

#endif  /* VERTEX */

#ifdef FRAGMENT

in vec4 v_front;
in vec4 v_back;
flat in vec4 v_front_flat;
flat in vec4 v_back_flat;
in vec4 v_tex[4];
noperspective in float v_dist;
in float v_fog;

layout(location = 0) out vec4 o_col;

#if LGL_T0 == 1
layout(binding = 0) uniform samplerCube s0;
#  define SAMPLE0(t) texture(s0, (t).xyz)
#elif LGL_T0 == 2
layout(binding = 0) uniform sampler2DRect s0;
#  define SAMPLE0(t) texture(s0, (t).xy / (t).w)
#else
layout(binding = 0) uniform sampler2D s0;
#  define SAMPLE0(t) texture(s0, (t).xy / (t).w)
#endif
#if LGL_T1 == 1
layout(binding = 1) uniform samplerCube s1;
#  define SAMPLE1(t) texture(s1, (t).xyz)
#elif LGL_T1 == 2
layout(binding = 1) uniform sampler2DRect s1;
#  define SAMPLE1(t) texture(s1, (t).xy / (t).w)
#else
layout(binding = 1) uniform sampler2D s1;
#  define SAMPLE1(t) texture(s1, (t).xy / (t).w)
#endif
#if LGL_T2 == 1
layout(binding = 2) uniform samplerCube s2;
#  define SAMPLE2(t) texture(s2, (t).xyz)
#elif LGL_T2 == 2
layout(binding = 2) uniform sampler2DRect s2;
#  define SAMPLE2(t) texture(s2, (t).xy / (t).w)
#else
layout(binding = 2) uniform sampler2D s2;
#  define SAMPLE2(t) texture(s2, (t).xy / (t).w)
#endif
#if LGL_T3 == 1
layout(binding = 3) uniform samplerCube s3;
#  define SAMPLE3(t) texture(s3, (t).xyz)
#elif LGL_T3 == 2
layout(binding = 3) uniform sampler2DRect s3;
#  define SAMPLE3(t) texture(s3, (t).xy / (t).w)
#else
layout(binding = 3) uniform sampler2D s3;
#  define SAMPLE3(t) texture(s3, (t).xy / (t).w)
#endif
/* the emulation's own: pixels and bitmaps */
layout(binding = 15) uniform sampler2D s_px;

/* base formats */
#define B_RGBA 0
#define B_RGB 1
#define B_ALPHA 2
#define B_LUM 3
#define B_LUM_ALPHA 4
#define B_INTENSITY 5

vec4 combine_src(int src, int op, vec4 Ct, vec4 Cprev, vec4 Cprim, vec4 Cc, bool alpha)
{
	vec4 s = (src == 0) ? Ct : (src == 1) ? Cc : (src == 2) ? Cprim : Cprev;
	if (alpha) {
		/* alpha operands: 0 source alpha, 1 one minus it */
		float a = ((op & 1) != 0) ? 1.0 - s.a : s.a;
		return vec4(a);
	}
	vec3 c = (op == 0) ? s.rgb : (op == 1) ? 1.0 - s.rgb : (op == 2) ? vec3(s.a) : vec3(1.0 - s.a);
	return vec4(c, 0.0);
}

vec4 combine_fn(int fn, vec4 a0, vec4 a1, vec4 a2)
{
	if (fn == 0) return a0;                             /* replace */
	if (fn == 1) return a0 * a1;                        /* modulate */
	if (fn == 2) return a0 + a1;                        /* add */
	if (fn == 3) return a0 + a1 - 0.5;                  /* add signed */
	if (fn == 4) return a0 * a2 + a1 * (1.0 - a2);      /* interpolate */
	if (fn == 5) return a0 - a1;                        /* subtract */
	return vec4(4.0 * dot(a0.rgb - 0.5, a1.rgb - 0.5)); /* dot3 */
}

vec4 texenv(int u, vec4 Cp, vec4 Cprim, vec4 Ct)
{
	int mode = env[u].x, fmt = env[u].y;
	bool has_rgb = (fmt != B_ALPHA);
	bool has_a = (fmt == B_RGBA || fmt == B_ALPHA || fmt == B_LUM_ALPHA || fmt == B_INTENSITY);
	vec4 Cc = env_color[u];
	vec3 rgb = Cp.rgb;
	float a = Cp.a;
	if (mode == 0) {
		rgb = has_rgb ? Cp.rgb * Ct.rgb : Cp.rgb;
		a = has_a ? Cp.a * Ct.a : Cp.a;
	}
	else if (mode == 1) {
		rgb = has_rgb ? Ct.rgb : Cp.rgb;
		a = has_a ? Ct.a : Cp.a;
	}
	else if (mode == 2) {
		if (fmt == B_RGB) rgb = Ct.rgb;
		else if (fmt == B_RGBA) rgb = mix(Cp.rgb, Ct.rgb, Ct.a);
	}
	else if (mode == 3) {
		rgb = has_rgb ? mix(Cp.rgb, Cc.rgb, Ct.rgb) : Cp.rgb;
		a = (fmt == B_INTENSITY) ? mix(Cp.a, Cc.a, Ct.a) : has_a ? Cp.a * Ct.a : Cp.a;
	}
	else if (mode == 4) {
		rgb = has_rgb ? Cp.rgb + Ct.rgb : Cp.rgb;
		a = (fmt == B_INTENSITY) ? Cp.a + Ct.a : has_a ? Cp.a * Ct.a : Cp.a;
	}
	else {
		ivec4 s = env_src[u];
		vec4 r0 = combine_src(s.x & 15, s.z & 15, Ct, Cp, Cprim, Cc, false);
		vec4 r1 = combine_src((s.x >> 4) & 15, (s.z >> 4) & 15, Ct, Cp, Cprim, Cc, false);
		vec4 r2 = combine_src((s.x >> 8) & 15, (s.z >> 8) & 15, Ct, Cp, Cprim, Cc, false);
		vec4 q0 = combine_src(s.y & 15, s.w & 15, Ct, Cp, Cprim, Cc, true);
		vec4 q1 = combine_src((s.y >> 4) & 15, (s.w >> 4) & 15, Ct, Cp, Cprim, Cc, true);
		vec4 q2 = combine_src((s.y >> 8) & 15, (s.w >> 8) & 15, Ct, Cp, Cprim, Cc, true);
		vec4 rc = combine_fn(env[u].z, r0, r1, r2);
		vec4 ac = combine_fn(env[u].w, q0, q1, q2);
		rgb = rc.rgb * env_scale[u].x;
		a = (env[u].z == 7) ? rc.a * env_scale[u].y : ac.a * env_scale[u].y;
	}
	return clamp(vec4(rgb, a), 0.0, 1.0);
}

bool alpha_pass(float a)
{
	int f = modes.x;
	float r = fogv.w;
	if (f == 0x200) return false;     /* never */
	if (f == 0x201) return a < r;
	if (f == 0x202) return a == r;
	if (f == 0x203) return a <= r;
	if (f == 0x204) return a > r;
	if (f == 0x205) return a != r;
	if (f == 0x206) return a >= r;
	return true;
}

void main()
{
	if (u_mode == 2) {
		/* a bitmap: the raster colour where its bits are set */
		if (texture(s_px, v_tex[0].xy).r < 0.5) {
			discard;
		}
		o_col = v_front;
		return;
	}
	if (u_mode == 1) {
		vec4 c = texture(s_px, v_tex[0].xy) * px_scale + px_bias;
		c = clamp(c, 0.0, 1.0);
		if ((flags.x & F_ALPHA_TEST) != 0 && !alpha_pass(c.a)) {
			discard;
		}
		o_col = c;
		return;
	}

	bool flat_shade = (flags.x & F_FLAT) != 0;
	bool back = (flags.x & F_TWO_SIDE) != 0 && !gl_FrontFacing;
	vec4 prim = back ? (flat_shade ? v_back_flat : v_back) : (flat_shade ? v_front_flat : v_front);
	vec4 c = prim;

	if (tex_on.x != 0) c = texenv(0, c, prim, SAMPLE0(v_tex[0]));
	if (tex_on.y != 0) c = texenv(1, c, prim, SAMPLE1(v_tex[1]));
	if (tex_on.z != 0) c = texenv(2, c, prim, SAMPLE2(v_tex[2]));
	if (tex_on.w != 0) c = texenv(3, c, prim, SAMPLE3(v_tex[3]));

	if ((flags.x & F_FOG) != 0) {
		float f;
		if (modes.y == 1) f = (fogv.y - v_fog) / max(fogv.y - fogv.x, 1e-8);
		else if (modes.y == 2) f = exp(-fogv.z * v_fog);
		else f = exp(-(fogv.z * v_fog) * (fogv.z * v_fog));
		c.rgb = mix(fog_color.rgb, c.rgb, clamp(f, 0.0, 1.0));
	}

	if (u_prim == 0 && (flags.x & F_POINT_SMOOTH) != 0) {
		if (length(gl_PointCoord - vec2(0.5)) > 0.5) {
			discard;
		}
	}
	else if (u_prim == 1 && (flags.x & F_LINE_STIPPLE) != 0) {
		int bit = int(floor(v_dist / float(max(modes.w, 1)))) & 15;
		if (((modes.z >> bit) & 1) == 0) {
			discard;
		}
	}
	else if (u_prim == 2 && (flags.x & F_POLY_STIPPLE) != 0) {
		ivec2 p = ivec2(gl_FragCoord.xy) & 31;
		uint row = pstip[p.y >> 2][p.y & 3];
		if (((row >> uint(p.x)) & 1u) == 0u) {
			discard;
		}
	}

	if ((flags.x & F_ALPHA_TEST) != 0 && !alpha_pass(c.a)) {
		discard;
	}
	o_col = c;
}

#endif  /* FRAGMENT */
