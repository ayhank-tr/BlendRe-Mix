/* Converters */

float convert_rgba_to_float(vec4 color)
{
#ifdef USE_NEW_SHADING
	return color.r * 0.2126 + color.g * 0.7152 + color.b * 0.0722;
#else
	return (color.r + color.g + color.b) / 3.0;
#endif
}

float exp_blender(float f)
{
	return pow(2.71828182846, f);
}

float compatible_pow(float x, float y)
{
	if (y == 0.0) /* x^0 -> 1, including 0^0 */
		return 1.0;

	/* glsl pow doesn't accept negative x */
	if (x < 0.0) {
		if (mod(-y, 2.0) == 0.0)
			return pow(-x, y);
		else
			return -pow(-x, y);
	}
	else if (x == 0.0)
		return 0.0;

	return pow(x, y);
}

void rgb_to_hsv(vec4 rgb, out vec4 outcol)
{
	float cmax, cmin, h, s, v, cdelta;
	vec3 c;

	cmax = max(rgb[0], max(rgb[1], rgb[2]));
	cmin = min(rgb[0], min(rgb[1], rgb[2]));
	cdelta = cmax - cmin;

	v = cmax;
	if (cmax != 0.0)
		s = cdelta / cmax;
	else {
		s = 0.0;
		h = 0.0;
	}

	if (s == 0.0) {
		h = 0.0;
	}
	else {
		c = (vec3(cmax, cmax, cmax) - rgb.xyz) / cdelta;

		if (rgb.x == cmax) h = c[2] - c[1];
		else if (rgb.y == cmax) h = 2.0 + c[0] -  c[2];
		else h = 4.0 + c[1] - c[0];

		h /= 6.0;

		if (h < 0.0)
			h += 1.0;
	}

	outcol = vec4(h, s, v, rgb.w);
}

void hsv_to_rgb(vec4 hsv, out vec4 outcol)
{
	float i, f, p, q, t, h, s, v;
	vec3 rgb;

	h = hsv[0];
	s = hsv[1];
	v = hsv[2];

	if (s == 0.0) {
		rgb = vec3(v, v, v);
	}
	else {
		if (h == 1.0)
			h = 0.0;

		h *= 6.0;
		i = floor(h);
		f = h - i;
		rgb = vec3(f, f, f);
		p = v * (1.0 - s);
		q = v * (1.0 - (s * f));
		t = v * (1.0 - (s * (1.0 - f)));

		if (i == 0.0) rgb = vec3(v, t, p);
		else if (i == 1.0) rgb = vec3(q, v, p);
		else if (i == 2.0) rgb = vec3(p, v, t);
		else if (i == 3.0) rgb = vec3(p, q, v);
		else if (i == 4.0) rgb = vec3(t, p, v);
		else rgb = vec3(v, p, q);
	}

	outcol = vec4(rgb, hsv.w);
}

float srgb_to_linearrgb(float c)
{
	if (c < 0.04045)
		return (c < 0.0) ? 0.0 : c * (1.0 / 12.92);
	else
		return pow((c + 0.055) * (1.0 / 1.055), 2.4);
}

float linearrgb_to_srgb(float c)
{
	if (c < 0.0031308)
		return (c < 0.0) ? 0.0 : c * 12.92;
	else
		return 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

void srgb_to_linearrgb(vec4 col_from, out vec4 col_to)
{
	col_to.r = srgb_to_linearrgb(col_from.r);
	col_to.g = srgb_to_linearrgb(col_from.g);
	col_to.b = srgb_to_linearrgb(col_from.b);
	col_to.a = col_from.a;
}

void linearrgb_to_srgb(vec4 col_from, out vec4 col_to)
{
	col_to.r = linearrgb_to_srgb(col_from.r);
	col_to.g = linearrgb_to_srgb(col_from.g);
	col_to.b = linearrgb_to_srgb(col_from.b);
	col_to.a = col_from.a;
}

void color_to_normal(vec3 color, out vec3 normal)
{
	normal.x =  2.0 * ((color.r) - 0.5);
	normal.y = -2.0 * ((color.g) - 0.5);
	normal.z =  2.0 * ((color.b) - 0.5);
}

void color_to_normal_new_shading(vec3 color, out vec3 normal)
{
	normal.x =  2.0 * ((color.r) - 0.5);
	normal.y =  2.0 * ((color.g) - 0.5);
	normal.z =  2.0 * ((color.b) - 0.5);
}

void color_to_blender_normal_new_shading(vec3 color, out vec3 normal)
{
	normal.x =  2.0 * ((color.r) - 0.5);
	normal.y = -2.0 * ((color.g) - 0.5);
	normal.z = -2.0 * ((color.b) - 0.5);
}

#define M_PI 3.14159265358979323846
#define M_1_PI 0.31830988618379069

/*********** SHADER NODES ***************/

void vcol_attribute(vec4 attvcol, out vec4 vcol)
{
	vcol = vec4(attvcol.x, attvcol.y, attvcol.z, 1.0);
}

void uv_attribute(vec2 attuv, out vec3 uv)
{
	uv = vec3(attuv * 2.0 - vec2(1.0, 1.0), 0.0);
}

void geom(
        vec3 co, vec3 nor, mat4 viewinvmat, vec3 attorco, vec2 attuv, vec4 attvcol,
        out vec3 global, out vec3 local, out vec3 view, out vec3 orco, out vec3 uv,
        out vec3 normal, out vec4 vcol, out float vcol_alpha, out float frontback)
{
	local = co;
	view = (bge_proj[3][3] == 0.0) ? normalize(local) : vec3(0.0, 0.0, -1.0);
	global = (viewinvmat * vec4(local, 1.0)).xyz;
	orco = attorco;
	uv_attribute(attuv, uv);
	normal = -normalize(nor);   /* blender render normal is negated */
	vcol_attribute(attvcol, vcol);
	srgb_to_linearrgb(vcol, vcol);
	vcol_alpha = attvcol.a;
	frontback = (gl_FrontFacing) ? 1.0 : 0.0;
}

void particle_info(
        vec4 sprops, vec3 loc, vec3 vel, vec3 avel,
        out float index, out float age, out float life_time, out vec3 location,
        out float size, out vec3 velocity, out vec3 angular_velocity)
{
	index = sprops.x;
	age = sprops.y;
	life_time = sprops.z;
	size = sprops.w;

	location = loc;
	velocity = vel;
	angular_velocity = avel;
}

void vect_normalize(vec3 vin, out vec3 vout)
{
	vout = normalize(vin);
}

void direction_transform_m4v3(vec3 vin, mat4 mat, out vec3 vout)
{
	vout = (mat * vec4(vin, 0.0)).xyz;
}

void point_transform_m4v3(vec3 vin, mat4 mat, out vec3 vout)
{
	vout = (mat * vec4(vin, 1.0)).xyz;
}

void point_texco_remap_square(vec3 vin, out vec3 vout)
{
	vout = vec3(vin - vec3(0.5, 0.5, 0.5)) * 2.0;
}

void point_map_to_sphere(vec3 vin, out vec3 vout)
{
	float len = length(vin);
	float v, u;
	if (len > 0.0) {
		if (vin.x == 0.0 && vin.y == 0.0)
			u = 0.0;
		else
			u = (1.0 - atan(vin.x, vin.y) / M_PI) / 2.0;

		v = 1.0 - acos(vin.z / len) / M_PI;
	}
	else
		v = u = 0.0;

	vout = vec3(u, v, 0.0);
}

void point_map_to_tube(vec3 vin, out vec3 vout)
{
	float u, v;
	v = (vin.z + 1.0) * 0.5;
	float len = sqrt(vin.x * vin.x + vin.y * vin[1]);
	if (len > 0.0)
		u = (1.0 - (atan(vin.x / len, vin.y / len) / M_PI)) * 0.5;
	else
		v = u = 0.0;

	vout = vec3(u, v, 0.0);
}

void mapping(vec3 vec, mat4 mat, vec3 minvec, vec3 maxvec, float domin, float domax, out vec3 outvec)
{
	outvec = (mat * vec4(vec, 1.0)).xyz;
	if (domin == 1.0)
		outvec = max(outvec, minvec);
	if (domax == 1.0)
		outvec = min(outvec, maxvec);
}

void camera(vec3 co, out vec3 outview, out float outdepth, out float outdist)
{
	outdepth = abs(co.z);
	outdist = length(co);
	outview = normalize(co);
}

void lamp(
        vec4 col, float energy, vec3 lv, float dist, vec3 shadow, float visifac,
        out vec4 outcol, out vec3 outlv, out float outdist, out vec4 outshadow, out float outvisifac)
{
	outcol = col * energy;
	outlv = lv;
	outdist = dist;
	outshadow = vec4(shadow, 1.0);
	outvisifac = visifac;
}

void math_add(float val1, float val2, out float outval)
{
	outval = val1 + val2;
}

void math_subtract(float val1, float val2, out float outval)
{
	outval = val1 - val2;
}

void math_multiply(float val1, float val2, out float outval)
{
	outval = val1 * val2;
}

void math_divide(float val1, float val2, out float outval)
{
	if (val2 == 0.0)
		outval = 0.0;
	else
		outval = val1 / val2;
}

void math_sine(float val, out float outval)
{
	outval = sin(val);
}

void math_cosine(float val, out float outval)
{
	outval = cos(val);
}

void math_tangent(float val, out float outval)
{
	outval = tan(val);
}

void math_asin(float val, out float outval)
{
	if (val <= 1.0 && val >= -1.0)
		outval = asin(val);
	else
		outval = 0.0;
}

void math_acos(float val, out float outval)
{
	if (val <= 1.0 && val >= -1.0)
		outval = acos(val);
	else
		outval = 0.0;
}

void math_atan(float val, out float outval)
{
	outval = atan(val);
}

void math_pow(float val1, float val2, out float outval)
{
	if (val1 >= 0.0) {
		outval = compatible_pow(val1, val2);
	}
	else {
		float val2_mod_1 = mod(abs(val2), 1.0);

		if (val2_mod_1 > 0.999 || val2_mod_1 < 0.001)
			outval = compatible_pow(val1, floor(val2 + 0.5));
		else
			outval = 0.0;
	}
}

void math_log(float val1, float val2, out float outval)
{
	if (val1 > 0.0  && val2 > 0.0)
		outval = log2(val1) / log2(val2);
	else
		outval = 0.0;
}

void math_max(float val1, float val2, out float outval)
{
	outval = max(val1, val2);
}

void math_min(float val1, float val2, out float outval)
{
	outval = min(val1, val2);
}

void math_round(float val, out float outval)
{
	outval = floor(val + 0.5);
}

void math_less_than(float val1, float val2, out float outval)
{
	if (val1 < val2)
		outval = 1.0;
	else
		outval = 0.0;
}

void math_greater_than(float val1, float val2, out float outval)
{
	if (val1 > val2)
		outval = 1.0;
	else
		outval = 0.0;
}

void math_modulo(float val1, float val2, out float outval)
{
	if (val2 == 0.0)
		outval = 0.0;
	else
		outval = mod(val1, val2);

	/* change sign to match C convention, mod in GLSL will take absolute for negative numbers,
	 * see https://www.opengl.org/sdk/docs/man/html/mod.xhtml */
	outval = (val1 > 0.0) ? outval : outval - val2;
}

void math_abs(float val1, out float outval)
{
	outval = abs(val1);
}

void squeeze(float val, float width, float center, out float outval)
{
	outval = 1.0 / (1.0 + pow(2.71828183, -((val - center) * width)));
}

void vec_math_add(vec3 v1, vec3 v2, out vec3 outvec, out float outval)
{
	outvec = v1 + v2;
	outval = (abs(outvec[0]) + abs(outvec[1]) + abs(outvec[2])) / 3.0;
}

void vec_math_sub(vec3 v1, vec3 v2, out vec3 outvec, out float outval)
{
	outvec = v1 - v2;
	outval = (abs(outvec[0]) + abs(outvec[1]) + abs(outvec[2])) / 3.0;
}

void vec_math_average(vec3 v1, vec3 v2, out vec3 outvec, out float outval)
{
	outvec = v1 + v2;
	outval = length(outvec);
	outvec = normalize(outvec);
}
void vec_math_mix(float strength, vec3 v1, vec3 v2, out vec3 outvec)
{
	outvec = strength * v1 + (1 - strength) * v2;
}

void vec_math_dot(vec3 v1, vec3 v2, out vec3 outvec, out float outval)
{
	outvec = vec3(0, 0, 0);
	outval = dot(v1, v2);
}

void vec_math_cross(vec3 v1, vec3 v2, out vec3 outvec, out float outval)
{
	outvec = cross(v1, v2);
	outval = length(outvec);
	outvec /= outval;
}

void vec_math_normalize(vec3 v, out vec3 outvec, out float outval)
{
	outval = length(v);
	outvec = normalize(v);
}

void vec_math_negate(vec3 v, out vec3 outv)
{
	outv = -v;
}

void invert_z(vec3 v, out vec3 outv)
{
	v.z = -v.z;
	outv = v;
}

void normal(vec3 dir, vec3 nor, out vec3 outnor, out float outdot)
{
	outnor = nor;
	outdot = -dot(dir, nor);
}

void normal_new_shading(vec3 dir, vec3 nor, out vec3 outnor, out float outdot)
{
	outnor = normalize(nor);
	outdot = dot(normalize(dir), nor);
}

void curves_vec(float fac, vec3 vec, sampler2D curvemap, out vec3 outvec)
{
	outvec.x = texture(curvemap, vec2((vec.x + 1.0) * 0.5, 0.0)).x;
	outvec.y = texture(curvemap, vec2((vec.y + 1.0) * 0.5, 0.0)).y;
	outvec.z = texture(curvemap, vec2((vec.z + 1.0) * 0.5, 0.0)).z;

	if (fac != 1.0)
		outvec = (outvec * fac) + (vec * (1.0 - fac));

}

void curves_rgb(float fac, vec4 col, sampler2D curvemap, out vec4 outcol)
{
	outcol.r = texture(curvemap, vec2(texture(curvemap, vec2(col.r, 0.0)).a, 0.0)).r;
	outcol.g = texture(curvemap, vec2(texture(curvemap, vec2(col.g, 0.0)).a, 0.0)).g;
	outcol.b = texture(curvemap, vec2(texture(curvemap, vec2(col.b, 0.0)).a, 0.0)).b;

	if (fac != 1.0)
		outcol = (outcol * fac) + (col * (1.0 - fac));

	outcol.a = col.a;
}

void set_value(float val, out float outval)
{
	outval = val;
}

void set_rgb(vec3 col, out vec3 outcol)
{
	outcol = col;
}

void set_rgba(vec4 col, out vec4 outcol)
{
	outcol = col;
}

void set_value_zero(out float outval)
{
	outval = 0.0;
}

void set_value_one(out float outval)
{
	outval = 1.0;
}

void set_rgb_zero(out vec3 outval)
{
	outval = vec3(0.0);
}

void set_rgb_one(out vec3 outval)
{
	outval = vec3(1.0);
}

void set_rgba_zero(out vec4 outval)
{
	outval = vec4(0.0);
}

void set_rgba_one(out vec4 outval)
{
	outval = vec4(1.0);
}

void brightness_contrast(vec4 col, float brightness, float contrast, out vec4 outcol)
{
	float a = 1.0 + contrast;
	float b = brightness - contrast * 0.5;

	outcol.r = max(a * col.r + b, 0.0);
	outcol.g = max(a * col.g + b, 0.0);
	outcol.b = max(a * col.b + b, 0.0);
	outcol.a = col.a;
}

void mix_blend(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	outcol = mix(col1, col2, fac);
	outcol.a = col1.a;
}

void mix_add(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	outcol = mix(col1, col1 + col2, fac);
	outcol.a = col1.a;
}

void mix_mult(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	outcol = mix(col1, col1 * col2, fac);
	outcol.a = col1.a;
}

void mix_screen(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	float facm = 1.0 - fac;

	outcol = vec4(1.0) - (vec4(facm) + fac * (vec4(1.0) - col2)) * (vec4(1.0) - col1);
	outcol.a = col1.a;
}

void mix_overlay(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	float facm = 1.0 - fac;

	outcol = col1;

	if (outcol.r < 0.5)
		outcol.r *= facm + 2.0 * fac * col2.r;
	else
		outcol.r = 1.0 - (facm + 2.0 * fac * (1.0 - col2.r)) * (1.0 - outcol.r);

	if (outcol.g < 0.5)
		outcol.g *= facm + 2.0 * fac * col2.g;
	else
		outcol.g = 1.0 - (facm + 2.0 * fac * (1.0 - col2.g)) * (1.0 - outcol.g);

	if (outcol.b < 0.5)
		outcol.b *= facm + 2.0 * fac * col2.b;
	else
		outcol.b = 1.0 - (facm + 2.0 * fac * (1.0 - col2.b)) * (1.0 - outcol.b);
}

void mix_sub(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	outcol = mix(col1, col1 - col2, fac);
	outcol.a = col1.a;
}

void mix_div(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	float facm = 1.0 - fac;

	outcol = col1;

	if (col2.r != 0.0) outcol.r = facm * outcol.r + fac * outcol.r / col2.r;
	if (col2.g != 0.0) outcol.g = facm * outcol.g + fac * outcol.g / col2.g;
	if (col2.b != 0.0) outcol.b = facm * outcol.b + fac * outcol.b / col2.b;
}

void mix_diff(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	outcol = mix(col1, abs(col1 - col2), fac);
	outcol.a = col1.a;
}

void mix_dark(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	outcol.rgb = min(col1.rgb, col2.rgb * fac);
	outcol.a = col1.a;
}

void mix_light(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	outcol.rgb = max(col1.rgb, col2.rgb * fac);
	outcol.a = col1.a;
}

void mix_dodge(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	outcol = col1;

	if (outcol.r != 0.0) {
		float tmp = 1.0 - fac * col2.r;
		if (tmp <= 0.0)
			outcol.r = 1.0;
		else if ((tmp = outcol.r / tmp) > 1.0)
			outcol.r = 1.0;
		else
			outcol.r = tmp;
	}
	if (outcol.g != 0.0) {
		float tmp = 1.0 - fac * col2.g;
		if (tmp <= 0.0)
			outcol.g = 1.0;
		else if ((tmp = outcol.g / tmp) > 1.0)
			outcol.g = 1.0;
		else
			outcol.g = tmp;
	}
	if (outcol.b != 0.0) {
		float tmp = 1.0 - fac * col2.b;
		if (tmp <= 0.0)
			outcol.b = 1.0;
		else if ((tmp = outcol.b / tmp) > 1.0)
			outcol.b = 1.0;
		else
			outcol.b = tmp;
	}
}

void mix_burn(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	float tmp, facm = 1.0 - fac;

	outcol = col1;

	tmp = facm + fac * col2.r;
	if (tmp <= 0.0)
		outcol.r = 0.0;
	else if ((tmp = (1.0 - (1.0 - outcol.r) / tmp)) < 0.0)
		outcol.r = 0.0;
	else if (tmp > 1.0)
		outcol.r = 1.0;
	else
		outcol.r = tmp;

	tmp = facm + fac * col2.g;
	if (tmp <= 0.0)
		outcol.g = 0.0;
	else if ((tmp = (1.0 - (1.0 - outcol.g) / tmp)) < 0.0)
		outcol.g = 0.0;
	else if (tmp > 1.0)
		outcol.g = 1.0;
	else
		outcol.g = tmp;

	tmp = facm + fac * col2.b;
	if (tmp <= 0.0)
		outcol.b = 0.0;
	else if ((tmp = (1.0 - (1.0 - outcol.b) / tmp)) < 0.0)
		outcol.b = 0.0;
	else if (tmp > 1.0)
		outcol.b = 1.0;
	else
		outcol.b = tmp;
}

void mix_hue(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	float facm = 1.0 - fac;

	outcol = col1;

	vec4 hsv, hsv2, tmp;
	rgb_to_hsv(col2, hsv2);

	if (hsv2.y != 0.0) {
		rgb_to_hsv(outcol, hsv);
		hsv.x = hsv2.x;
		hsv_to_rgb(hsv, tmp);

		outcol = mix(outcol, tmp, fac);
		outcol.a = col1.a;
	}
}

void mix_sat(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	float facm = 1.0 - fac;

	outcol = col1;

	vec4 hsv, hsv2;
	rgb_to_hsv(outcol, hsv);

	if (hsv.y != 0.0) {
		rgb_to_hsv(col2, hsv2);

		hsv.y = facm * hsv.y + fac * hsv2.y;
		hsv_to_rgb(hsv, outcol);
	}
}

void mix_val(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	float facm = 1.0 - fac;

	vec4 hsv, hsv2;
	rgb_to_hsv(col1, hsv);
	rgb_to_hsv(col2, hsv2);

	hsv.z = facm * hsv.z + fac * hsv2.z;
	hsv_to_rgb(hsv, outcol);
}

void mix_color(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	float facm = 1.0 - fac;

	outcol = col1;

	vec4 hsv, hsv2, tmp;
	rgb_to_hsv(col2, hsv2);

	if (hsv2.y != 0.0) {
		rgb_to_hsv(outcol, hsv);
		hsv.x = hsv2.x;
		hsv.y = hsv2.y;
		hsv_to_rgb(hsv, tmp);

		outcol = mix(outcol, tmp, fac);
		outcol.a = col1.a;
	}
}

void mix_soft(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);
	float facm = 1.0 - fac;

	vec4 one = vec4(1.0);
	vec4 scr = one - (one - col2) * (one - col1);
	outcol = facm * col1 + fac * ((one - col1) * col2 * col1 + col1 * scr);
}

void mix_linear(float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	fac = clamp(fac, 0.0, 1.0);

	outcol = col1 + fac * (2.0 * (col2 - vec4(0.5)));
}

void valtorgb(float fac, sampler2D colormap, out vec4 outcol, out float outalpha)
{
	outcol = texture(colormap, vec2(fac, 0.0));
	outalpha = outcol.a;
}

void rgbtobw(vec4 color, out float outval)
{
#ifdef USE_NEW_SHADING
	outval = color.r * 0.2126 + color.g * 0.7152 + color.b * 0.0722;
#else
	outval = color.r * 0.35 + color.g * 0.45 + color.b * 0.2; /* keep these factors in sync with texture.h:RGBTOBW */
#endif
}

void invert(float fac, vec4 col, out vec4 outcol)
{
	outcol.xyz = mix(col.xyz, vec3(1.0, 1.0, 1.0) - col.xyz, fac);
	outcol.w = col.w;
}

void clamp_vec3(vec3 vec, vec3 min, vec3 max, out vec3 out_vec)
{
	out_vec = clamp(vec, min, max);
}

void clamp_val(float value, float min, float max, out float out_value)
{
	out_value = clamp(value, min, max);
}

void hue_sat(float hue, float sat, float value, float fac, vec4 col, out vec4 outcol)
{
	vec4 hsv;

	rgb_to_hsv(col, hsv);

	hsv[0] += (hue - 0.5);
	if (hsv[0] > 1.0) hsv[0] -= 1.0; else if (hsv[0] < 0.0) hsv[0] += 1.0;
	hsv[1] *= sat;
	if (hsv[1] > 1.0) hsv[1] = 1.0; else if (hsv[1] < 0.0) hsv[1] = 0.0;
	hsv[2] *= value;
	if (hsv[2] > 1.0) hsv[2] = 1.0; else if (hsv[2] < 0.0) hsv[2] = 0.0;

	hsv_to_rgb(hsv, outcol);

	outcol = mix(col, outcol, fac);
}

void separate_rgb(vec4 col, out float r, out float g, out float b)
{
	r = col.r;
	g = col.g;
	b = col.b;
}

void combine_rgb(float r, float g, float b, out vec4 col)
{
	col = vec4(r, g, b, 1.0);
}

void separate_xyz(vec3 vec, out float x, out float y, out float z)
{
	x = vec.r;
	y = vec.g;
	z = vec.b;
}

void combine_xyz(float x, float y, float z, out vec3 vec)
{
	vec = vec3(x, y, z);
}

void separate_hsv(vec4 col, out float h, out float s, out float v)
{
	vec4 hsv;

	rgb_to_hsv(col, hsv);
	h = hsv[0];
	s = hsv[1];
	v = hsv[2];
}

void combine_hsv(float h, float s, float v, out vec4 col)
{
	hsv_to_rgb(vec4(h, s, v, 1.0), col);
}

void output_node(vec4 rgb, float alpha, out vec4 outrgb)
{
	outrgb = vec4(rgb.rgb, alpha);
}

/*********** TEXTURES ***************/

void texture_flip_blend(vec3 vec, out vec3 outvec)
{
	outvec = vec.yxz;
}

void texture_blend_lin(vec3 vec, out float outval)
{
	outval = (1.0 + vec.x) / 2.0;
}

void texture_blend_quad(vec3 vec, out float outval)
{
	outval = max((1.0 + vec.x) / 2.0, 0.0);
	outval *= outval;
}

void texture_wood_sin(vec3 vec, out float value, out vec4 color, out vec3 normal)
{
	float a = sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z) * 20.0;
	float wi = 0.5 + 0.5 * sin(a);

	value = wi;
	color = vec4(wi, wi, wi, 1.0);
	normal = vec3(0.0, 0.0, 0.0);
}

void texture_image(vec3 vec, sampler2D ima, out float value, out vec4 color, out vec3 normal)
{
	color = texture(ima, (vec.xy + vec2(1.0, 1.0)) * 0.5);
	value = color.a;

	normal.x = 2.0 * (color.r - 0.5);
	normal.y = 2.0 * (0.5 - color.g);
	normal.z = 2.0 * (color.b - 0.5);
}

/************* MTEX *****************/

void texco_orco(vec3 attorco, out vec3 orco)
{
	orco = attorco;
}

/* BGE-Remix P8.5: orco in the mesh's texture space (orcomat: identity where
 * the attribute already is) */
void texco_orco_space(vec3 attorco, mat4 orcomat, out vec3 orco)
{
	orco = (orcomat * vec4(attorco, 1.0)).xyz;
}

void texco_uv(vec2 attuv, out vec3 uv)
{
	/* disabled for now, works together with leaving out mtex_2d_mapping
	   uv = vec3(attuv*2.0 - vec2(1.0, 1.0), 0.0); */
	uv = vec3(attuv, 0.0);
}

void texco_norm(vec3 normal, out vec3 outnormal)
{
	/* corresponds to shi->orn, which is negated so cancels
	   out blender normal negation */
	outnormal = normalize(normal);
}

void texco_tangent(vec4 tangent, out vec3 outtangent)
{
	outtangent = normalize(tangent.xyz);
}

void texco_global(mat4 viewinvmat, vec3 co, out vec3 global)
{
	global = (viewinvmat * vec4(co, 1.0)).xyz;
}

void texco_object(mat4 viewinvmat, mat4 obinvmat, vec3 co, out vec3 object)
{
	object = (obinvmat * (viewinvmat * vec4(co, 1.0))).xyz;
}

void texco_refl(vec3 vn, vec3 view, out vec3 ref)
{
	ref = view - 2.0 * dot(vn, view) * vn;
}

void shade_norm(vec3 normal, out vec3 outnormal)
{
	/* blender render normal is negated */
	outnormal = -normalize(normal);
}

void mtex_mirror(vec3 tcol, vec4 refcol, float tin, float colmirfac, out vec4 outrefcol)
{
	outrefcol = mix(refcol, vec4(1.0, tcol), tin * colmirfac);
}

void mtex_rgb_blend(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	float facm;

	fact *= facg;
	facm = 1.0 - fact;

	incol = fact * texcol + facm * outcol;
}

void mtex_rgb_mul(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	float facm;

	fact *= facg;
	facm = 1.0 - fact;

	incol = (facm + fact * texcol) * outcol;
}

void mtex_rgb_screen(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	float facm;

	fact *= facg;
	facm = 1.0 - fact;

	incol = vec3(1.0) - (vec3(facm) + fact * (vec3(1.0) - texcol)) * (vec3(1.0) - outcol);
}

void mtex_rgb_overlay(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	float facm;

	fact *= facg;
	facm = 1.0 - fact;

	if (outcol.r < 0.5)
		incol.r = outcol.r * (facm + 2.0 * fact * texcol.r);
	else
		incol.r = 1.0 - (facm + 2.0 * fact * (1.0 - texcol.r)) * (1.0 - outcol.r);

	if (outcol.g < 0.5)
		incol.g = outcol.g * (facm + 2.0 * fact * texcol.g);
	else
		incol.g = 1.0 - (facm + 2.0 * fact * (1.0 - texcol.g)) * (1.0 - outcol.g);

	if (outcol.b < 0.5)
		incol.b = outcol.b * (facm + 2.0 * fact * texcol.b);
	else
		incol.b = 1.0 - (facm + 2.0 * fact * (1.0 - texcol.b)) * (1.0 - outcol.b);
}

void mtex_rgb_sub(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	incol = -fact * facg * texcol + outcol;
}

void mtex_rgb_add(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	incol = fact * facg * texcol + outcol;
}

void mtex_rgb_div(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	float facm;

	fact *= facg;
	facm = 1.0 - fact;

	if (texcol.r != 0.0) incol.r = facm * outcol.r + fact * outcol.r / texcol.r;
	if (texcol.g != 0.0) incol.g = facm * outcol.g + fact * outcol.g / texcol.g;
	if (texcol.b != 0.0) incol.b = facm * outcol.b + fact * outcol.b / texcol.b;
}

void mtex_rgb_diff(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	float facm;

	fact *= facg;
	facm = 1.0 - fact;

	incol = facm * outcol + fact * abs(texcol - outcol);
}

void mtex_rgb_dark(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	float facm, col;

	fact *= facg;
	facm = 1.0 - fact;

	incol.r = min(outcol.r, texcol.r) * fact + outcol.r * facm;
	incol.g = min(outcol.g, texcol.g) * fact + outcol.g * facm;
	incol.b = min(outcol.b, texcol.b) * fact + outcol.b * facm;
}

void mtex_rgb_light(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	float facm, col;

	fact *= facg;

	col = fact * texcol.r;
	if (col > outcol.r) incol.r = col; else incol.r = outcol.r;
	col = fact * texcol.g;
	if (col > outcol.g) incol.g = col; else incol.g = outcol.g;
	col = fact * texcol.b;
	if (col > outcol.b) incol.b = col; else incol.b = outcol.b;
}

void mtex_rgb_hue(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	vec4 col;

	mix_hue(fact * facg, vec4(outcol, 1.0), vec4(texcol, 1.0), col);
	incol.rgb = col.rgb;
}

void mtex_rgb_sat(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	vec4 col;

	mix_sat(fact * facg, vec4(outcol, 1.0), vec4(texcol, 1.0), col);
	incol.rgb = col.rgb;
}

void mtex_rgb_val(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	vec4 col;

	mix_val(fact * facg, vec4(outcol, 1.0), vec4(texcol, 1.0), col);
	incol.rgb = col.rgb;
}

void mtex_rgb_color(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	vec4 col;

	mix_color(fact * facg, vec4(outcol, 1.0), vec4(texcol, 1.0), col);
	incol.rgb = col.rgb;
}

void mtex_rgb_soft(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	vec4 col;

	mix_soft(fact * facg, vec4(outcol, 1.0), vec4(texcol, 1.0), col);
	incol.rgb = col.rgb;
}

void mtex_rgb_linear(vec3 outcol, vec3 texcol, float fact, float facg, out vec3 incol)
{
	fact *= facg;

	if (texcol.r > 0.5)
		incol.r = outcol.r + fact * (2.0 * (texcol.r - 0.5));
	else
		incol.r = outcol.r + fact * (2.0 * (texcol.r) - 1.0);

	if (texcol.g > 0.5)
		incol.g = outcol.g + fact * (2.0 * (texcol.g - 0.5));
	else
		incol.g = outcol.g + fact * (2.0 * (texcol.g) - 1.0);

	if (texcol.b > 0.5)
		incol.b = outcol.b + fact * (2.0 * (texcol.b - 0.5));
	else
		incol.b = outcol.b + fact * (2.0 * (texcol.b) - 1.0);
}

void mtex_value_vars(inout float fact, float facg, out float facm)
{
	fact *= abs(facg);
	facm = 1.0 - fact;

	if (facg < 0.0) {
		float tmp = fact;
		fact = facm;
		facm = tmp;
	}
}

void mtex_value_blend(float outcol, float texcol, float fact, float facg, out float incol)
{
	float facm;
	mtex_value_vars(fact, facg, facm);

	incol = fact * texcol + facm * outcol;
}

void mtex_value_mul(float outcol, float texcol, float fact, float facg, out float incol)
{
	float facm;
	mtex_value_vars(fact, facg, facm);

	facm = 1.0 - facg;
	incol = (facm + fact * texcol) * outcol;
}

void mtex_value_screen(float outcol, float texcol, float fact, float facg, out float incol)
{
	float facm;
	mtex_value_vars(fact, facg, facm);

	facm = 1.0 - facg;
	incol = 1.0 - (facm + fact * (1.0 - texcol)) * (1.0 - outcol);
}

void mtex_value_sub(float outcol, float texcol, float fact, float facg, out float incol)
{
	float facm;
	mtex_value_vars(fact, facg, facm);

	fact = -fact;
	incol = fact * texcol + outcol;
}

void mtex_value_add(float outcol, float texcol, float fact, float facg, out float incol)
{
	float facm;
	mtex_value_vars(fact, facg, facm);

	fact = fact;
	incol = fact * texcol + outcol;
}

void mtex_value_div(float outcol, float texcol, float fact, float facg, out float incol)
{
	float facm;
	mtex_value_vars(fact, facg, facm);

	if (texcol != 0.0)
		incol = facm * outcol + fact * outcol / texcol;
	else
		incol = 0.0;
}

void mtex_value_diff(float outcol, float texcol, float fact, float facg, out float incol)
{
	float facm;
	mtex_value_vars(fact, facg, facm);

	incol = facm * outcol + fact * abs(texcol - outcol);
}

void mtex_value_dark(float outcol, float texcol, float fact, float facg, out float incol)
{
	float facm;
	mtex_value_vars(fact, facg, facm);

	incol = facm * outcol + fact * min(outcol, texcol);
}

void mtex_value_light(float outcol, float texcol, float fact, float facg, out float incol)
{
	float facm;
	mtex_value_vars(fact, facg, facm);

	float col = fact * texcol;
	if (col > outcol) incol = col; else incol = outcol;
}

void mtex_value_clamp_positive(float fac, out float outfac)
{
	outfac = max(fac, 0.0);
}

void mtex_value_clamp(float fac, out float outfac)
{
	outfac = clamp(fac, 0.0, 1.0);
}

void mtex_har_divide(float har, out float outhar)
{
	outhar = har / 128.0;
}

void mtex_har_multiply_clamp(float har, out float outhar)
{
	har *= 128.0;

	if (har < 1.0) outhar = 1.0;
	else if (har > 511.0) outhar = 511.0;
	else outhar = har;
}

void mtex_alpha_from_col(vec4 col, out float alpha)
{
	alpha = col.a;
}

void mtex_alpha_to_col(vec4 col, float alpha, out vec4 outcol)
{
	outcol = vec4(col.rgb, alpha);
}

void mtex_alpha_multiply_value(vec4 col, float value, out vec4 outcol)
{
	outcol = vec4(col.rgb, col.a * value);
}

void mtex_rgbtoint(vec4 rgb, out float intensity)
{
	intensity = dot(vec3(0.35, 0.45, 0.2), rgb.rgb);
}

void mtex_value_invert(float invalue, out float outvalue)
{
	outvalue = 1.0 - invalue;
}

void mtex_rgb_invert(vec4 inrgb, out vec4 outrgb)
{
	outrgb = vec4(vec3(1.0) - inrgb.rgb, inrgb.a);
}

void mtex_value_stencil(float stencil, float intensity, out float outstencil, out float outintensity)
{
	float fact = intensity;
	outintensity = intensity * stencil;
	outstencil = stencil * fact;
}

void mtex_rgb_stencil(float stencil, vec4 rgb, out float outstencil, out vec4 outrgb)
{
	float fact = rgb.a;
	outrgb = vec4(rgb.rgb, rgb.a * stencil);
	outstencil = stencil * fact;
}

void mtex_mapping_ofs(vec3 texco, vec3 ofs, out vec3 outtexco)
{
	outtexco = texco + ofs;
}

void mtex_mapping_size(vec3 texco, vec3 size, out vec3 outtexco)
{
	outtexco = size * texco;
}

void mtex_2d_mapping(vec3 vec, out vec3 outvec)
{
	outvec = vec3(vec.xy * 0.5 + vec2(0.5), vec.z);
}

vec3 mtex_2d_mapping(vec3 vec)
{
	return vec3(vec.xy * 0.5 + vec2(0.5), vec.z);
}

void mtex_cube_map(vec3 co, samplerCube ima, out float value, out vec4 color)
{
	color = texture(ima, co);
	value = 1.0;
}

void mtex_cube_map_refl_from_refldir(
        samplerCube ima, vec3 reflecteddirection, out float value, out vec4 color)
{
        color = texture(ima, reflecteddirection);
        value = color.a;
}

void mtex_cube_map_refl(
        samplerCube ima, vec3 vp, vec3 vn, mat4 viewmatrixinverse, mat4 viewmatrix,
        out float value, out vec4 color)
{
	vec3 viewdirection = vec3(viewmatrixinverse * vec4(vp, 0.0));
	vec3 normaldirection = normalize(vec3(vec4(vn, 0.0) * viewmatrix));
	vec3 reflecteddirection = reflect(viewdirection, normaldirection);
	color = texture(ima, reflecteddirection);
	value = 1.0;
}

void mtex_image(vec3 texco, sampler2D ima, out float value, out vec4 color)
{
	color = texture(ima, texco.xy);
	value = 1.0;
}

void mtex_normal(vec3 texco, sampler2D ima, out vec3 normal)
{
	// The invert of the red channel is to make
	// the normal map compliant with the outside world.
	// It needs to be done because in Blender
	// the normal used points inward.
	// Should this ever change this negate must be removed.
	vec4 color = texture(ima, texco.xy);
	normal = 2.0 * (vec3(-color.r, color.g, color.b) - vec3(-0.5, 0.5, 0.5));
}

void mtex_bump_normals_init(vec3 vN, out vec3 vNorg, out vec3 vNacc, out float fPrevMagnitude)
{
	vNorg = vN;
	vNacc = vN;
	fPrevMagnitude = 1.0;
}

/** helper method to extract the upper left 3x3 matrix from a 4x4 matrix */
mat3 to_mat3(mat4 m4)
{
	mat3 m3;
	m3[0] = m4[0].xyz;
	m3[1] = m4[1].xyz;
	m3[2] = m4[2].xyz;
	return m3;
}

void mtex_bump_init_objspace(
        vec3 surf_pos, vec3 surf_norm,
        mat4 mView, mat4 mViewInv, mat4 mObj, mat4 mObjInv,
        float fPrevMagnitude_in, vec3 vNacc_in,
        out float fPrevMagnitude_out, out vec3 vNacc_out,
        out vec3 vR1, out vec3 vR2, out float fDet)
{
	mat3 obj2view = to_mat3(bge_mv);
	mat3 view2obj = to_mat3(bge_mvinv);

	vec3 vSigmaS = view2obj * dFdx(surf_pos);
	vec3 vSigmaT = view2obj * dFdy(surf_pos);
	vec3 vN = normalize(surf_norm * obj2view);

	vR1 = cross(vSigmaT, vN);
	vR2 = cross(vN, vSigmaS);
	fDet = dot(vSigmaS, vR1);

	/* pretransform vNacc (in mtex_bump_apply) using the inverse transposed */
	vR1 = vR1 * view2obj;
	vR2 = vR2 * view2obj;
	vN = vN * view2obj;

	float fMagnitude = abs(fDet) * length(vN);
	vNacc_out = vNacc_in * (fMagnitude / fPrevMagnitude_in);
	fPrevMagnitude_out = fMagnitude;
}

void mtex_bump_init_texturespace(
        vec3 surf_pos, vec3 surf_norm,
        float fPrevMagnitude_in, vec3 vNacc_in,
        out float fPrevMagnitude_out, out vec3 vNacc_out,
        out vec3 vR1, out vec3 vR2, out float fDet)
{
	vec3 vSigmaS = dFdx(surf_pos);
	vec3 vSigmaT = dFdy(surf_pos);
	vec3 vN = surf_norm; /* normalized interpolated vertex normal */

	vR1 = normalize(cross(vSigmaT, vN));
	vR2 = normalize(cross(vN, vSigmaS));
	fDet = sign(dot(vSigmaS, vR1));

	float fMagnitude = abs(fDet);
	vNacc_out = vNacc_in * (fMagnitude / fPrevMagnitude_in);
	fPrevMagnitude_out = fMagnitude;
}

void mtex_bump_init_viewspace(
        vec3 surf_pos, vec3 surf_norm,
        float fPrevMagnitude_in, vec3 vNacc_in,
        out float fPrevMagnitude_out, out vec3 vNacc_out,
        out vec3 vR1, out vec3 vR2, out float fDet)
{
	vec3 vSigmaS = dFdx(surf_pos);
	vec3 vSigmaT = dFdy(surf_pos);
	vec3 vN = surf_norm; /* normalized interpolated vertex normal */

	vR1 = cross(vSigmaT, vN);
	vR2 = cross(vN, vSigmaS);
	fDet = dot(vSigmaS, vR1);

	float fMagnitude = abs(fDet);
	vNacc_out = vNacc_in * (fMagnitude / fPrevMagnitude_in);
	fPrevMagnitude_out = fMagnitude;
}

void mtex_bump_tap3(
        vec3 texco, sampler2D ima, float hScale,
        out float dBs, out float dBt)
{
	vec2 STll = texco.xy;
	vec2 STlr = texco.xy + dFdx(texco.xy);
	vec2 STul = texco.xy + dFdy(texco.xy);

	float Hll, Hlr, Hul;
	rgbtobw(texture(ima, STll), Hll);
	rgbtobw(texture(ima, STlr), Hlr);
	rgbtobw(texture(ima, STul), Hul);

	dBs = hScale * (Hlr - Hll);
	dBt = hScale * (Hul - Hll);
}

#ifdef BUMP_BICUBIC

void mtex_bump_bicubic(
        vec3 texco, sampler2D ima, float hScale,
        out float dBs, out float dBt )
{
	float Hl;
	float Hr;
	float Hd;
	float Hu;

	vec2 TexDx = dFdx(texco.xy);
	vec2 TexDy = dFdy(texco.xy);

	vec2 STl = texco.xy - 0.5 * TexDx;
	vec2 STr = texco.xy + 0.5 * TexDx;
	vec2 STd = texco.xy - 0.5 * TexDy;
	vec2 STu = texco.xy + 0.5 * TexDy;

	rgbtobw(texture(ima, STl), Hl);
	rgbtobw(texture(ima, STr), Hr);
	rgbtobw(texture(ima, STd), Hd);
	rgbtobw(texture(ima, STu), Hu);

	vec2 dHdxy = vec2(Hr - Hl, Hu - Hd);
	float fBlend = clamp(1.0 - textureQueryLOD(ima, texco.xy).x, 0.0, 1.0);
	if (fBlend != 0.0) {
		// the derivative of the bicubic sampling of level 0
		ivec2 vDim;
		vDim = textureSize(ima, 0);

		// taking the fract part of the texture coordinate is a hardcoded wrap mode.
		// this is acceptable as textures use wrap mode exclusively in 3D view elsewhere in blender.
		// this is done so that we can still get a valid texel with uvs outside the 0,1 range
		// by texelFetch below, as coordinates are clamped when using this function.
		vec2 fTexLoc = vDim * fract(texco.xy) - vec2(0.5, 0.5);
		ivec2 iTexLoc = ivec2(floor(fTexLoc));
		vec2 t = clamp(fTexLoc - iTexLoc, 0.0, 1.0);        // sat just to be pedantic

/*******************************************************************************************
 * This block will replace the one below when one channel textures are properly supported. *
 *******************************************************************************************
		vec4 vSamplesUL = textureGather(ima, (iTexLoc+ivec2(-1,-1) + vec2(0.5,0.5))/vDim);
		vec4 vSamplesUR = textureGather(ima, (iTexLoc+ivec2(1,-1) + vec2(0.5,0.5))/vDim);
		vec4 vSamplesLL = textureGather(ima, (iTexLoc+ivec2(-1,1) + vec2(0.5,0.5))/vDim);
		vec4 vSamplesLR = textureGather(ima, (iTexLoc+ivec2(1,1) + vec2(0.5,0.5))/vDim);

		mat4 H = mat4(vSamplesUL.w, vSamplesUL.x, vSamplesLL.w, vSamplesLL.x,
		            vSamplesUL.z, vSamplesUL.y, vSamplesLL.z, vSamplesLL.y,
		            vSamplesUR.w, vSamplesUR.x, vSamplesLR.w, vSamplesLR.x,
		            vSamplesUR.z, vSamplesUR.y, vSamplesLR.z, vSamplesLR.y);
 */
		ivec2 iTexLocMod = iTexLoc + ivec2(-1, -1);

		mat4 H;

		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				ivec2 iTexTmp = iTexLocMod + ivec2(i, j);

				// wrap texture coordinates manually for texelFetch to work on uvs oitside the 0,1 range.
				// this is guaranteed to work since we take the fractional part of the uv above.
				iTexTmp.x = (iTexTmp.x < 0) ? iTexTmp.x + vDim.x : ((iTexTmp.x >= vDim.x) ? iTexTmp.x - vDim.x : iTexTmp.x);
				iTexTmp.y = (iTexTmp.y < 0) ? iTexTmp.y + vDim.y : ((iTexTmp.y >= vDim.y) ? iTexTmp.y - vDim.y : iTexTmp.y);

				rgbtobw(texelFetch(ima, iTexTmp, 0), H[i][j]);
			}
		}

		float x = t.x, y = t.y;
		float x2 = x * x, x3 = x2 * x, y2 = y * y, y3 = y2 * y;

		vec4 X  = vec4(-0.5 * (x3 + x) + x2,    1.5 * x3 - 2.5 * x2 + 1, -1.5 * x3 + 2 * x2 + 0.5 * x, 0.5 * (x3 - x2));
		vec4 Y  = vec4(-0.5 * (y3 + y) + y2,    1.5 * y3 - 2.5 * y2 + 1, -1.5 * y3 + 2 * y2 + 0.5 * y, 0.5 * (y3 - y2));
		vec4 dX = vec4(-1.5 * x2 + 2 * x - 0.5, 4.5 * x2 - 5 * x,        -4.5 * x2 + 4 * x + 0.5,      1.5 * x2 - x);
		vec4 dY = vec4(-1.5 * y2 + 2 * y - 0.5, 4.5 * y2 - 5 * y,        -4.5 * y2 + 4 * y + 0.5,      1.5 * y2 - y);

		// complete derivative in normalized coordinates (mul by vDim)
		vec2 dHdST = vDim * vec2(dot(Y, H * dX), dot(dY, H * X));

		// transform derivative to screen-space
		vec2 dHdxy_bicubic = vec2(dHdST.x * TexDx.x + dHdST.y * TexDx.y,
		                          dHdST.x * TexDy.x + dHdST.y * TexDy.y);

		// blend between the two
		dHdxy = dHdxy * (1 - fBlend) + dHdxy_bicubic * fBlend;
	}

	dBs = hScale * dHdxy.x;
	dBt = hScale * dHdxy.y;
}

#endif

void mtex_bump_tap5(
        vec3 texco, sampler2D ima, float hScale,
        out float dBs, out float dBt)
{
	vec2 TexDx = dFdx(texco.xy);
	vec2 TexDy = dFdy(texco.xy);

	vec2 STc = texco.xy;
	vec2 STl = texco.xy - 0.5 * TexDx;
	vec2 STr = texco.xy + 0.5 * TexDx;
	vec2 STd = texco.xy - 0.5 * TexDy;
	vec2 STu = texco.xy + 0.5 * TexDy;

	float Hc, Hl, Hr, Hd, Hu;
	rgbtobw(texture(ima, STc), Hc);
	rgbtobw(texture(ima, STl), Hl);
	rgbtobw(texture(ima, STr), Hr);
	rgbtobw(texture(ima, STd), Hd);
	rgbtobw(texture(ima, STu), Hu);

	dBs = hScale * (Hr - Hl);
	dBt = hScale * (Hu - Hd);
}

void mtex_bump_deriv(
        vec3 texco, sampler2D ima, float ima_x, float ima_y, float hScale,
        out float dBs, out float dBt)
{
	float s = 1.0;      // negate this if flipped texture coordinate
	vec2 TexDx = dFdx(texco.xy);
	vec2 TexDy = dFdy(texco.xy);

	// this variant using a derivative map is described here
	// http://mmikkelsen3d.blogspot.com/2011/07/derivative-maps.html
	vec2 dim = vec2(ima_x, ima_y);
	vec2 dBduv = hScale * dim * (2.0 * texture(ima, texco.xy).xy - 1.0);

	dBs = dBduv.x * TexDx.x + s * dBduv.y * TexDx.y;
	dBt = dBduv.x * TexDy.x + s * dBduv.y * TexDy.y;
}

void mtex_bump_apply(
        float fDet, float dBs, float dBt, vec3 vR1, vec3 vR2, vec3 vNacc_in,
        out vec3 vNacc_out, out vec3 perturbed_norm)
{
	vec3 vSurfGrad = sign(fDet) * (dBs * vR1 + dBt * vR2);

	vNacc_out = vNacc_in - vSurfGrad;
	perturbed_norm = normalize(vNacc_out);
}

void mtex_bump_apply_texspace(
        float fDet, float dBs, float dBt, vec3 vR1, vec3 vR2,
        sampler2D ima, vec3 texco, float ima_x, float ima_y, vec3 vNacc_in,
        out vec3 vNacc_out, out vec3 perturbed_norm)
{
	vec2 TexDx = dFdx(texco.xy);
	vec2 TexDy = dFdy(texco.xy);

	vec3 vSurfGrad = sign(fDet) * (
	        dBs / length(vec2(ima_x * TexDx.x, ima_y * TexDx.y)) * vR1 +
	        dBt / length(vec2(ima_x * TexDy.x, ima_y * TexDy.y)) * vR2);

	vNacc_out = vNacc_in - vSurfGrad;
	perturbed_norm = normalize(vNacc_out);
}

void mtex_negate_texnormal(vec3 normal, out vec3 outnormal)
{
	outnormal = vec3(-normal.x, -normal.y, normal.z);
}

void mtex_nspace_tangent(vec4 tangent, vec3 normal, vec3 texnormal, out vec3 outnormal)
{
	vec3 B = tangent.w * cross(normal, tangent.xyz);

	outnormal = texnormal.x * tangent.xyz + texnormal.y * B + texnormal.z * normal;
	outnormal = normalize(outnormal);
}

void mtex_nspace_world(mat4 viewmat, vec3 texnormal, out vec3 outnormal)
{
	outnormal = normalize((viewmat * vec4(texnormal, 0.0)).xyz);
}

void mtex_nspace_object(vec3 texnormal, out vec3 outnormal)
{
	outnormal = normalize(bge_normalmat * texnormal);
}

void mtex_blend_normal(float norfac, vec3 normal, vec3 newnormal, out vec3 outnormal)
{
	outnormal = (1.0 - norfac) * normal + norfac * newnormal;
	outnormal = normalize(outnormal);
}

/******* MATERIAL *********/

void lamp_visibility_sun_hemi(vec3 lampvec, out vec3 lv, out float dist, out float visifac)
{
	lv = lampvec;
	dist = 1.0;
	visifac = 1.0;
}

void lamp_visibility_other(vec3 co, vec3 lampco, out vec3 lv, out float dist, out float visifac)
{
	lv = co - lampco;
	dist = length(lv);
	lv = normalize(lv);
	visifac = 1.0;
}

void lamp_falloff_invlinear(float lampdist, float dist, out float visifac)
{
	visifac = lampdist / (lampdist + dist);
}

void lamp_falloff_invsquare(float lampdist, float dist, out float visifac)
{
	visifac = lampdist / (lampdist + dist * dist);
}

void lamp_falloff_sliders(float lampdist, float ld1, float ld2, float dist, out float visifac)
{
	float lampdistkw = lampdist * lampdist;

	visifac = lampdist / (lampdist + ld1 * dist);
	visifac *= lampdistkw / (lampdistkw + ld2 * dist * dist);
}

void lamp_falloff_invcoefficients(float coeff_const, float coeff_lin, float coeff_quad, float dist, out float visifac)
{
	vec3 coeff = vec3(coeff_const, coeff_lin, coeff_quad);
	vec3 d_coeff = vec3(1.0, dist, dist * dist);
	float visifac_r = dot(coeff, d_coeff);
	if (visifac_r > 0.0)
		visifac = 1.0 / visifac_r;
	else
		visifac = 0.0;
}

void lamp_falloff_curve(float lampdist, sampler2D curvemap, float dist, out float visifac)
{
	visifac = texture(curvemap, vec2(dist / lampdist, 0.0)).x;
}

void lamp_visibility_sphere(float lampdist, float dist, float visifac, out float outvisifac)
{
	float t = lampdist - dist;

	outvisifac = visifac * max(t, 0.0) / lampdist;
}

void lamp_visibility_spot_square(vec3 lampvec, mat4 lampimat, vec2 scale, vec3 lv, out float inpr)
{
	if (dot(lv, lampvec) > 0.0) {
		vec3 lvrot = (lampimat * vec4(lv, 0.0)).xyz;
		/* without clever non-uniform scale, we could do: */
		// float x = max(abs(lvrot.x / lvrot.z), abs(lvrot.y / lvrot.z));
		float x = max(abs((lvrot.x / scale.x) / lvrot.z), abs((lvrot.y / scale.y) / lvrot.z));

		inpr = 1.0 / sqrt(1.0 + x * x);
	}
	else
		inpr = 0.0;
}

void lamp_visibility_spot_circle(vec3 lampvec, mat4 lampimat, vec2 scale, vec3 lv, out float inpr)
{
	/* without clever non-uniform scale, we could do: */
	// inpr = dot(lv, lampvec);
	if (dot(lv, lampvec) > 0.0) {
		vec3 lvrot = (lampimat * vec4(lv, 0.0)).xyz;
		float x = abs(lvrot.x / lvrot.z);
		float y = abs(lvrot.y / lvrot.z);

		float ellipse = abs((x * x) / (scale.x * scale.x) + (y * y) / (scale.y * scale.y));

		inpr = 1.0 / sqrt(1.0 + ellipse);
	}
	else
		inpr = 0.0;
}

void lamp_visibility_spot(float spotsi, float spotbl, float inpr, float visifac, out float outvisifac)
{
	float t = spotsi;

	if (inpr <= t) {
		outvisifac = 0.0;
	}
	else {
		t = inpr - t;

		/* soft area */
		if (spotbl != 0.0)
			inpr *= smoothstep(0.0, 1.0, t / spotbl);

		outvisifac = visifac * inpr;
	}
}

void lamp_visibility_clamp(float visifac, out float outvisifac)
{
	outvisifac = (visifac < 0.001) ? 0.0 : visifac;
}

void world_paper_view(vec3 vec, out vec3 outvec)
{
	vec3 nvec = normalize(vec);
	outvec = (bge_proj[3][3] == 0.0) ? vec3(nvec.x, 0.0, nvec.y) : vec3(0.0, 0.0, -1.0);
}

void world_zen_mapping(vec3 view, float zenup, float zendown, out float zenfac)
{
	if (view.z >= 0.0)
		zenfac = zenup;
	else
		zenfac = zendown;
}

void world_blend_paper_real(vec3 vec, out float blend)
{
	blend = abs(vec.y);
}

void world_blend_paper(vec3 vec, out float blend)
{
	blend = (vec.y + 1.0) * 0.5;
}

void world_blend_real(vec3 vec, out float blend)
{
	blend = abs(normalize(vec).z);
}

void world_blend(vec3 vec, out float blend)
{
	blend = (normalize(vec).z + 1) * 0.5;
}

void shade_view(vec3 co, out vec3 view)
{
	/* handle perspective/orthographic */
	view = (bge_proj[3][3] == 0.0) ? normalize(co) : vec3(0.0, 0.0, -1.0);
}

void shade_tangent_v(vec3 lv, vec3 tang, out vec3 vn)
{
	vec3 c = cross(lv, tang);
	vec3 vnor = cross(c, tang);

	vn = -normalize(vnor);
}

void shade_inp(vec3 vn, vec3 lv, out float inp)
{
	inp = dot(vn, lv);
}

void shade_is_no_diffuse(out float is)
{
	is = 0.0;
}

void shade_is_hemi(float inp, out float is)
{
	is = 0.5 * inp + 0.5;
}

float area_lamp_energy(mat4 area, vec3 co, vec3 vn)
{
	vec3 vec[4], c[4];
	float rad[4], fac;

	vec[0] = normalize(co - area[0].xyz);
	vec[1] = normalize(co - area[1].xyz);
	vec[2] = normalize(co - area[2].xyz);
	vec[3] = normalize(co - area[3].xyz);

	c[0] = normalize(cross(vec[0], vec[1]));
	c[1] = normalize(cross(vec[1], vec[2]));
	c[2] = normalize(cross(vec[2], vec[3]));
	c[3] = normalize(cross(vec[3], vec[0]));

	rad[0] = acos(dot(vec[0], vec[1]));
	rad[1] = acos(dot(vec[1], vec[2]));
	rad[2] = acos(dot(vec[2], vec[3]));
	rad[3] = acos(dot(vec[3], vec[0]));

	fac =  rad[0] * dot(vn, c[0]);
	fac += rad[1] * dot(vn, c[1]);
	fac += rad[2] * dot(vn, c[2]);
	fac += rad[3] * dot(vn, c[3]);

	return max(fac, 0.0);
}

void shade_inp_area(
        vec3 position, vec3 lampco, vec3 lampvec, vec3 vn, mat4 area, float areasize, float k,
        out float inp)
{
	vec3 co = position;
	vec3 vec = co - lampco;

	if (dot(vec, lampvec) < 0.0) {
		inp = 0.0;
	}
	else {
		float intens = area_lamp_energy(area, co, vn);

		inp = pow(intens * areasize, k);
	}
}

void shade_diffuse_oren_nayer(float nl, vec3 n, vec3 l, vec3 v, float rough, out float is)
{
	vec3 h = normalize(v + l);
	float nh = max(dot(n, h), 0.0);
	float nv = max(dot(n, v), 0.0);
	float realnl = dot(n, l);

	if (realnl < 0.0) {
		is = 0.0;
	}
	else if (nl < 0.0) {
		is = 0.0;
	}
	else {
		float vh = max(dot(v, h), 0.0);
		float Lit_A = acos(realnl);
		float View_A = acos(nv);

		vec3 Lit_B = normalize(l - realnl * n);
		vec3 View_B = normalize(v - nv * n);

		float t = max(dot(Lit_B, View_B), 0.0);

		float a, b;

		if (Lit_A > View_A) {
			a = Lit_A;
			b = View_A;
		}
		else {
			a = View_A;
			b = Lit_A;
		}

		float A = 1.0 - (0.5 * ((rough * rough) / ((rough * rough) + 0.33)));
		float B = 0.45 * ((rough * rough) / ((rough * rough) + 0.09));

		b *= 0.95;
		is = nl * (A + (B * t * sin(a) * tan(b)));
	}
}

void shade_diffuse_toon(vec3 n, vec3 l, vec3 v, float size, float tsmooth, out float is)
{
	float rslt = dot(n, l);
	float ang = acos(rslt);

	if (ang < size) is = 1.0;
	else if (ang > (size + tsmooth) || tsmooth == 0.0) is = 0.0;
	else is = 1.0 - ((ang - size) / tsmooth);
}

void shade_diffuse_minnaert(float nl, vec3 n, vec3 v, float darkness, out float is)
{
	if (nl <= 0.0) {
		is = 0.0;
	}
	else {
		float nv = max(dot(n, v), 0.0);

		if (darkness <= 1.0)
			is = nl * pow(max(nv * nl, 0.1), darkness - 1.0);
		else
			is = nl * pow(1.0001 - nv, darkness - 1.0);
	}
}

float fresnel_fac(vec3 view, vec3 vn, float grad, float fac)
{
	float t1, t2;
	float ffac;

	if (fac == 0.0) {
		ffac = 1.0;
	}
	else {
		t1 = dot(view, vn);
		if (t1 > 0.0) t2 = 1.0 + t1;
		else t2 = 1.0 - t1;

		t2 = grad + (1.0 - grad) * pow(t2, fac);

		if (t2 < 0.0) ffac = 0.0;
		else if (t2 > 1.0) ffac = 1.0;
		else ffac = t2;
	}

	return ffac;
}

void shade_diffuse_fresnel(vec3 vn, vec3 lv, vec3 view, float fac_i, float fac, out float is)
{
	is = fresnel_fac(lv, vn, fac_i, fac);
}

void shade_cubic(float is, out float outis)
{
	if (is > 0.0 && is < 1.0)
		outis = smoothstep(0.0, 1.0, is);
	else
		outis = is;
}

void shade_visifac(float i, float visifac, float refl, out float outi)
{
	/*if (i > 0.0)*/
	outi = max(i * visifac * refl, 0.0);
	/*else
	    outi = i;*/
}

void shade_tangent_v_spec(vec3 tang, out vec3 vn)
{
	vn = tang;
}

void shade_add_to_diffuse(float i, vec3 lampcol, vec3 col, out vec3 outcol)
{
	if (i > 0.0)
		outcol = i * lampcol * col;
	else
		outcol = vec3(0.0, 0.0, 0.0);
}

void shade_hemi_spec(vec3 vn, vec3 lv, vec3 view, float spec, float hard, float visifac, out float t)
{
	lv += view;
	lv = normalize(lv);

	t = dot(vn, lv);
	t = 0.5 * t + 0.5;

	t = visifac * spec * pow(t, hard);
}

void shade_phong_spec(vec3 n, vec3 l, vec3 v, float hard, out float specfac)
{
	vec3 h = normalize(l + v);
	float rslt = max(dot(h, n), 0.0);

	specfac = pow(rslt, hard);
}

void shade_cooktorr_spec(vec3 n, vec3 l, vec3 v, float hard, out float specfac)
{
	vec3 h = normalize(v + l);
	float nh = dot(n, h);

	if (nh < 0.0) {
		specfac = 0.0;
	}
	else {
		float nv = max(dot(n, v), 0.0);
		float i = pow(nh, hard);

		i = i / (0.1 + nv);
		specfac = i;
	}
}

void shade_blinn_spec(vec3 n, vec3 l, vec3 v, float refrac, float spec_power, out float specfac)
{
	if (refrac < 1.0) {
		specfac = 0.0;
	}
	else if (spec_power == 0.0) {
		specfac = 0.0;
	}
	else {
		if (spec_power < 100.0)
			spec_power = sqrt(1.0 / spec_power);
		else
			spec_power = 10.0 / spec_power;

		vec3 h = normalize(v + l);
		float nh = dot(n, h);
		if (nh < 0.0) {
			specfac = 0.0;
		}
		else {
			float nv = max(dot(n, v), 0.01);
			float nl = dot(n, l);
			if (nl <= 0.01) {
				specfac = 0.0;
			}
			else {
				float vh = max(dot(v, h), 0.01);

				float a = 1.0;
				float b = (2.0 * nh * nv) / vh;
				float c = (2.0 * nh * nl) / vh;

				float g = 0.0;

				if (a < b && a < c) g = a;
				else if (b < a && b < c) g = b;
				else if (c < a && c < b) g = c;

				float p = sqrt(((refrac * refrac) + (vh * vh) - 1.0));
				float f = ((((p - vh) * (p - vh)) / ((p + vh) * (p + vh))) *
				           (1.0 + ((((vh * (p + vh)) - 1.0) * ((vh * (p + vh)) - 1.0)) /
				                   (((vh * (p - vh)) + 1.0) * ((vh * (p - vh)) + 1.0)))));
				float ang = acos(nh);

				specfac = max(f * g * exp_blender((-(ang * ang) / (2.0 * spec_power * spec_power))), 0.0);
			}
		}
	}
}

void shade_wardiso_spec(vec3 n, vec3 l, vec3 v, float rms, out float specfac)
{
	vec3 h = normalize(l + v);
	float nh = max(dot(n, h), 0.001);
	float nv = max(dot(n, v), 0.001);
	float nl = max(dot(n, l), 0.001);
	float angle = tan(acos(nh));
	float alpha = max(rms, 0.001);

	specfac = nl * (1.0 / (4.0 * M_PI * alpha * alpha)) * (exp_blender(-(angle * angle) / (alpha * alpha)) / (sqrt(nv * nl)));
}

void shade_toon_spec(vec3 n, vec3 l, vec3 v, float size, float tsmooth, out float specfac)
{
	vec3 h = normalize(l + v);
	float rslt = dot(h, n);
	float ang = acos(rslt);

	if (ang < size) rslt = 1.0;
	else if (ang >= (size + tsmooth) || tsmooth == 0.0) rslt = 0.0;
	else rslt = 1.0 - ((ang - size) / tsmooth);

	specfac = rslt;
}

void shade_spec_area_inp(float specfac, float inp, out float outspecfac)
{
	outspecfac = specfac * inp;
}

void shade_spec_t(float shadfac, float spec, float visifac, float specfac, out float t)
{
	t = shadfac * spec * visifac * specfac;
}

void shade_add_spec(float t, vec3 lampcol, vec3 speccol, out vec3 outcol)
{
	outcol = t * lampcol * speccol;
}

void shade_add_mirror(vec3 mir, vec4 refcol, vec3 combined, out vec3 result)
{
	result = mir * refcol.gba + (vec3(1.0) - mir * refcol.rrr) * combined;
}

void alpha_spec_correction(vec3 spec, float spectra, float alpha, out float outalpha)
{
	if (spectra > 0.0) {
		float t = clamp(max(max(spec.r, spec.g), spec.b) * spectra, 0.0, 1.0);
		outalpha = (1.0 - t) * alpha + t;
	}
	else {
		outalpha = alpha;
	}
}

void shade_add(vec4 col1, vec4 col2, out vec4 outcol)
{
	outcol = col1 + col2;
}

void shade_madd(vec4 col, vec4 col1, vec4 col2, out vec4 outcol)
{
	outcol = col + col1 * col2;
}

void shade_add_clamped(vec4 col1, vec4 col2, out vec4 outcol)
{
	outcol = col1 + max(col2, vec4(0.0, 0.0, 0.0, 0.0));
}

void shade_madd_clamped(vec4 col, vec4 col1, vec4 col2, out vec4 outcol)
{
	outcol = col + max(col1 * col2, vec4(0.0, 0.0, 0.0, 0.0));
}

void env_apply(vec4 col, vec3 hor, vec3 zen, vec4 f, mat4 vm, vec3 vn, out vec4 outcol)
{
	vec3 vv = normalize(vm[2].xyz);
	float skyfac = 0.5 * (1.0 + dot(vn, -vv));
	outcol = col + f * vec4(mix(hor, zen, skyfac), 0);
}

void shade_maddf(vec4 col, float f, vec4 col1, out vec4 outcol)
{
	outcol = col + f * col1;
}

void shade_mul(vec4 col1, vec4 col2, out vec4 outcol)
{
	outcol = col1 * col2;
}

void shade_mul_value(float fac, vec4 col, out vec4 outcol)
{
	outcol = col * fac;
}

void shade_mul_value_v3(float fac, vec3 col, out vec3 outcol)
{
	outcol = col * fac;
}

void shade_obcolor(vec4 col, vec4 obcol, out vec4 outcol)
{
	outcol = vec4(col.rgb * obcol.rgb, col.a);
}

void ramp_rgbtobw(vec3 color, out float outval)
{
	outval = color.r * 0.3 + color.g * 0.58 + color.b * 0.12;
}

void shade_only_shadow(float i, float shadfac, float energy, vec3 shadcol, out vec3 outshadrgb)
{
	outshadrgb = i * energy * (1.0 - shadfac) * (vec3(1.0) - shadcol);
}

void shade_only_shadow_diffuse(vec3 shadrgb, vec3 rgb, vec4 diff, out vec4 outdiff)
{
	outdiff = diff - vec4(rgb * shadrgb, 0.0);
}

void shade_only_shadow_specular(vec3 shadrgb, vec3 specrgb, vec4 spec, out vec4 outspec)
{
	outspec = spec - vec4(specrgb * shadrgb, 0.0);
}

void shade_clamp_positive(vec4 col, out vec4 outcol)
{
	outcol = max(col, vec4(0.0));
}

void test_shadowbuf(
        vec3 rco, sampler2DShadow shadowmap, mat4 shadowpersmat, float shadowbias, float inp,
        out float result)
{
	if (inp <= 0.0) {
		result = 0.0;
	}
	else {
		vec4 co = shadowpersmat * vec4(rco, 1.0);

		//float bias = (1.5 - inp*inp)*shadowbias;
		co.z -= shadowbias * co.w;

		if (co.w > 0.0 && co.x > 0.0 && co.x / co.w < 1.0 && co.y > 0.0 && co.y / co.w < 1.0)
			result = textureProj(shadowmap, co);
		else
			result = 1.0;
	}
}

void test_shadowbuf_vsm(
        vec3 rco, sampler2D shadowmap, mat4 shadowpersmat, float shadowbias, float bleedbias, float inp,
        out float result)
{
	if (inp <= 0.0) {
		result = 0.0;
	}
	else {
		vec4 co = shadowpersmat * vec4(rco, 1.0);
		if (co.w > 0.0 && co.x > 0.0 && co.x / co.w < 1.0 && co.y > 0.0 && co.y / co.w < 1.0) {
			vec2 moments = textureProj(shadowmap, co).rg;
			float dist = co.z / co.w;
			float p = 0.0;

			if (dist <= moments.x)
				p = 1.0;

			float variance = moments.y - (moments.x * moments.x);
			variance = max(variance, shadowbias / 10.0);

			float d = moments.x - dist;
			float p_max = variance / (variance + d * d);

			// Now reduce light-bleeding by removing the [0, x] tail and linearly rescaling (x, 1]
			p_max = clamp((p_max - bleedbias) / (1.0 - bleedbias), 0.0, 1.0);

			result = max(p, p_max);
		}
		else {
			result = 1.0;
		}
	}
}

void shadows_only(
        vec3 rco, sampler2DShadow shadowmap, mat4 shadowpersmat,
        float shadowbias, vec3 shadowcolor, float inp,
        out vec3 result)
{
	result = vec3(1.0);

	if (inp > 0.0) {
		float shadfac;

		test_shadowbuf(rco, shadowmap, shadowpersmat, shadowbias, inp, shadfac);
		result -= (1.0 - shadfac) * (vec3(1.0) - shadowcolor);
	}
}

void shadows_only_vsm(
        vec3 rco, sampler2D shadowmap, mat4 shadowpersmat,
        float shadowbias, float bleedbias, vec3 shadowcolor, float inp,
        out vec3 result)
{
	result = vec3(1.0);

	if (inp > 0.0) {
		float shadfac;

		test_shadowbuf_vsm(rco, shadowmap, shadowpersmat, shadowbias, bleedbias, inp, shadfac);
		result -= (1.0 - shadfac) * (vec3(1.0) - shadowcolor);
	}
}

/* ---- BGE-Remix P8.10: shadows of every lamp type ---- */

/* a rotation per pixel, so a few samples blend into a smooth edge */
float shadow_noise()
{
	return 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
}

/* sample i of n, spread evenly over the unit disk (golden angle spiral) */
vec2 shadow_disk(float i, float n, float rot)
{
	float r = sqrt((i + 0.5) / n);
	float a = i * 2.3999632 + rot;
	return r * vec2(cos(a), sin(a));
}

/* a shadow map's depth (0..1) in world units; clip: near, far, orthographic */
float shadow_linear(float d, vec3 clip)
{
	if (clip.z > 0.5)
		return clip.x + d * (clip.y - clip.x);
	return clip.x * clip.y / max(clip.y - d * (clip.y - clip.x), 1e-8);
}

bool shadow_inside(vec3 co)
{
	return all(greaterThan(co, vec3(0.0))) && all(lessThan(co, vec3(1.0)));
}

vec4 shadow_linear4(vec4 d, vec3 clip)
{
	if (clip.z > 0.5)
		return clip.x + d * (clip.y - clip.x);
	return clip.x * clip.y / max(clip.y - d * (clip.y - clip.x), vec4(1e-8));
}

/* A soft shadow from one view of the shadow pool (percentage-closer soft
 * shadows; BGE-Remix P8 Part B (P8.16): one layer of bge_shadowpool, read
 * four texels at a time): the blockers near the receiver give the penumbra's
 * width, from the light's size and how far the receiver is behind them; the
 * map is then filtered over that width.
 *   layer  the view's layer in the pool
 *   uv     the receiver in the view (0..1), zr its depth (world units)
 *   bias   world units; slope: the receiver's depth change per unit sideways
 *   uvw    the view's uv per world unit at the receiver
 *   light  perspective views: the light's width; orthographic: its spread
 *          (width per unit of distance)
 *   texel  one texel in the view's uv */
float shadow_soft(
        float layer, vec2 uv, float zr, float bias, float slope, vec3 clip,
        float uvw, float light, float texel)
{
	const float N = 8.0;
	const float MAXR = 24.0;
	bool persp = clip.z < 0.5;
	float rot = shadow_noise();

	/* where blockers of the light's area can be (four texels a sample) */
	float reach = persp ? light * 0.5 * (zr - clip.x) / max(clip.x, 1e-4) : light * 0.5 * (zr - clip.x);
	float search = clamp(reach * uvw, 2.0 * texel, MAXR * texel);
	float blockers = 0.0, nblock = 0.0;
	for (float i = 0.0; i < N; i += 1.0) {
		vec2 o = shadow_disk(i, N, rot) * search;
		vec4 d = BGE_GATHER(vec3(uv + o, layer));
		vec4 zb = shadow_linear4(d, clip);
		/* nothing drawn there (beyond the lamp's Clip End): no blocker */
		vec4 hit = vec4(lessThan(d, vec4(0.99999))) *
		           vec4(lessThan(zb, vec4(zr - bias - slope * length(o) / uvw)));
		blockers += dot(hit, zb);
		nblock += dot(hit, vec4(1.0));
	}
	if (nblock < 0.5)
		return 1.0;
	float zb = blockers / nblock;

	/* the penumbra on the receiver, filtered with each sample's four texels
	 * weighted as a bilinear filter weighs them */
	float pen = persp ? light * (zr - zb) / max(zb, 1e-4) : light * (zr - zb);
	float radius = clamp(0.5 * pen * uvw, texel, MAXR * texel);
	float lit = 0.0;
	for (float i = 0.0; i < N; i += 1.0) {
		vec2 o = shadow_disk(i, N, rot + 1.7) * radius;
		vec2 p = uv + o;
		vec4 d = BGE_GATHER(vec3(p, layer));
		vec4 ok = max(vec4(greaterThanEqual(d, vec4(0.99999))),
		              vec4(greaterThanEqual(shadow_linear4(d, clip), vec4(zr - bias - slope * length(o) / uvw))));
		vec2 f = fract(p / texel - 0.5);
		vec4 w = vec4((1.0 - f.x) * f.y, f.x * f.y, f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y));
		lit += dot(ok, w);
	}
	return lit / N;
}

/* the receiver's offset along its normal, and its slope, against acne (the
 * shaders' normals point into the surface, Blender Internal's convention, so
 * the offset goes against them) */
void shadow_receiver(float inp, float bias, out float ndl, out float sinl, out float offset)
{
	ndl = clamp(inp, 0.05, 1.0);
	sinl = sqrt(1.0 - ndl * ndl);
	offset = (0.5 + 1.5 * sinl) * bias;
}

/* spot lamps. params: the light's width, bias, 0, one texel; clip: near, far,
 * the tangent of half the view, 0 */
void shadow_spot_soft(
        vec3 rco, vec3 vn, float inp, float layer, mat4 persmat, vec4 params, vec4 clip,
        out float result)
{
	if (inp <= 0.0) {
		result = 0.0;
		return;
	}
	float ndl, sinl, offset;
	shadow_receiver(inp, params.y, ndl, sinl, offset);
	vec4 co = persmat * vec4(rco, 1.0);
	float texw = 2.0 * max(co.w, 1e-4) * clip.z * params.w;  /* one texel in world units here */
	co = persmat * vec4(rco - vn * texw * offset, 1.0);
	if (co.w <= 0.0) {
		result = 1.0;
		return;
	}
	vec2 uv = co.xy / co.w;
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
		result = 1.0;
		return;
	}
	result = shadow_soft(layer, uv, co.w, texw * params.y, sinl / ndl * params.y,
	                     vec3(clip.xy, 0.0), 0.5 / (co.w * clip.z), params.x, params.w);
}

/* point and area lamps: a cube's six faces, a layer each.
 * lampmat: the camera's view to the lamp's own axes; clip: near, far, the
 * tangent of half a face's view (a little over 1: the faces overlap), 0 */
void shadow_cube_soft(
        vec3 rco, vec3 vn, float inp, float layer, mat4 lampmat, vec4 params, vec4 clip,
        out float result)
{
	if (inp <= 0.0) {
		result = 0.0;
		return;
	}
	float ndl, sinl, offset;
	shadow_receiver(inp, params.y, ndl, sinl, offset);
	vec3 l = (lampmat * vec4(rco, 1.0)).xyz;
	vec3 al = abs(l);
	float texw = 2.0 * max(max(max(al.x, al.y), al.z), 1e-4) * clip.z * params.w;
	l = (lampmat * vec4(rco - vn * texw * offset, 1.0)).xyz;
	al = abs(l);

	/* the face looks along the largest axis, its up is the next axis */
	vec3 f, u;
	float face;
	if (al.x >= al.y && al.x >= al.z) {
		face = (l.x >= 0.0) ? 0.0 : 1.0;
		f = vec3((l.x >= 0.0) ? 1.0 : -1.0, 0.0, 0.0);
		u = vec3(0.0, 1.0, 0.0);
	}
	else if (al.y >= al.z) {
		face = (l.y >= 0.0) ? 2.0 : 3.0;
		f = vec3(0.0, (l.y >= 0.0) ? 1.0 : -1.0, 0.0);
		u = vec3(0.0, 0.0, 1.0);
	}
	else {
		face = (l.z >= 0.0) ? 4.0 : 5.0;
		f = vec3(0.0, 0.0, (l.z >= 0.0) ? 1.0 : -1.0);
		u = vec3(1.0, 0.0, 0.0);
	}
	float zr = dot(l, f);
	if (zr <= clip.x) {
		result = 1.0;
		return;
	}
	vec2 uv = vec2(dot(l, cross(f, u)), dot(l, u)) / (zr * clip.z) * 0.5 + 0.5;
	result = shadow_soft(layer + face, uv, zr, texw * params.y, sinl / ndl * params.y,
	                     vec3(clip.xy, 0.0), 0.5 / (zr * clip.z), params.x, params.w);
}

/* the sun: four cascades (a layer each) that follow the camera, the finest that
 * holds the point is used. c0..c3: the camera's view to each cascade (uv and
 * depth, 0..1); scale: each one's uv per world unit; range: each one's depth
 * in world units; params: the sun's spread, bias, 0, one texel */
void shadow_sun_soft(
        vec3 rco, vec3 vn, float inp, float layer, mat4 c0, mat4 c1, mat4 c2, mat4 c3,
        vec4 scale, vec4 range, vec4 params, out float result)
{
	if (inp <= 0.0) {
		result = 0.0;
		return;
	}
	vec4 p = vec4(rco, 1.0);
	mat4 m = c0;
	float k = 0.0;
	if (!shadow_inside((c0 * p).xyz)) {
		m = c1;
		k = 1.0;
		if (!shadow_inside((c1 * p).xyz)) {
			m = c2;
			k = 2.0;
			if (!shadow_inside((c2 * p).xyz)) {
				m = c3;
				k = 3.0;
				if (!shadow_inside((c3 * p).xyz)) {
					result = 1.0;
					return;
				}
			}
		}
	}
	float sc = (k < 0.5) ? scale.x : (k < 1.5) ? scale.y : (k < 2.5) ? scale.z : scale.w;
	float rg = (k < 0.5) ? range.x : (k < 1.5) ? range.y : (k < 2.5) ? range.z : range.w;
	float ndl, sinl, offset;
	shadow_receiver(inp, params.y, ndl, sinl, offset);
	float texw = params.w / sc;
	vec3 co = (m * vec4(rco - vn * texw * offset, 1.0)).xyz;
	result = shadow_soft(layer + k, clamp(co.xy, 0.0, 1.0), co.z * rg, texw * params.y, sinl / ndl * params.y,
	                     vec3(0.0, rg, 1.0), sc, params.x, params.w);
}

/* the camera's view depth of a depth buffer value */
float shadow_view_z(float d)
{
	float ndc = d * 2.0 - 1.0;
	if (bge_proj[2][3] == 0.0)
		return (ndc - bge_proj[3][2]) / bge_proj[2][2];
	return -bge_proj[3][2] / (ndc + bge_proj[2][2]);
}

/* contact shadows: a few steps toward the light through the scene's depth.
 * contact: distance (0: off), thickness, 0, bias; the scene's depth is
 * bge_scenedepth (bge_scenedepth_on: drawn this frame).
 * lv points from the light to the surface (Blender Internal's convention) */
void shadow_contact(vec3 rco, vec3 lv, vec4 contact, float shadfac, out float result)
{
	result = shadfac;
	if (contact.x <= 0.0 || bge_scenedepth_on < 0.5 || shadfac <= 0.0)
		return;
	const float N = 8.0;
	float jitter = fract(shadow_noise() * 0.1591549);
	for (float i = 0.0; i < N; i += 1.0) {
		vec3 p = rco - lv * (contact.x * (i + jitter) / N);
		vec4 c = bge_proj * vec4(p, 1.0);
		if (c.w <= 0.0)
			break;
		vec2 ndc = c.xy / c.w;
		if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0)
			break;
		float dz = shadow_view_z(texture(bge_scenedepth, ndc * 0.5 + 0.5).r) - p.z;
		if (dz > contact.w && dz < contact.y) {
			result = 0.0;
			return;
		}
	}
}

/* a shadow factor as the lamp's shadow colour (Lamp Data node) */
void shadow_color_mix(float inp, float shadfac, vec3 shadowcolor, out vec3 result)
{
	result = (inp > 0.0) ? vec3(1.0) - (1.0 - shadfac) * (vec3(1.0) - shadowcolor) : vec3(1.0);
}

void shade_light_texture(vec3 rco, sampler2D cookie, mat4 shadowpersmat, out vec4 result)
{

	vec4 co = shadowpersmat * vec4(rco, 1.0);

	result = textureProj(cookie, co);
}

/* BGE-Remix P8.9: a lamp texture's coordinates: View across the spot's cone
 * (-1..1, as Blender Internal), Global the world */
void lamp_texco_view(vec3 rco, mat4 shadowpersmat, out vec3 co)
{
	vec4 p = shadowpersmat * vec4(rco, 1.0);
	co = vec3(p.xy / max(abs(p.w), 1e-6) * 2.0 - vec2(1.0), 0.0);
}

void lamp_texco_global(vec3 rco, mat4 viewinvmat, out vec3 co)
{
	co = (viewinvmat * vec4(rco, 1.0)).xyz;
}

void shade_exposure_correct(vec3 col, float linfac, float logfac, out vec3 outcol)
{
	outcol = linfac * (1.0 - exp(col * logfac));
}

void shade_mist_factor(
        vec3 co, float enable, float miststa, float mistdist, float misttype, float misi, float misthi,
        mat4 viewinv, out float outfac)
{
	if (enable == 1.0) {
		float fac, zcor;

		zcor = (bge_proj[3][3] == 0.0) ? length(co) : -co[2];

		fac = clamp((zcor - miststa) / mistdist, 0.0, 1.0);
		if (misttype == 0.0) fac *= fac;
		else if (misttype == 1.0) ;
		else fac = sqrt(fac);

		/* BGE-Remix P8 Part B (P8.17): the mist's height, as F12 (none at
		 * and above it, fading in below) */
		if (misthi != 0.0 && fac != 0.0) {
			float hi = (viewinv * vec4(co, 1.0)).z;
			if (hi > misthi)
				fac = 0.0;
			else if (hi > 0.0) {
				hi = (misthi - hi) / misthi;
				fac *= hi * hi;
			}
		}

		outfac = 1.0 - (1.0 - fac) * (1.0 - misi);
	}
	else {
		outfac = 0.0;
	}
}

void shade_world_mix(vec3 hor, vec4 col, out vec4 outcol)
{
	float fac = clamp(col.a, 0.0, 1.0);
	outcol = vec4(mix(hor, col.rgb, fac), col.a);
}

void shade_alpha_opaque(vec4 col, out vec4 outcol)
{
	outcol = vec4(col.rgb, 1.0);
}

void shade_alpha_obcolor(vec4 col, vec4 obcol, out vec4 outcol)
{
	outcol = vec4(col.rgb, col.a * obcol.a);
}

/*********** NEW SHADER UTILITIES **************/

float fresnel_dielectric_0(float eta)
{
	/* compute fresnel reflactance at normal incidence => cosi = 1.0 */
	float A = (eta - 1.0) / (eta + 1.0);

	return A * A;
}

float fresnel_dielectric_cos(float cosi, float eta)
{
	/* compute fresnel reflectance without explicitly computing
	 * the refracted direction */
	float c = abs(cosi);
	float g = eta * eta - 1.0 + c * c;
	float result;

	if (g > 0.0) {
		g = sqrt(g);
		float A = (g - c) / (g + c);
		float B = (c * (g + c) - 1.0) / (c * (g - c) + 1.0);
		result = 0.5 * A * A * (1.0 + B * B);
	}
	else {
		result = 1.0;  /* TIR (no refracted component) */
	}

	return result;
}

float fresnel_dielectric(vec3 Incoming, vec3 Normal, float eta)
{
	/* compute fresnel reflectance without explicitly computing
	 * the refracted direction */
	return fresnel_dielectric_cos(dot(Incoming, Normal), eta);
}

float hypot(float x, float y)
{
	return sqrt(x * x + y * y);
}

void generated_from_orco(vec3 orco, out vec3 generated)
{
	generated = orco * 0.5 + vec3(0.5);
}

int floor_to_int(float x)
{
	return int(floor(x));
}

int quick_floor(float x)
{
	return int(x) - ((x < 0) ? 1 : 0);
}

#ifdef BIT_OPERATIONS
float integer_noise(int n)
{
	int nn;
	n = (n + 1013) & 0x7fffffff;
	n = (n >> 13) ^ n;
	nn = (n * (n * n * 60493 + 19990303) + 1376312589) & 0x7fffffff;
	return 0.5 * (float(nn) / 1073741824.0);
}

uint hash(uint kx, uint ky, uint kz)
{
#define rot(x, k) (((x) << (k)) | ((x) >> (32 - (k))))
#define final(a, b, c) \
{ \
	c ^= b; c -= rot(b, 14); \
	a ^= c; a -= rot(c, 11); \
	b ^= a; b -= rot(a, 25); \
	c ^= b; c -= rot(b, 16); \
	a ^= c; a -= rot(c, 4);  \
	b ^= a; b -= rot(a, 14); \
	c ^= b; c -= rot(b, 24); \
}
	// now hash the data!
	uint a, b, c, len = 3u;
	a = b = c = 0xdeadbeefu + (len << 2u) + 13u;

	c += kz;
	b += ky;
	a += kx;
	final (a, b, c);

	return c;
#undef rot
#undef final
}

uint hash(int kx, int ky, int kz)
{
	return hash(uint(kx), uint(ky), uint(kz));
}

float bits_to_01(uint bits)
{
	float x = float(bits) * (1.0 / float(0xffffffffu));
	return x;
}

float cellnoise(vec3 p)
{
	int ix = quick_floor(p.x);
	int iy = quick_floor(p.y);
	int iz = quick_floor(p.z);

	return bits_to_01(hash(uint(ix), uint(iy), uint(iz)));
}

vec3 cellnoise_color(vec3 p)
{
	float r = cellnoise(p);
	float g = cellnoise(vec3(p.y, p.x, p.z));
	float b = cellnoise(vec3(p.y, p.z, p.x));

	return vec3(r, g, b);
}
#endif  // BIT_OPERATIONS

float floorfrac(float x, out int i)
{
	i = floor_to_int(x);
	return x - i;
}


/* Principled BSDF operations */

float sqr(float a)
{
	return a*a;
}

float schlick_fresnel(float u)
{
	float m = clamp(1.0 - u, 0.0, 1.0);
	float m2 = m * m;
	return m2 * m2 * m; // pow(m,5)
}

float GTR1(float NdotH, float a)
{
	if (a >= 1.0) {
		return M_1_PI;
	}

	a = max(a, 0.001);
	float a2 = a*a;
	float t = 1.0 + (a2 - 1.0) * NdotH*NdotH;
	return (a2 - 1.0) / (M_PI * log(a2) * t);
}

float GTR2(float NdotH, float a)
{
	float a2 = a*a;
	float t = 1.0 + (a2 - 1.0) * NdotH*NdotH;
	return a2 / (M_PI * t*t);
}

float GTR2_aniso(float NdotH, float HdotX, float HdotY, float ax, float ay)
{
	return 1.0 / (M_PI * ax*ay * sqr(sqr(HdotX / ax) + sqr(HdotY / ay) + NdotH*NdotH));
}

float smithG_GGX(float NdotV, float alphaG)
{
	float a = alphaG*alphaG;
	float b = NdotV*NdotV;
	return 1.0 / (NdotV + sqrt(a + b - a * b));
}

vec3 rotate_vector(vec3 p, vec3 n, float theta) {
	return (
	           p * cos(theta) + cross(n, p) *
	           sin(theta) + n * dot(p, n) *
	           (1.0 - cos(theta))
	       );
}


/*********** NEW SHADER NODES ***************/

#define NUM_LIGHTS 3

/* bsdfs */

void node_bsdf_diffuse(vec4 color, float roughness, vec3 N, out vec4 result)
{
	/* ambient light */
	vec3 L = vec3(0.2);

	/* directional lights */
	for (int i = 0; i < NUM_LIGHTS; i++) {
		vec3 light_position = bge_lightsource[i].position.xyz;
		vec3 light_diffuse = bge_lightsource[i].diffuse.rgb;

		float bsdf = max(dot(N, light_position), 0.0);
		L += light_diffuse * bsdf;
	}

	result = vec4(L * color.rgb, 1.0);
}

void node_bsdf_glossy(vec4 color, float roughness, vec3 N, out vec4 result)
{
	/* ambient light */
	vec3 L = vec3(0.2);

	/* directional lights */
	for (int i = 0; i < NUM_LIGHTS; i++) {
		vec3 light_position = bge_lightsource[i].position.xyz;
		vec3 H = bge_lightsource[i].halfVector.xyz;
		vec3 light_diffuse = bge_lightsource[i].diffuse.rgb;
		vec3 light_specular = bge_lightsource[i].specular.rgb;

		/* we mix in some diffuse so low roughness still shows up */
		float bsdf = 0.5 * pow(max(dot(N, H), 0.0), 1.0 / roughness);
		bsdf += 0.5 * max(dot(N, light_position), 0.0);
		L += light_specular * bsdf;
	}

	result = vec4(L * color.rgb, 1.0);
}

void node_bsdf_anisotropic(
        vec4 color, float roughness, float anisotropy, float rotation, vec3 N, vec3 T,
        out vec4 result)
{
	node_bsdf_diffuse(color, 0.0, N, result);
}

void node_bsdf_glass(vec4 color, float roughness, float ior, vec3 N, out vec4 result)
{
	node_bsdf_diffuse(color, 0.0, N, result);
}

void node_bsdf_toon(vec4 color, float size, float tsmooth, vec3 N, out vec4 result)
{
	node_bsdf_diffuse(color, 0.0, N, result);
}

void node_bsdf_principled(vec4 base_color, float subsurface, vec3 subsurface_radius, vec4 subsurface_color, float metallic, float specular,
	float specular_tint, float roughness, float anisotropic, float anisotropic_rotation, float sheen, float sheen_tint, float clearcoat,
	float clearcoat_roughness, float ior, float transmission, float transmission_roughness, vec3 N, vec3 CN, vec3 T, vec3 I, out vec4 result)
{
	/* ambient light */
	// TODO: set ambient light to an appropriate value
	vec3 L = mix(0.1, 0.03, metallic) * mix(base_color.rgb, subsurface_color.rgb, subsurface * (1.0 - metallic));

	float eta = (2.0 / (1.0 - sqrt(0.08 * specular))) - 1.0;

	/* set the viewing vector */
	vec3 V = (bge_proj[3][3] == 0.0) ? -normalize(I) : vec3(0.0, 0.0, 1.0);

	/* get the tangent */
	vec3 Tangent = T;
	if (T == vec3(0.0)) {
		// if no tangent is set, use a default tangent
		if(N.x != N.y || N.x != N.z) {
			Tangent = vec3(N.z-N.y, N.x-N.z, N.y-N.x);  // (1,1,1) x N
		}
		else {
			Tangent = vec3(N.z-N.y, N.x+N.z, -N.y-N.x);  // (-1,1,1) x N
		}
	}

	/* rotate tangent */
	if (anisotropic_rotation != 0.0) {
		Tangent = rotate_vector(Tangent, N, anisotropic_rotation * 2.0 * M_PI);
	}

	/* calculate the tangent and bitangent */
	vec3 Y = normalize(cross(N, Tangent));
	vec3 X = cross(Y, N);

	/* fresnel normalization parameters */
	float F0 = fresnel_dielectric_0(eta);
	float F0_norm = 1.0 / (1.0 - F0);

	/* directional lights */
	for (int i = 0; i < NUM_LIGHTS; i++) {
		vec3 light_position_world = bge_lightsource[i].position.xyz;
		vec3 light_position = normalize(light_position_world);

		vec3 H = normalize(light_position + V);

		vec3 light_diffuse = bge_lightsource[i].diffuse.rgb;
		vec3 light_specular = bge_lightsource[i].specular.rgb;

		float NdotL = dot(N, light_position);
		float NdotV = dot(N, V);
		float LdotH = dot(light_position, H);

		vec3 diffuse_and_specular_bsdf = vec3(0.0);
		if (NdotL >= 0.0 && NdotV >= 0.0) {
			float NdotH = dot(N, H);

			float Cdlum = 0.3 * base_color.r + 0.6 * base_color.g + 0.1 * base_color.b; // luminance approx.

			vec3 Ctint = Cdlum > 0 ? base_color.rgb / Cdlum : vec3(1.0); // normalize lum. to isolate hue+sat
			vec3 Cspec0 = mix(specular * 0.08 * mix(vec3(1.0), Ctint, specular_tint), base_color.rgb, metallic);
			vec3 Csheen = mix(vec3(1.0), Ctint, sheen_tint);

			// Diffuse fresnel - go from 1 at normal incidence to .5 at grazing
			// and mix in diffuse retro-reflection based on roughness

			float FL = schlick_fresnel(NdotL), FV = schlick_fresnel(NdotV);
			float Fd90 = 0.5 + 2.0 * LdotH*LdotH * roughness;
			float Fd = mix(1.0, Fd90, FL) * mix(1.0, Fd90, FV);

			// Based on Hanrahan-Krueger brdf approximation of isotropic bssrdf
			// 1.25 scale is used to (roughly) preserve albedo
			// Fss90 used to "flatten" retroreflection based on roughness
			float Fss90 = LdotH*LdotH * roughness;
			float Fss = mix(1.0, Fss90, FL) * mix(1.0, Fss90, FV);
			float ss = 1.25 * (Fss * (1.0 / (NdotL + NdotV) - 0.5) + 0.5);

			// specular
			float aspect = sqrt(1.0 - anisotropic * 0.9);
			float a = sqr(roughness);
			float ax = max(0.001, a / aspect);
			float ay = max(0.001, a * aspect);
			float Ds = GTR2_aniso(NdotH, dot(H, X), dot(H, Y), ax, ay); //GTR2(NdotH, a);
			float FH = (fresnel_dielectric_cos(LdotH, eta) - F0) * F0_norm;
			vec3 Fs = mix(Cspec0, vec3(1.0), FH);
			float roughg = sqr(roughness * 0.5 + 0.5);
			float Gs = smithG_GGX(NdotL, roughg) * smithG_GGX(NdotV, roughg);

			// sheen
			vec3 Fsheen = schlick_fresnel(LdotH) * sheen * Csheen;

			vec3 diffuse_bsdf = (mix(Fd * base_color.rgb, ss * subsurface_color.rgb, subsurface) + Fsheen) * light_diffuse;
			vec3 specular_bsdf = Gs * Fs * Ds * light_specular;
			diffuse_and_specular_bsdf = diffuse_bsdf * (1.0 - metallic) + specular_bsdf;
		}
		diffuse_and_specular_bsdf *= max(NdotL, 0.0);

		float CNdotL = dot(CN, light_position);
		float CNdotV = dot(CN, V);

		vec3 clearcoat_bsdf = vec3(0.0);
		if (CNdotL >= 0.0 && CNdotV >= 0.0 && clearcoat > 0.0) {
			float CNdotH = dot(CN, H);
			//float FH = schlick_fresnel(LdotH);

			// clearcoat (ior = 1.5 -> F0 = 0.04)
			float Dr = GTR1(CNdotH, sqr(clearcoat_roughness));
			float Fr = fresnel_dielectric_cos(LdotH, 1.5); //mix(0.04, 1.0, FH);
			float Gr = smithG_GGX(CNdotL, 0.25) * smithG_GGX(CNdotV, 0.25);

			clearcoat_bsdf = clearcoat * Gr * Fr * Dr * vec3(0.25) * light_specular;
		}
		clearcoat_bsdf *= max(CNdotL, 0.0);

		L += diffuse_and_specular_bsdf + clearcoat_bsdf;
	}

	result = vec4(L, 1.0);
}

void node_bsdf_translucent(vec4 color, vec3 N, out vec4 result)
{
	node_bsdf_diffuse(color, 0.0, N, result);
}

void node_bsdf_transparent(vec4 color, out vec4 result)
{
	/* this isn't right */
	result.r = color.r;
	result.g = color.g;
	result.b = color.b;
	result.a = 0.0;
}

void node_bsdf_velvet(vec4 color, float sigma, vec3 N, out vec4 result)
{
	node_bsdf_diffuse(color, 0.0, N, result);
}

void node_subsurface_scattering(
        vec4 color, float scale, vec3 radius, float sharpen, float texture_blur, vec3 N,
        out vec4 result)
{
	node_bsdf_diffuse(color, 0.0, N, result);
}

void node_bsdf_hair(vec4 color, float offset, float roughnessu, float roughnessv, vec3 tangent, out vec4 result)
{
	result = color;
}

void node_bsdf_refraction(vec4 color, float roughness, float ior, vec3 N, out vec4 result)
{
	node_bsdf_diffuse(color, 0.0, N, result);
}

void node_ambient_occlusion(vec4 color, out vec4 result)
{
	result = color;
}

/* emission */

void node_emission(vec4 color, float strength, vec3 N, out vec4 result)
{
	result = color * strength;
}

/* background */

void background_transform_to_world(vec3 viewvec, out vec3 worldvec)
{
	vec4 v = (bge_proj[3][3] == 0.0) ? vec4(viewvec, 1.0) : vec4(0.0, 0.0, 1.0, 1.0);
	vec4 co_homogenous = (bge_projinv * v);

	vec4 co = vec4(co_homogenous.xyz / co_homogenous.w, 0.0);
	worldvec = (bge_mvinv * co).xyz;
}

void node_background(vec4 color, float strength, vec3 N, out vec4 result)
{
	result = color * strength;
}

/* closures */

void node_mix_shader(float fac, vec4 shader1, vec4 shader2, out vec4 shader)
{
	shader = mix(shader1, shader2, fac);
}

void node_add_shader(vec4 shader1, vec4 shader2, out vec4 shader)
{
	shader = shader1 + shader2;
}

/* fresnel */

void node_fresnel(float ior, vec3 N, vec3 I, out float result)
{
	/* handle perspective/orthographic */
	vec3 I_view = (bge_proj[3][3] == 0.0) ? normalize(I) : vec3(0.0, 0.0, -1.0);

	float eta = max(ior, 0.00001);
	result = fresnel_dielectric(I_view, N, (gl_FrontFacing) ? eta : 1.0 / eta);
}

/* layer_weight */

void node_layer_weight(float blend, vec3 N, vec3 I, out float fresnel, out float facing)
{
	/* fresnel */
	float eta = max(1.0 - blend, 0.00001);
	vec3 I_view = (bge_proj[3][3] == 0.0) ? normalize(I) : vec3(0.0, 0.0, -1.0);

	fresnel = fresnel_dielectric(I_view, N, (gl_FrontFacing) ? 1.0 / eta : eta);

	/* facing */
	facing = abs(dot(I_view, N));
	if (blend != 0.5) {
		blend = clamp(blend, 0.0, 0.99999);
		blend = (blend < 0.5) ? 2.0 * blend : 0.5 / (1.0 - blend);
		facing = pow(facing, blend);
	}
	facing = 1.0 - facing;
}

/* gamma */

void node_gamma(vec4 col, float gamma, out vec4 outcol)
{
	outcol = col;

	if (col.r > 0.0)
		outcol.r = compatible_pow(col.r, gamma);
	if (col.g > 0.0)
		outcol.g = compatible_pow(col.g, gamma);
	if (col.b > 0.0)
		outcol.b = compatible_pow(col.b, gamma);
}

/* geometry */

void node_attribute(vec3 attr, out vec4 outcol, out vec3 outvec, out float outf)
{
	outcol = vec4(attr, 1.0);
	outvec = attr;
	outf = (attr.x + attr.y + attr.z) / 3.0;
}

void node_uvmap(vec3 attr_uv, out vec3 outvec)
{
	outvec = attr_uv;
}

void node_geometry(
        vec3 I, vec3 N, mat4 toworld,
        out vec3 position, out vec3 normal, out vec3 tangent,
        out vec3 true_normal, out vec3 incoming, out vec3 parametric,
        out float backfacing, out float pointiness)
{
	position = (toworld * vec4(I, 1.0)).xyz;
	normal = (toworld * vec4(N, 0.0)).xyz;
	tangent = vec3(0.0);
	true_normal = normal;

	/* handle perspective/orthographic */
	vec3 I_view = (bge_proj[3][3] == 0.0) ? normalize(I) : vec3(0.0, 0.0, -1.0);
	incoming = -(toworld * vec4(I_view, 0.0)).xyz;

	parametric = vec3(0.0);
	backfacing = (gl_FrontFacing) ? 0.0 : 1.0;
	pointiness = 0.5;
}

void node_tex_coord(
        vec3 I, vec3 N, mat4 viewinvmat, mat4 obinvmat, vec4 camerafac,
        vec3 attr_orco, vec3 attr_uv,
        out vec3 generated, out vec3 normal, out vec3 uv, out vec3 object,
        out vec3 camera, out vec3 window, out vec3 reflection)
{
	generated = attr_orco * 0.5 + vec3(0.5);
	normal = normalize((obinvmat * (viewinvmat * vec4(N, 0.0))).xyz);
	uv = attr_uv;
	object = (obinvmat * (viewinvmat * vec4(I, 1.0))).xyz;
	camera = vec3(I.xy, -I.z);
	vec4 projvec = bge_proj * vec4(I, 1.0);
	window = vec3(mtex_2d_mapping(projvec.xyz / projvec.w).xy * camerafac.xy + camerafac.zw, 0.0);

	vec3 shade_I;
	shade_view(I, shade_I);
	vec3 view_reflection = reflect(shade_I, normalize(N));
	reflection = (viewinvmat * vec4(view_reflection, 0.0)).xyz;
}

void node_tex_coord_background(
        vec3 I, vec3 N, mat4 viewinvmat, mat4 obinvmat, vec4 camerafac,
        vec3 attr_orco, vec3 attr_uv,
        out vec3 generated, out vec3 normal, out vec3 uv, out vec3 object,
        out vec3 camera, out vec3 window, out vec3 reflection)
{
	vec4 v = (bge_proj[3][3] == 0.0) ? vec4(I, 1.0) : vec4(0.0, 0.0, 1.0, 1.0);
	vec4 co_homogenous = (bge_projinv * v);

	vec4 co = vec4(co_homogenous.xyz / co_homogenous.w, 0.0);

	co = normalize(co);
	vec3 coords = (bge_mvinv * co).xyz;

	generated = coords;
	normal = -coords;
	uv = vec3(attr_uv.xy, 0.0);
	object = coords;

	camera = vec3(co.xy, -co.z);
	window = (bge_proj[3][3] == 0.0) ?
	         vec3(mtex_2d_mapping(I).xy * camerafac.xy + camerafac.zw, 0.0) :
	         vec3(vec2(0.5) * camerafac.xy + camerafac.zw, 0.0);

	reflection = -coords;
}

/* textures */

float calc_gradient(vec3 p, int gradient_type)
{
	float x, y, z;
	x = p.x;
	y = p.y;
	z = p.z;
	if (gradient_type == 0) {  /* linear */
		return x;
	}
	else if (gradient_type == 1) {  /* quadratic */
		float r = max(x, 0.0);
		return r * r;
	}
	else if (gradient_type == 2) {  /* easing */
		float r = min(max(x, 0.0), 1.0);
		float t = r * r;
		return (3.0 * t - 2.0 * t * r);
	}
	else if (gradient_type == 3) {  /* diagonal */
		return (x + y) * 0.5;
	}
	else if (gradient_type == 4) {  /* radial */
		return atan(y, x) / (M_PI * 2) + 0.5;
	}
	else {
		/* Bias a little bit for the case where p is a unit length vector,
		 * to get exactly zero instead of a small random value depending
		 * on float precision. */
		float r = max(0.999999 - sqrt(x * x + y * y + z * z), 0.0);
		if (gradient_type == 5) {  /* quadratic sphere */
			return r * r;
		}
		else if (gradient_type == 6) {  /* sphere */
			return r;
		}
	}
	return 0.0;
}

void node_tex_gradient(vec3 co, float gradient_type, out vec4 color, out float fac)
{
	float f = calc_gradient(co, int(gradient_type));
	f = clamp(f, 0.0, 1.0);

	color = vec4(f, f, f, 1.0);
	fac = f;
}

void node_tex_checker(vec3 co, vec4 color1, vec4 color2, float scale, out vec4 color, out float fac)
{
	vec3 p = co * scale;

	/* Prevent precision issues on unit coordinates. */
	p.x = (p.x + 0.000001) * 0.999999;
	p.y = (p.y + 0.000001) * 0.999999;
	p.z = (p.z + 0.000001) * 0.999999;

	int xi = int(abs(floor(p.x)));
	int yi = int(abs(floor(p.y)));
	int zi = int(abs(floor(p.z)));

	bool check = ((mod(xi, 2) == mod(yi, 2)) == bool(mod(zi, 2)));

	color = check ? color1 : color2;
	fac = check ? 1.0 : 0.0;
}

#ifdef BIT_OPERATIONS
vec2 calc_brick_texture(vec3 p, float mortar_size, float mortar_smooth, float bias,
                        float brick_width, float row_height,
                        float offset_amount, int offset_frequency,
                        float squash_amount, int squash_frequency)
{
	int bricknum, rownum;
	float offset = 0.0;
	float x, y;

	rownum = floor_to_int(p.y / row_height);

	if (offset_frequency != 0 && squash_frequency != 0) {
		brick_width *= (rownum % squash_frequency != 0) ? 1.0 : squash_amount; /* squash */
		offset = (rownum % offset_frequency != 0) ? 0.0 : (brick_width * offset_amount); /* offset */
	}

	bricknum = floor_to_int((p.x + offset) / brick_width);

	x = (p.x + offset) - brick_width * bricknum;
	y = p.y - row_height * rownum;

	float tint = clamp((integer_noise((rownum << 16) + (bricknum & 0xFFFF)) + bias), 0.0, 1.0);

	float min_dist = min(min(x, y), min(brick_width - x, row_height - y));
	if (min_dist >= mortar_size) {
		return vec2(tint, 0.0);
	}
	else if (mortar_smooth == 0.0) {
		return vec2(tint, 1.0);
	}
	else {
		min_dist = 1.0 - min_dist/mortar_size;
		return vec2(tint, smoothstep(0.0, mortar_smooth, min_dist));
	}
}
#endif

void node_tex_brick(vec3 co,
                    vec4 color1, vec4 color2,
                    vec4 mortar, float scale,
                    float mortar_size, float mortar_smooth, float bias,
                    float brick_width, float row_height,
                    float offset_amount, float offset_frequency,
                    float squash_amount, float squash_frequency,
                    out vec4 color, out float fac)
{
#ifdef BIT_OPERATIONS
	vec2 f2 = calc_brick_texture(co * scale,
	                             mortar_size, mortar_smooth, bias,
	                             brick_width, row_height,
	                             offset_amount, int(offset_frequency),
	                             squash_amount, int(squash_frequency));
	float tint = f2.x;
	float f = f2.y;
	if (f != 1.0) {
		float facm = 1.0 - tint;
		color1 = facm * color1 + tint * color2;
	}
	color = mix(color1, mortar, f);
	fac = f;
#else
	color = vec4(1.0);
	fac = 1.0;
#endif
}

void node_tex_clouds(vec3 co, float size, out vec4 color, out float fac)
{
	color = vec4(1.0);
	fac = 1.0;
}

void node_tex_environment_equirectangular(vec3 co, sampler2D ima, out vec4 color)
{
	vec3 nco = normalize(co);
	float u = -atan(nco.y, nco.x) / (2.0 * M_PI) + 0.5;
	float v = atan(nco.z, hypot(nco.x, nco.y)) / M_PI + 0.5;

	color = texture(ima, vec2(u, v));
}

void node_tex_environment_mirror_ball(vec3 co, sampler2D ima, out vec4 color)
{
	vec3 nco = normalize(co);

	nco.y -= 1.0;

	float div = 2.0 * sqrt(max(-0.5 * nco.y, 0.0));
	if (div > 0.0)
		nco /= div;

	float u = 0.5 * (nco.x + 1.0);
	float v = 0.5 * (nco.z + 1.0);

	color = texture(ima, vec2(u, v));
}

void node_tex_environment_empty(vec3 co, out vec4 color)
{
	color = vec4(1.0, 0.0, 1.0, 1.0);
}

void node_tex_image(vec3 co, sampler2D ima, out vec4 color, out float alpha)
{
	color = texture(ima, co.xy);
	alpha = color.a;
}

void node_tex_image_box(vec3 texco,
                        vec3 nob,
                        sampler2D ima,
                        float blend,
                        out vec4 color,
                        out float alpha)
{
	/* project from direction vector to barycentric coordinates in triangles */
	nob = vec3(abs(nob.x), abs(nob.y), abs(nob.z));
	nob /= (nob.x + nob.y + nob.z);

	/* basic idea is to think of this as a triangle, each corner representing
	 * one of the 3 faces of the cube. in the corners we have single textures,
	 * in between we blend between two textures, and in the middle we a blend
	 * between three textures.
	 *
	 * the Nxyz values are the barycentric coordinates in an equilateral
	 * triangle, which in case of blending, in the middle has a smaller
	 * equilateral triangle where 3 textures blend. this divides things into
	 * 7 zones, with an if () test for each zone */

	vec3 weight = vec3(0.0, 0.0, 0.0);
	float limit = 0.5 * (1.0 + blend);

	/* first test for corners with single texture */
	if (nob.x > limit * (nob.x + nob.y) && nob.x > limit * (nob.x + nob.z)) {
		weight.x = 1.0;
	}
	else if (nob.y > limit * (nob.x + nob.y) && nob.y > limit * (nob.y + nob.z)) {
		weight.y = 1.0;
	}
	else if (nob.z > limit * (nob.x + nob.z) && nob.z > limit * (nob.y + nob.z)) {
		weight.z = 1.0;
	}
	else if (blend > 0.0) {
		/* in case of blending, test for mixes between two textures */
		if (nob.z < (1.0 - limit) * (nob.y + nob.x)) {
			weight.x = nob.x / (nob.x + nob.y);
			weight.x = clamp((weight.x - 0.5 * (1.0 - blend)) / blend, 0.0, 1.0);
			weight.y = 1.0 - weight.x;
		}
		else if (nob.x < (1.0 - limit) * (nob.y + nob.z)) {
			weight.y = nob.y / (nob.y + nob.z);
			weight.y = clamp((weight.y - 0.5 * (1.0 - blend)) / blend, 0.0, 1.0);
			weight.z = 1.0 - weight.y;
		}
		else if (nob.y < (1.0 - limit) * (nob.x + nob.z)) {
			weight.x = nob.x / (nob.x + nob.z);
			weight.x = clamp((weight.x - 0.5 * (1.0 - blend)) / blend, 0.0, 1.0);
			weight.z = 1.0 - weight.x;
		}
		else {
			/* last case, we have a mix between three */
			weight.x = ((2.0 - limit) * nob.x + (limit - 1.0)) / (2.0 * limit - 1.0);
			weight.y = ((2.0 - limit) * nob.y + (limit - 1.0)) / (2.0 * limit - 1.0);
			weight.z = ((2.0 - limit) * nob.z + (limit - 1.0)) / (2.0 * limit - 1.0);
		}
	}
	else {
		/* Desperate mode, no valid choice anyway, fallback to one side.*/
		weight.x = 1.0;
	}
	color = vec4(0);
	if (weight.x > 0.0) {
		color += weight.x * texture(ima, texco.yz);
	}
	if (weight.y > 0.0) {
		color += weight.y * texture(ima, texco.xz);
	}
	if (weight.z > 0.0) {
		color += weight.z * texture(ima, texco.yx);
	}

	alpha = color.a;
}

void node_tex_image_empty(vec3 co, out vec4 color, out float alpha)
{
	color = vec4(0.0);
	alpha = 0.0;
}

void node_tex_magic(vec3 co, float scale, float distortion, float depth, out vec4 color, out float fac)
{
	vec3 p = co * scale;
	float x = sin((p.x + p.y + p.z) * 5.0);
	float y = cos((-p.x + p.y - p.z) * 5.0);
	float z = -cos((-p.x - p.y + p.z) * 5.0);

	if (depth > 0) {
		x *= distortion;
		y *= distortion;
		z *= distortion;
		y = -cos(x - y + z);
		y *= distortion;
		if (depth > 1) {
			x = cos(x - y - z);
			x *= distortion;
			if (depth > 2) {
				z = sin(-x - y - z);
				z *= distortion;
				if (depth > 3) {
					x = -cos(-x + y - z);
					x *= distortion;
					if (depth > 4) {
						y = -sin(-x + y + z);
						y *= distortion;
						if (depth > 5) {
							y = -cos(-x + y + z);
							y *= distortion;
							if (depth > 6) {
								x = cos(x + y + z);
								x *= distortion;
								if (depth > 7) {
									z = sin(x + y - z);
									z *= distortion;
									if (depth > 8) {
										x = -cos(-x - y + z);
										x *= distortion;
										if (depth > 9) {
											y = -sin(x - y + z);
											y *= distortion;
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
	if (distortion != 0.0) {
		distortion *= 2.0;
		x /= distortion;
		y /= distortion;
		z /= distortion;
	}

	color = vec4(0.5 - x, 0.5 - y, 0.5 - z, 1.0);
	fac = (color.x + color.y + color.z) / 3.0;
}

#ifdef BIT_OPERATIONS
float noise_fade(float t)
{
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float noise_scale3(float result)
{
	return 0.9820 * result;
}

float noise_nerp(float t, float a, float b)
{
	return (1.0 - t) * a + t * b;
}

float noise_grad(uint hash, float x, float y, float z)
{
	uint h = hash & 15u;
	float u = h < 8u ? x : y;
	float vt = ((h == 12u) || (h == 14u)) ? x : z;
	float v = h < 4u ? y : vt;
	return (((h & 1u) != 0u) ? -u : u) + (((h & 2u) != 0u) ? -v : v);
}

float noise_perlin(float x, float y, float z)
{
	int X; float fx = floorfrac(x, X);
	int Y; float fy = floorfrac(y, Y);
	int Z; float fz = floorfrac(z, Z);

	float u = noise_fade(fx);
	float v = noise_fade(fy);
	float w = noise_fade(fz);

	float result;

	result = noise_nerp(w, noise_nerp(v, noise_nerp(u, noise_grad(hash(X, Y, Z), fx, fy, fz),
	                                                noise_grad(hash(X + 1, Y, Z), fx - 1.0, fy, fz)),
	                                  noise_nerp(u, noise_grad(hash(X, Y + 1, Z), fx, fy - 1.0, fz),
	                                             noise_grad(hash(X + 1, Y + 1, Z), fx - 1.0, fy - 1.0, fz))),
	                    noise_nerp(v, noise_nerp(u, noise_grad(hash(X, Y, Z + 1), fx, fy, fz - 1.0),
	                                             noise_grad(hash(X + 1, Y, Z + 1), fx - 1.0, fy, fz - 1.0)),
	                               noise_nerp(u, noise_grad(hash(X, Y + 1, Z + 1), fx, fy - 1.0, fz - 1.0),
	                                          noise_grad(hash(X + 1, Y + 1, Z + 1), fx - 1.0, fy - 1.0, fz - 1.0))));
	return noise_scale3(result);
}

float noise(vec3 p)
{
	return 0.5 * noise_perlin(p.x, p.y, p.z) + 0.5;
}

float snoise(vec3 p)
{
	return noise_perlin(p.x, p.y, p.z);
}

float noise_turbulence(vec3 p, float octaves, int hard)
{
	float fscale = 1.0;
	float amp = 1.0;
	float sum = 0.0;
	int i, n;
	octaves = clamp(octaves, 0.0, 16.0);
	n = int(octaves);
	for (i = 0; i <= n; i++) {
		float t = noise(fscale * p);
		if (hard != 0) {
			t = abs(2.0 * t - 1.0);
		}
		sum += t * amp;
		amp *= 0.5;
		fscale *= 2.0;
	}
	float rmd = octaves - floor(octaves);
	if  (rmd != 0.0) {
		float t = noise(fscale * p);
		if (hard != 0) {
			t = abs(2.0 * t - 1.0);
		}
		float sum2 = sum + t * amp;
		sum *= (float(1 << n) / float((1 << (n + 1)) - 1));
		sum2 *= (float(1 << (n + 1)) / float((1 << (n + 2)) - 1));
		return (1.0 - rmd) * sum + rmd * sum2;
	}
	else {
		sum *= (float(1 << n) / float((1 << (n + 1)) - 1));
		return sum;
	}
}
#endif  // BIT_OPERATIONS

void node_tex_noise(vec3 co, float scale, float detail, float distortion, out vec4 color, out float fac)
{
#ifdef BIT_OPERATIONS
	vec3 p = co * scale;
	int hard = 0;
	if (distortion != 0.0) {
		vec3 r, offset = vec3(13.5, 13.5, 13.5);
		r.x = noise(p + offset) * distortion;
		r.y = noise(p) * distortion;
		r.z = noise(p - offset) * distortion;
		p += r;
	}

	fac = noise_turbulence(p, detail, hard);
	color = vec4(fac,
	             noise_turbulence(vec3(p.y, p.x, p.z), detail, hard),
	             noise_turbulence(vec3(p.y, p.z, p.x), detail, hard),
	             1);
#else  // BIT_OPERATIONS
	color = vec4(1.0);
	fac = 1.0;
#endif  // BIT_OPERATIONS
}


#ifdef BIT_OPERATIONS

/* Musgrave fBm
 *
 * H: fractal increment parameter
 * lacunarity: gap between successive frequencies
 * octaves: number of frequencies in the fBm
 *
 * from "Texturing and Modelling: A procedural approach"
 */

float noise_musgrave_fBm(vec3 p, float H, float lacunarity, float octaves)
{
	float rmd;
	float value = 0.0;
	float pwr = 1.0;
	float pwHL = pow(lacunarity, -H);
	int i;

	for (i = 0; i < int(octaves); i++) {
		value += snoise(p) * pwr;
		pwr *= pwHL;
		p *= lacunarity;
	}

	rmd = octaves - floor(octaves);
	if (rmd != 0.0)
		value += rmd * snoise(p) * pwr;

	return value;
}

/* Musgrave Multifractal
 *
 * H: highest fractal dimension
 * lacunarity: gap between successive frequencies
 * octaves: number of frequencies in the fBm
 */

float noise_musgrave_multi_fractal(vec3 p, float H, float lacunarity, float octaves)
{
	float rmd;
	float value = 1.0;
	float pwr = 1.0;
	float pwHL = pow(lacunarity, -H);
	int i;

	for (i = 0; i < int(octaves); i++) {
		value *= (pwr * snoise(p) + 1.0);
		pwr *= pwHL;
		p *= lacunarity;
	}

	rmd = octaves - floor(octaves);
	if (rmd != 0.0)
		value *= (rmd * pwr * snoise(p) + 1.0); /* correct? */

	return value;
}

/* Musgrave Heterogeneous Terrain
 *
 * H: fractal dimension of the roughest area
 * lacunarity: gap between successive frequencies
 * octaves: number of frequencies in the fBm
 * offset: raises the terrain from `sea level'
 */

float noise_musgrave_hetero_terrain(vec3 p, float H, float lacunarity, float octaves, float offset)
{
	float value, increment, rmd;
	float pwHL = pow(lacunarity, -H);
	float pwr = pwHL;
	int i;

	/* first unscaled octave of function; later octaves are scaled */
	value = offset + snoise(p);
	p *= lacunarity;

	for (i = 1; i < int(octaves); i++) {
		increment = (snoise(p) + offset) * pwr * value;
		value += increment;
		pwr *= pwHL;
		p *= lacunarity;
	}

	rmd = octaves - floor(octaves);
	if (rmd != 0.0) {
		increment = (snoise(p) + offset) * pwr * value;
		value += rmd * increment;
	}

	return value;
}

/* Hybrid Additive/Multiplicative Multifractal Terrain
 *
 * H: fractal dimension of the roughest area
 * lacunarity: gap between successive frequencies
 * octaves: number of frequencies in the fBm
 * offset: raises the terrain from `sea level'
 */

float noise_musgrave_hybrid_multi_fractal(vec3 p, float H, float lacunarity, float octaves, float offset, float gain)
{
	float result, signal, weight, rmd;
	float pwHL = pow(lacunarity, -H);
	float pwr = pwHL;
	int i;

	result = snoise(p) + offset;
	weight = gain * result;
	p *= lacunarity;

	for (i = 1; (weight > 0.001f) && (i < int(octaves)); i++) {
		if (weight > 1.0)
			weight = 1.0;

		signal = (snoise(p) + offset) * pwr;
		pwr *= pwHL;
		result += weight * signal;
		weight *= gain * signal;
		p *= lacunarity;
	}

	rmd = octaves - floor(octaves);
	if (rmd != 0.0)
		result += rmd * ((snoise(p) + offset) * pwr);

	return result;
}

/* Ridged Multifractal Terrain
 *
 * H: fractal dimension of the roughest area
 * lacunarity: gap between successive frequencies
 * octaves: number of frequencies in the fBm
 * offset: raises the terrain from `sea level'
 */

float noise_musgrave_ridged_multi_fractal(vec3 p, float H, float lacunarity, float octaves, float offset, float gain)
{
	float result, signal, weight;
	float pwHL = pow(lacunarity, -H);
	float pwr = pwHL;
	int i;

	signal = offset - abs(snoise(p));
	signal *= signal;
	result = signal;
	weight = 1.0;

	for (i = 1; i < int(octaves); i++) {
		p *= lacunarity;
		weight = clamp(signal * gain, 0.0, 1.0);
		signal = offset - abs(snoise(p));
		signal *= signal;
		signal *= weight;
		result += signal * pwr;
		pwr *= pwHL;
	}

	return result;
}

float svm_musgrave(int type,
                   float dimension,
                   float lacunarity,
                   float octaves,
                   float offset,
                   float intensity,
                   float gain,
                   vec3 p)
{
	if (type == 0 /*NODE_MUSGRAVE_MULTIFRACTAL*/)
		return intensity * noise_musgrave_multi_fractal(p, dimension, lacunarity, octaves);
	else if (type == 1 /*NODE_MUSGRAVE_FBM*/)
		return intensity * noise_musgrave_fBm(p, dimension, lacunarity, octaves);
	else if (type == 2 /*NODE_MUSGRAVE_HYBRID_MULTIFRACTAL*/)
		return intensity * noise_musgrave_hybrid_multi_fractal(p, dimension, lacunarity, octaves, offset, gain);
	else if (type == 3 /*NODE_MUSGRAVE_RIDGED_MULTIFRACTAL*/)
		return intensity * noise_musgrave_ridged_multi_fractal(p, dimension, lacunarity, octaves, offset, gain);
	else if (type == 4 /*NODE_MUSGRAVE_HETERO_TERRAIN*/)
		return intensity * noise_musgrave_hetero_terrain(p, dimension, lacunarity, octaves, offset);
	return 0.0;
}
#endif  // #ifdef BIT_OPERATIONS

void node_tex_musgrave(vec3 co,
                       float scale,
                       float detail,
                       float dimension,
                       float lacunarity,
                       float offset,
                       float gain,
                       float type,
                       out vec4 color,
                       out float fac)
{
#ifdef BIT_OPERATIONS
	fac = svm_musgrave(int(type),
	                   dimension,
	                   lacunarity,
	                   detail,
	                   offset,
	                   1.0,
	                   gain,
	                   co * scale);
#else
	fac = 1.0;
#endif

	color = vec4(fac, fac, fac, 1.0);
}

void node_tex_sky(vec3 co, out vec4 color)
{
	color = vec4(1.0);
}

void node_tex_voronoi(vec3 co, float scale, float coloring, out vec4 color, out float fac)
{
#ifdef BIT_OPERATIONS
	vec3 p = co * scale;
	int xx, yy, zz, xi, yi, zi;
	float da[4];
	vec3 pa[4];

	xi = floor_to_int(p[0]);
	yi = floor_to_int(p[1]);
	zi = floor_to_int(p[2]);

	da[0] = 1e+10;
	da[1] = 1e+10;
	da[2] = 1e+10;
	da[3] = 1e+10;

	for (xx = xi - 1; xx <= xi + 1; xx++) {
		for (yy = yi - 1; yy <= yi + 1; yy++) {
			for (zz = zi - 1; zz <= zi + 1; zz++) {
				vec3 ip = vec3(xx, yy, zz);
				vec3 vp = cellnoise_color(ip);
				vec3 pd = p - (vp + ip);
				float d = dot(pd, pd);
				vp += vec3(xx, yy, zz);
				if (d < da[0]) {
					da[3] = da[2];
					da[2] = da[1];
					da[1] = da[0];
					da[0] = d;
					pa[3] = pa[2];
					pa[2] = pa[1];
					pa[1] = pa[0];
					pa[0] = vp;
				}
				else if (d < da[1]) {
					da[3] = da[2];
					da[2] = da[1];
					da[1] = d;

					pa[3] = pa[2];
					pa[2] = pa[1];
					pa[1] = vp;
				}
				else if (d < da[2]) {
					da[3] = da[2];
					da[2] = d;

					pa[3] = pa[2];
					pa[2] = vp;
				}
				else if (d < da[3]) {
					da[3] = d;
					pa[3] = vp;
				}
			}
		}
	}

	if (coloring == 0.0) {
		fac = abs(da[0]);
		color = vec4(fac, fac, fac, 1);
	}
	else {
		color = vec4(cellnoise_color(pa[0]), 1);
		fac = (color.x + color.y + color.z) * (1.0 / 3.0);
	}
#else  // BIT_OPERATIONS
	color = vec4(1.0);
	fac = 1.0;
#endif  // BIT_OPERATIONS
}

#ifdef BIT_OPERATIONS
float calc_wave(vec3 p, float distortion, float detail, float detail_scale, int wave_type, int wave_profile)
{
	float n;

	if (wave_type == 0) /* type bands */
		n = (p.x + p.y + p.z) * 10.0;
	else /* type rings */
		n = length(p) * 20.0;

	if (distortion != 0.0)
		n += distortion * noise_turbulence(p * detail_scale, detail, 0);

	if (wave_profile == 0) { /* profile sin */
		return 0.5 + 0.5 * sin(n);
	}
	else { /* profile saw */
		n /= 2.0 * M_PI;
		n -= int(n);
		return (n < 0.0) ? n + 1.0 : n;
	}
}
#endif  // BIT_OPERATIONS

void node_tex_wave(
        vec3 co, float scale, float distortion, float detail, float detail_scale, float wave_type, float wave_profile,
        out vec4 color, out float fac)
{
#ifdef BIT_OPERATIONS
	float f;
	f = calc_wave(co * scale, distortion, detail, detail_scale, int(wave_type), int(wave_profile));

	color = vec4(f, f, f, 1.0);
	fac = f;
#else  // BIT_OPERATIONS
	color = vec4(1.0);
	fac = 1;
#endif  // BIT_OPERATIONS
}

/* light path */

void node_light_path(
	out float is_camera_ray,
	out float is_shadow_ray,
	out float is_diffuse_ray,
	out float is_glossy_ray,
	out float is_singular_ray,
	out float is_reflection_ray,
	out float is_transmission_ray,
	out float ray_length,
	out float ray_depth,
	out float diffuse_depth,
	out float glossy_depth,
	out float transparent_depth,
	out float transmission_depth)
{
	is_camera_ray = 1.0;
	is_shadow_ray = 0.0;
	is_diffuse_ray = 0.0;
	is_glossy_ray = 0.0;
	is_singular_ray = 0.0;
	is_reflection_ray = 0.0;
	is_transmission_ray = 0.0;
	ray_length = 1.0;
	ray_depth = 1.0;
	diffuse_depth = 1.0;
	glossy_depth = 1.0;
	transparent_depth = 1.0;
	transmission_depth = 1.0;
}

void node_light_falloff(float strength, float tsmooth, out float quadratic, out float linear, out float constant)
{
	quadratic = strength;
	linear = strength;
	constant = strength;
}

void node_object_info(mat4 obmat, vec3 info, out vec3 location, out float object_index, out float material_index, out float random)
{
	location = obmat[3].xyz;
	object_index = info.x;
	material_index = info.y;
	random = info.z;
}

void node_normal_map(vec4 tangent, vec3 normal, vec3 texnormal, out vec3 outnormal)
{
	vec3 B = tangent.w * cross(normal, tangent.xyz);

	outnormal = texnormal.x * tangent.xyz + texnormal.y * B + texnormal.z * normal;
	outnormal = normalize(outnormal);
}

void node_bump(float strength, float dist, float height, vec3 N, vec3 surf_pos, float invert, out vec3 result)
{
	if (invert != 0.0) {
		dist *= -1.0;
	}
	vec3 dPdx = dFdx(surf_pos);
	vec3 dPdy = dFdy(surf_pos);

	/* Get surface tangents from normal. */
	vec3 Rx = cross(dPdy, N);
	vec3 Ry = cross(N, dPdx);

	/* Compute surface gradient and determinant. */
	float det = dot(dPdx, Rx);
	float absdet = abs(det);

	float dHdx = dFdx(height);
	float dHdy = dFdy(height);
	vec3 surfgrad = dHdx * Rx + dHdy * Ry;

	strength = max(strength, 0.0);

	result = normalize(absdet * N - dist * sign(det) * surfgrad);
	result = normalize(strength * result + (1.0 - strength) * N);
}

/* output */

void node_output_material(vec4 surface, vec4 volume, float displacement, out vec4 result)
{
	result = surface;
}

void node_output_world(vec4 surface, vec4 volume, out vec4 result)
{
	result = surface;
}

/* ********************** matcap style render ******************** */

void material_preview_matcap(vec4 color, sampler2D ima, vec4 N, vec4 mask, out vec4 result)
{
	vec3 normal;
	vec2 tex;
	
#ifndef USE_OPENSUBDIV
	/* remap to 0.0 - 1.0 range. This is done because OpenGL 2.0 clamps colors
	 * between shader stages and we want the full range of the normal */
	normal = vec3(2.0, 2.0, 2.0) * vec3(N.x, N.y, N.z) - vec3(1.0, 1.0, 1.0);
	if (normal.z < 0.0) {
		normal.z = 0.0;
	}
	normal = normalize(normal);
#else
	normal = inpt.v.normal;
	mask = vec4(1.0, 1.0, 1.0, 1.0);
#endif

	tex.x = 0.5 + 0.49 * normal.x;
	tex.y = 0.5 + 0.49 * normal.y;
	result = texture(ima, tex) * mask;
}

/* BGE-Remix P8: "Face Textures" (the face's image, mixed in when one is bound) */
void mtex_face_image(vec4 col, float on, vec4 rgb_in, out vec4 rgb_out, out float alpha)
{
	rgb_out = mix(rgb_in, vec4(col.rgb, rgb_in.a), on);
	alpha = mix(1.0, col.a, on);
}

void mtex_face_alpha(float texalpha, float on, float alpha_in, out float alpha_out)
{
	alpha_out = mix(alpha_in, texalpha, on);
}

/* BGE-Remix P8.5: an image texture's mapping as a render does it: the X/Y/Z
 * projection, then Flat, Cube, Tube or Sphere (wrap 0..3). co is -1..1
 * (uvmode: UV coordinates, 0..1, are brought to that range first); nor
 * picks the cube side. */
void mtex_image_mapping(vec3 co, vec3 proj, float wrap, float uvmode, vec3 nor, out vec3 outco)
{
	vec3 t;
	if (uvmode != 0.0)
		co = vec3(co.xy * 2.0 - vec2(1.0), 0.0);
	t.x = (proj.x > 0.5) ? co[int(proj.x) - 1] : 0.0;
	t.y = (proj.y > 0.5) ? co[int(proj.y) - 1] : 0.0;
	t.z = (proj.z > 0.5) ? co[int(proj.z) - 1] : 0.0;

	if (wrap == 2.0) {
		point_map_to_tube(t, outco);
	}
	else if (wrap == 3.0) {
		point_map_to_sphere(t, outco);
	}
	else if (wrap == 1.0) {
		vec3 a = abs(nor);
		if (a.z >= a.x && a.z >= a.y)
			outco = vec3(t.xy * 0.5 + vec2(0.5), 0.0);
		else if (a.y >= a.x && a.y >= a.z)
			outco = vec3(t.xz * 0.5 + vec2(0.5), 0.0);
		else
			outco = vec3(t.yz * 0.5 + vec2(0.5), 0.0);
	}
	else {
		outco = vec3(t.xy * 0.5 + vec2(0.5), 0.0);
	}
}

/* BGE-Remix P8.6: the values a bake writes (alpha: drawn here) */
void bake_out_color(vec4 rgb, float refl, float srgb, vec3 uv, out vec4 result)
{
	vec4 col = vec4(rgb.rgb * refl, 1.0);
	if (srgb != 0.0)
		linearrgb_to_srgb(col, col);
	result = vec4(clamp(col.rgb, 0.0, 1.0), 1.0);
}

void bake_out_value(float value, float scale, vec3 uv, out vec4 result)
{
	float v = clamp(value * scale, 0.0, 1.0);
	result = vec4(v, v, v, 1.0);
}

/* vn: the shading normal (Blender Internal's, pointing inwards); nor: the
 * surface's; tangent: the UV map's (w: the bitangent's sign) */
void bake_out_normal(vec3 vn, vec3 nor, vec4 tangent, vec3 uv, out vec4 result)
{
	vec3 N = normalize(nor);
	vec3 n = -normalize(vn);
	vec3 T = tangent.xyz - N * dot(N, tangent.xyz);
	T = (dot(T, T) > 0.0) ? normalize(T) : vec3(1.0, 0.0, 0.0);
	vec3 B = ((tangent.w < 0.0) ? -1.0 : 1.0) * cross(N, T);
	vec3 ts = vec3(dot(n, T), dot(n, B), dot(n, N));
	result = vec4(ts * 0.5 + vec3(0.5), 1.0);
}

/* BGE-Remix P8.7: physically based shading (metal, roughness, Fresnel: the
 * model RTX Remix shades with). Blender's vectors: vn points into the
 * surface, lv from the lamp, view from the eye. Light units are Blender
 * Internal's (Lambert without the 1/pi), so lamps keep their brightness. */

void pbr_prepare(vec3 base, float metallic, float specular, float transmission, out vec3 diffcol, out vec3 f0)
{
	float m = clamp(metallic, 0.0, 1.0);
	diffcol = base * (1.0 - m) * (1.0 - clamp(transmission, 0.0, 1.0));
	f0 = mix(vec3(0.08 * clamp(specular, 0.0, 1.0)), base, m);
}

float pbr_ggx(float nh, float a)
{
	float a2 = a * a;
	float d = nh * nh * (a2 - 1.0) + 1.0;
	return a2 / (M_PI * d * d);
}

float pbr_smith(float nv, float nl, float a)
{
	float k = a * 0.5;
	return (nv / (nv * (1.0 - k) + k)) * (nl / (nl * (1.0 - k) + k));
}

vec3 pbr_fresnel(vec3 f0, float vh)
{
	return f0 + (vec3(1.0) - f0) * pow(1.0 - clamp(vh, 0.0, 1.0), 5.0);
}

/* flags: x no diffuse, y no specular, z hemi lamp */
void shade_pbr_light(
        vec3 vn, vec3 lv, vec3 view, float inp, float visifac, float shadfac, vec3 lcol, vec3 shadowcol,
        vec3 diffcol, vec3 f0, float roughness, float subsurface, vec3 flags,
        vec4 diff_in, vec4 spec_in, out vec4 diff_out, out vec4 spec_out)
{
	vec3 N = -normalize(vn);
	vec3 L = -normalize(lv);
	vec3 V = -normalize(view);
	float nl = (flags.z != 0.0) ? 0.5 * inp + 0.5 : inp;
	/* subsurface: light wraps around the shadowed side */
	float w = 0.5 * clamp(subsurface, 0.0, 1.0);
	float dl = max((nl + w) / (1.0 + w), 0.0);
	vec3 light = lcol * visifac;

	diff_out = diff_in;
	spec_out = spec_in;
	if (flags.x == 0.0)
		diff_out.rgb += diffcol * dl * light * mix(shadowcol, vec3(1.0), shadfac);
	if (flags.y == 0.0 && flags.z == 0.0 && nl > 0.0) {
		vec3 H = normalize(L + V);
		float nv = max(dot(N, V), 1e-4);
		float nh = max(dot(N, H), 0.0);
		float a = max(roughness * roughness, 0.002);
		float nlc = max(nl, 1e-4);
		vec3 F = pbr_fresnel(f0, dot(V, H));
		vec3 s = F * (pbr_ggx(nh, a) * pbr_smith(nv, nlc, a) / (4.0 * nv * nlc)) * M_PI * nlc;
		spec_out.rgb += s * light * shadfac;
	}
}

/* the world's light: ambient colour on the diffuse part, the sky's
 * horizon-to-zenith colours reflected (rougher: more even) */
/* (P8.18, further down) */
vec3 bge_env(vec3 wp, vec3 R, float rough, vec3 fallback);
vec4 bge_ssr(vec3 co, vec3 Rv, float rough);

void shade_pbr_ambient(
        vec3 co, vec3 vn, vec3 view, vec3 diffcol, vec3 f0, float roughness, float amb, vec3 ambcol,
        vec3 horicol, vec3 zencol, mat4 viewinv, vec4 col_in, out vec4 col_out)
{
	vec3 N = -normalize(vn);
	vec3 V = -normalize(view);
	float nv = max(dot(N, V), 0.0);
	vec3 Rv = reflect(-V, N);
	vec3 R = (viewinv * vec4(Rv, 0.0)).xyz;
	vec3 sky = mix(horicol, zencol, clamp(R.z, 0.0, 1.0));
	sky = mix(sky, 0.5 * (horicol + zencol), clamp(roughness, 0.0, 1.0));
	/* BGE-Remix P8 Part B (P8.18): a probe, the sky, the screen */
	sky = bge_env((viewinv * vec4(co, 1.0)).xyz, R, roughness, sky);
	vec4 s = bge_ssr(co, Rv, roughness);
	sky = mix(sky, s.rgb, s.a);
	vec3 F = f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1.0 - nv, 5.0);
	col_out = col_in;
	col_out.rgb += amb * (ambcol * diffcol + F * sky);
}

/* glass lets light through where it does not reflect it */
void shade_pbr_alpha(vec3 vn, vec3 view, vec3 f0, float transmission, float alpha, out float outalpha)
{
	vec3 N = -normalize(vn);
	vec3 V = -normalize(view);
	float f = pbr_fresnel(f0, max(dot(N, V), 0.0)).g;
	outalpha = alpha * (1.0 - clamp(transmission, 0.0, 1.0) * (1.0 - f));
}

/* ---- BGE-Remix P8 Part B (P8.15): the clustered lamps ----
 * The material's lamps in one loop: the lamps of the pixel's cluster (a
 * compute pass sorted them, GPU_lights.h), each lit as shade_one_light in
 * gpu_material.c writes code for one lamp, with the material's own diffuse
 * and specular models and ramps. Lamps with textures or a falloff curve,
 * and Variance shadows, keep that per-lamp code. In view space; shadows in
 * world space (the lamp records are world space).
 * matp: diffuse model, specular model, flags (BGE_LF_*), light group (-1:
 * none); diffp: roughness, toon size, toon smooth (or Fresnel), darkness;
 * specp: refraction (Blinn), rms (Ward), toon size, toon smooth;
 * rampc, ramps: input, blend type, factor, 0 */

#define BGE_LF_CUBIC 1
#define BGE_LF_RAMPCOL 2
#define BGE_LF_RAMPSPEC 4
#define BGE_LF_SHADOW 8
#define BGE_LF_NOSHADERS 16
#define BGE_LF_NOSHADOWS 32
#define BGE_LF_DIFFUSE 64
#define BGE_LF_SPECULAR 128

/* a ramp blend by type (MA_RAMP_*: the mix_* functions) */
void bge_ramp_blend(float type, float fac, vec4 col1, vec4 col2, out vec4 outcol)
{
	int t = int(type + 0.5);
	if (t == 0) mix_blend(fac, col1, col2, outcol);
	else if (t == 1) mix_add(fac, col1, col2, outcol);
	else if (t == 2) mix_mult(fac, col1, col2, outcol);
	else if (t == 3) mix_sub(fac, col1, col2, outcol);
	else if (t == 4) mix_screen(fac, col1, col2, outcol);
	else if (t == 5) mix_div(fac, col1, col2, outcol);
	else if (t == 6) mix_diff(fac, col1, col2, outcol);
	else if (t == 7) mix_dark(fac, col1, col2, outcol);
	else if (t == 8) mix_light(fac, col1, col2, outcol);
	else if (t == 9) mix_overlay(fac, col1, col2, outcol);
	else if (t == 10) mix_dodge(fac, col1, col2, outcol);
	else if (t == 11) mix_burn(fac, col1, col2, outcol);
	else if (t == 12) mix_hue(fac, col1, col2, outcol);
	else if (t == 13) mix_sat(fac, col1, col2, outcol);
	else if (t == 14) mix_val(fac, col1, col2, outcol);
	else if (t == 15) mix_color(fac, col1, col2, outcol);
	else if (t == 16) mix_soft(fac, col1, col2, outcol);
	else mix_linear(fac, col1, col2, outcol);
}

#ifdef BGE_CLUSTERED

uint bge_cluster_index(vec3 co)
{
	vec2 f = (gl_FragCoord.xy - bge_cl_viewport.xy) / max(bge_cl_viewport.zw, vec2(1.0));
	uvec2 t = uvec2(clamp(f, vec2(0.0), vec2(0.99999)) * vec2(16.0, 9.0));
	float z = max(-co.z, bge_cl_depth.x);
	uint k = uint(clamp(log(z / bge_cl_depth.x) * bge_cl_depth.z, 0.0, 23.0));
	return (k * 9u + t.y) * 16u + t.x;
}


/* do_colorband_blend */
void bge_colorband_blend(sampler2D band, vec4 rp, float fac, vec4 incol, out vec4 outcol)
{
	vec4 col;
	float a;
	valtorgb(fac, band, col, a);
	bge_ramp_blend(rp.y, col.a * rp.z, incol, col, outcol);
}

/* one lamp's visibility (lamp_get_visibility): visifac, and lv, dist, the
 * view space lamp position and direction (it returns a value: the
 * library's parser reads only functions without one, and would stop at its
 * struct parameter) */
float bge_lamp_visibility(BgeLamp L, vec3 co, mat4 viewmat, mat4 viewinv, out vec3 lampco, out vec3 lampvec,
                          out vec3 lv, out float dist)
{
	float visifac;
	int type = int(L.co_type.w + 0.5);
	uint mode = L.bits.x;
	lampvec = normalize(mat3(viewmat) * L.vec_dist.xyz);
	lampco = (viewmat * vec4(L.co_type.xyz, 1.0)).xyz;
	if (type == 1 || type == 3) {
		lamp_visibility_sun_hemi(lampvec, lv, dist, visifac);
		return visifac;
	}
	lamp_visibility_other(co, lampco, lv, dist, visifac);
	if (type == 4)
		return visifac;
	int falloff = int(L.falloff.x + 0.5);
	float ldist = L.vec_dist.w;
	if (falloff == 1) lamp_falloff_invlinear(ldist, dist, visifac);
	else if (falloff == 2) lamp_falloff_invsquare(ldist, dist, visifac);
	else if (falloff == 4) lamp_falloff_sliders(ldist, L.falloff.y, L.falloff.z, dist, visifac);
	else if (falloff == 5) lamp_falloff_invcoefficients(L.coeff.x, L.coeff.y, L.coeff.z, dist, visifac);
	if ((mode & 64u) != 0u)  /* LA_SPHERE */
		lamp_visibility_sphere(ldist, dist, visifac, visifac);
	if (type == 2) {
		float inpr;
		mat4 dynimat = L.imat * viewinv;
		if ((mode & 128u) != 0u)  /* LA_SQUARE */
			lamp_visibility_spot_square(lampvec, dynimat, L.spot.zw, lv, inpr);
		else
			lamp_visibility_spot_circle(lampvec, dynimat, L.spot.zw, lv, inpr);
		lamp_visibility_spot(L.spot.x, L.spot.y, inpr, visifac, visifac);
	}
	lamp_visibility_clamp(visifac, visifac);
	return visifac;
}

/* the lamp's shadow (gpu_lamp_shadow): its map in the pool, then contact */
float bge_lamp_shadow(BgeLamp L, vec3 co, vec3 vn, vec3 lv, float inp, mat4 viewinv)
{
	int kind = int(L.shadow_color.w + 0.5);
	vec3 wp = (viewinv * vec4(co, 1.0)).xyz;
	vec3 wn = mat3(viewinv) * vn;
	float layer = L.shadow_clip.w;
	float s = 1.0;
	if (kind == 1)
		shadow_spot_soft(wp, wn, inp, layer, L.shadowmat[0], L.shadow_params, L.shadow_clip, s);
	else if (kind == 2)
		shadow_cube_soft(wp, wn, inp, layer, L.shadowmat[0], L.shadow_params, L.shadow_clip, s);
	else if (kind == 3)
		shadow_sun_soft(wp, wn, inp, layer, L.shadowmat[0], L.shadowmat[1], L.shadowmat[2], L.shadowmat[3],
		                L.cascade_scale, L.cascade_range, L.shadow_params, s);
	shadow_contact(co, lv, L.contact, s, s);
	return s;
}

/* whether the lamp lights this material and object */
bool bge_lamp_applies(BgeLamp L, int flags, float group)
{
	uint mode = L.bits.x;
	if ((mode & 4u) != 0u && (L.bits.y & uint(bge_oblay)) == 0u)  /* LA_LAYER */
		return false;
	if ((mode & 32u) != 0u && (flags & BGE_LF_SHADOW) == 0)  /* LA_ONLYSHADOW */
		return false;
	if (group >= 0.0)
		return (L.bits.z & (1u << uint(group))) != 0u;
	return (L.bits.w & 4u) == 0u;  /* in an exclusive light group */
}

void shade_lights_clustered_ex(
        vec3 co, vec3 vn, vec3 view, vec4 rgb, vec4 specrgb, float spec, float har, float refl,
        vec4 matp, vec4 diffp, vec4 specp, vec4 matq, vec4 rampc, vec4 ramps, sampler2D bandc, sampler2D bands,
        mat4 viewmat, mat4 viewinv, vec4 diff_in, vec4 spec_in, out vec4 diff_out, out vec4 spec_out)
{
	diff_out = diff_in;
	spec_out = spec_in;
	if (bge_shadowpass != 0)
		return;
	int flags = int(matp.z + 0.5);
	int dmodel = int(matp.x + 0.5), smodel = int(matp.y + 0.5);
	bool shaders = (flags & BGE_LF_NOSHADERS) == 0;
	uint ci = bge_cluster_index(co);
	uint W = bge_cl_counts.x;
	for (uint w = 0u; w < W; w++) {
		uint bits = bge_cl_mask[ci * W + w];
		while (bits != 0u) {
			int b = findLSB(bits);
			bits &= bits - 1u;
			BgeLamp L = bge_lamps[w * 32u + uint(b)];
			if (!bge_lamp_applies(L, flags, matp.w))
				continue;
			uint mode = L.bits.x;
			int type = int(L.co_type.w + 0.5);
			vec3 lampco, lampvec, lv;
			float dist;
			float visifac = bge_lamp_visibility(L, co, viewmat, viewinv, lampco, lampvec, lv, dist);

			float inp, is;
			shade_inp(vn, lv, inp);
			if ((mode & 2048u) != 0u) {  /* LA_NO_DIFF */
				is = 0.0;
			}
			else if (type == 3) {
				shade_is_hemi(inp, is);
			}
			else {
				if (type == 4) {
					mat4 area = mat4(viewmat * L.area[0], viewmat * L.area[1], viewmat * L.area[2], viewmat * L.area[3]);
					shade_inp_area(co, lampco, lampvec, vn, area, L.coeff.w, L.falloff.w, inp);
				}
				is = inp;
				if (shaders) {
					if (dmodel == 1) shade_diffuse_oren_nayer(inp, vn, lv, view, diffp.x, is);
					else if (dmodel == 2) shade_diffuse_toon(vn, lv, view, diffp.y, diffp.z, is);
					else if (dmodel == 3) shade_diffuse_minnaert(inp, vn, view, diffp.w, is);
					else if (dmodel == 4) shade_diffuse_fresnel(vn, lv, view, diffp.y, diffp.z, is);
				}
			}
			if (shaders && (flags & BGE_LF_CUBIC) != 0)
				shade_cubic(is, is);

			float i;
			shade_visifac(is, visifac, refl, i);
			vec3 lcol = L.col_energy.rgb * L.col_energy.w;

			float shadfac = 1.0;
			bool onlyshadow = (mode & 32u) != 0u;
			if ((flags & BGE_LF_SHADOW) != 0 && L.shadow_color.w > 0.5) {
				if ((flags & BGE_LF_NOSHADOWS) == 0) {
					shadfac = bge_lamp_shadow(L, co, vn, lv, inp, viewinv);
					if (onlyshadow) {
						vec3 shadrgb;
						shade_only_shadow(i, shadfac, L.col_energy.w, L.shadow_color.rgb, shadrgb);
						if ((mode & 2048u) == 0u)
							shade_only_shadow_diffuse(shadrgb, rgb.rgb, diff_out, diff_out);
						if ((mode & 4096u) == 0u)
							shade_only_shadow_specular(shadrgb, specrgb.rgb, spec_out, spec_out);
						continue;
					}
				}
			}
			else if ((flags & BGE_LF_NOSHADOWS) != 0 && onlyshadow) {
				continue;
			}

			if ((flags & BGE_LF_DIFFUSE) != 0 && (mode & 2048u) == 0u) {
				vec4 lrgb;
				shade_mul_value(i, vec4(lcol, 1.0), lrgb);
				mix_mult(1.0 - shadfac, lrgb, vec4(L.shadow_color.rgb, 1.0), lrgb);
				/* add_to_diffuse */
				vec4 addcol = rgb;
				if ((flags & BGE_LF_RAMPCOL) != 0 && int(rampc.x + 0.5) != 3) {
					int rin = int(rampc.x + 0.5);
					float fac = 0.0;
					if (rin == 1) ramp_rgbtobw(lrgb.rgb, fac);
					else if (rin == 0) fac = is;
					else if (rin == 2) fac = dot(view, vn);
					bge_colorband_blend(bandc, rampc, fac, rgb, addcol);
				}
				shade_madd(diff_out, lrgb, addcol, diff_out);
			}

			if (shaders && (flags & BGE_LF_SPECULAR) != 0 && (mode & 4096u) == 0u && !onlyshadow) {
				vec3 outcol;
				if (type == 3) {
					float t;
					shade_hemi_spec(vn, lv, view, matq.x, har, visifac, t);
					shade_add_spec(t, lcol, specrgb.rgb, outcol);
				}
				else {
					float specfac;
					if (smodel == 1) shade_phong_spec(vn, lv, view, har, specfac);
					else if (smodel == 0) shade_cooktorr_spec(vn, lv, view, har, specfac);
					else if (smodel == 2) shade_blinn_spec(vn, lv, view, specp.x, har, specfac);
					else if (smodel == 4) shade_wardiso_spec(vn, lv, view, specp.y, specfac);
					else shade_toon_spec(vn, lv, view, specp.z, specp.w, specfac);
					if (type == 4)
						shade_spec_area_inp(specfac, inp, specfac);
					float t;
					shade_spec_t(shadfac, spec, visifac, specfac, t);
					vec4 sp = specrgb;
					if ((flags & BGE_LF_RAMPSPEC) != 0 && int(ramps.x + 0.5) != 3) {
						/* do_specular_ramp */
						int rin = int(ramps.x + 0.5);
						float fac = 0.0;
						if (rin == 1) fac = t;
						else if (rin == 0) fac = specfac;
						else if (rin == 2) fac = dot(view, vn);
						bge_colorband_blend(bands, ramps, fac, specrgb, sp);
					}
					shade_add_spec(t, lcol, sp.rgb, outcol);
				}
				shade_add_clamped(spec_out, vec4(outcol, 1.0), spec_out);
			}
		}
	}
}

void shade_lights_clustered_pbr(
        vec3 co, vec3 vn, vec3 view, vec4 rgb, vec4 specrgb, float refl, vec3 diffcol, vec3 f0, float roughness,
        float subsurface, vec4 matp, mat4 viewmat, mat4 viewinv, vec4 diff_in, vec4 spec_in, out vec4 diff_out,
        out vec4 spec_out)
{
	diff_out = diff_in;
	spec_out = spec_in;
	if (bge_shadowpass != 0)
		return;
	int flags = int(matp.z + 0.5);
	uint ci = bge_cluster_index(co);
	uint W = bge_cl_counts.x;
	for (uint w = 0u; w < W; w++) {
		uint bits = bge_cl_mask[ci * W + w];
		while (bits != 0u) {
			int b = findLSB(bits);
			bits &= bits - 1u;
			BgeLamp L = bge_lamps[w * 32u + uint(b)];
			if (!bge_lamp_applies(L, flags, matp.w))
				continue;
			uint mode = L.bits.x;
			int type = int(L.co_type.w + 0.5);
			vec3 lampco, lampvec, lv;
			float dist;
			float visifac = bge_lamp_visibility(L, co, viewmat, viewinv, lampco, lampvec, lv, dist);
			float inp;
			shade_inp(vn, lv, inp);
			if (type == 4 && (mode & 2048u) == 0u) {
				mat4 area = mat4(viewmat * L.area[0], viewmat * L.area[1], viewmat * L.area[2], viewmat * L.area[3]);
				shade_inp_area(co, lampco, lampvec, vn, area, L.coeff.w, L.falloff.w, inp);
			}
			float is = ((mode & 2048u) != 0u) ? 0.0 : (type == 3) ? 0.5 * inp + 0.5 : inp;
			float i;
			shade_visifac(is, visifac, refl, i);
			vec3 lcol = L.col_energy.rgb * L.col_energy.w;
			float shadfac = 1.0;
			bool onlyshadow = (mode & 32u) != 0u;
			if ((flags & BGE_LF_SHADOW) != 0 && L.shadow_color.w > 0.5) {
				if ((flags & BGE_LF_NOSHADOWS) == 0) {
					shadfac = bge_lamp_shadow(L, co, vn, lv, inp, viewinv);
					if (onlyshadow) {
						vec3 shadrgb;
						shade_only_shadow(i, shadfac, L.col_energy.w, L.shadow_color.rgb, shadrgb);
						if ((mode & 2048u) == 0u)
							shade_only_shadow_diffuse(shadrgb, rgb.rgb, diff_out, diff_out);
						if ((mode & 4096u) == 0u)
							shade_only_shadow_specular(shadrgb, specrgb.rgb, spec_out, spec_out);
						continue;
					}
				}
			}
			else if ((flags & BGE_LF_NOSHADOWS) != 0 && onlyshadow) {
				continue;
			}
			vec3 lf = vec3(((mode & 2048u) != 0u) ? 1.0 : 0.0, ((mode & (4096u | 32u)) != 0u) ? 1.0 : 0.0,
			               (type == 3) ? 1.0 : 0.0);
			shade_pbr_light(vn, lv, view, inp, visifac, shadfac, lcol, L.shadow_color.rgb, diffcol, f0, roughness,
			                subsurface, lf, diff_out, spec_out, diff_out, spec_out);
		}
	}
}

#else  /* no storage buffers (GLSL before 4.30): no clustered lamps */

void shade_lights_clustered_ex(
        vec3 co, vec3 vn, vec3 view, vec4 rgb, vec4 specrgb, float spec, float har, float refl,
        vec4 matp, vec4 diffp, vec4 specp, vec4 matq, vec4 rampc, vec4 ramps, sampler2D bandc, sampler2D bands,
        mat4 viewmat, mat4 viewinv, vec4 diff_in, vec4 spec_in, out vec4 diff_out, out vec4 spec_out)
{
	diff_out = diff_in;
	spec_out = spec_in;
}

void shade_lights_clustered_pbr(
        vec3 co, vec3 vn, vec3 view, vec4 rgb, vec4 specrgb, float refl, vec3 diffcol, vec3 f0, float roughness,
        float subsurface, vec4 matp, mat4 viewmat, mat4 viewinv, vec4 diff_in, vec4 spec_in, out vec4 diff_out,
        out vec4 spec_out)
{
	diff_out = diff_in;
	spec_out = spec_in;
}

#endif  /* BGE_CLUSTERED */

/* without ramps (the samplers are never read) */
void shade_lights_clustered(
        vec3 co, vec3 vn, vec3 view, vec4 rgb, vec4 specrgb, float spec, float har, float refl,
        vec4 matp, vec4 diffp, vec4 specp, vec4 matq, mat4 viewmat, mat4 viewinv, vec4 diff_in, vec4 spec_in,
        out vec4 diff_out, out vec4 spec_out)
{
	shade_lights_clustered_ex(co, vn, view, rgb, specrgb, spec, har, refl, matp, diffp, specp, matq, vec4(0.0),
	                          vec4(0.0), bge_noramp, bge_noramp, viewmat, viewinv, diff_in, spec_in, diff_out,
	                          spec_out);
}

/* ---- BGE-Remix P8 Part B (P8.17): Sky & Atmosphere ----
 * Blender Internal's sun and sky (sunsky.c: the Preetham sky, its
 * atmosphere) for a sun lamp's Sky and Atmosphere effects. s: the sun's
 * direction (world, toward the sun), sky on; its theta, phi, the sky's
 * exposure (negated), colour space; zenith Y, x, y, blend type; blend
 * factor, atmosphere on, distance factor, sun intensity. p: the Perez
 * coefficients of Y (0-4), x (5-9), y (10-14). a: the atmosphere's
 * extinction (xyz), Henyey-Greenstein g; Rayleigh and Mie in-scattering
 * with their factors; the attenuated sunlight. */

float sunsky_perez(float l0, float l1, float l2, float l3, float l4, float theta, float gamma, float lvz,
                   float suntheta)
{
	float den = (1.0 + l0 * exp(l1)) * (1.0 + l2 * exp(l3 * suntheta) + l4 * cos(suntheta) * cos(suntheta));
	float num = (1.0 + l0 * exp(l1 / cos(theta))) * (1.0 + l2 * exp(l3 * gamma) + l4 * cos(gamma) * cos(gamma));
	return lvz * num / den;
}

vec3 sunsky_xyz_to_rgb(vec3 c, float space)
{
	if (space < 0.5)
		return vec3(3.50570 * c.x - 1.73964 * c.y - 0.544011 * c.z,
		            -1.06906 * c.x + 1.97781 * c.y + 0.0351720 * c.z,
		            0.0563117 * c.x - 0.196994 * c.y + 1.05005 * c.z);
	if (space < 1.5)
		return vec3(3.240476 * c.x - 1.537150 * c.y - 0.498535 * c.z,
		            -0.969256 * c.x + 1.875992 * c.y + 0.041556 * c.z,
		            0.055648 * c.x - 0.204043 * c.y + 1.057311 * c.z);
	return vec3(2.28783848734076 * c.x - 0.833367677835217 * c.y - 0.454470795871421 * c.z,
	            -0.511651380743862 * c.x + 1.42275837632178 * c.y + 0.0888930017552939 * c.z,
	            0.00572040983140966 * c.x - 0.0159068485104036 * c.y + 1.0101864083734 * c.z);
}

/* the sky's colour toward v (world, normalized; GetSkyXYZRadiancef) */
vec3 sunsky_radiance(vec3 v, mat4 s, mat4 p)
{
	if (v.z < 0.001)
		v = normalize(vec3(v.xy, 0.001));
	float theta = acos(clamp(v.z, -1.0, 1.0));
	float phi = (abs(theta) < 1e-5) ? 0.0 : atan(v.y, v.x);
	float suntheta = s[1].x, sunphi = s[1].y;
	float hfade = 1.0, nfade = 1.0;
	if (theta > 1.5707963) {
		hfade = 1.0 - (theta * 0.31830989 - 0.5) * 2.0;
		hfade = hfade * hfade * (3.0 - 2.0 * hfade);
		theta = 1.5707963;
	}
	if (suntheta > 1.5707963 && theta <= 1.5707963) {
		nfade = 1.0 - (0.5 - theta * 0.31830989) * 2.0;
		nfade *= 1.0 - (suntheta * 0.31830989 - 0.5) * 2.0;
		nfade = nfade * nfade * (3.0 - 2.0 * nfade);
	}
	float cospsi = sin(theta) * sin(suntheta) * cos(sunphi - phi) + cos(theta) * cos(suntheta);
	float gamma = (cospsi > 1.0) ? 0.0 : (cospsi < -1.0) ? 3.14159265 : acos(cospsi);
	float x = sunsky_perez(p[1].y, p[1].z, p[1].w, p[2].x, p[2].y, theta, gamma, s[2].y, suntheta);
	float y = sunsky_perez(p[2].z, p[2].w, p[3].x, p[3].y, p[3].z, theta, gamma, s[2].z, suntheta);
	float Y = 6.666666667e-5 * nfade * hfade *
	          sunsky_perez(p[0].x, p[0].y, p[0].z, p[0].w, p[1].x, theta, gamma, s[2].x, suntheta);
	if (s[1].z != 0.0)
		Y = 1.0 - exp(Y * s[1].z);
	vec3 xyz = vec3((x / y) * Y, Y, ((1.0 - x - y) / y) * Y);
	return sunsky_xyz_to_rgb(xyz, s[1].w);
}

/* the world's colour with the sky blended over it (shadeSunView); view: from
 * the camera into the world */
void world_sunsky(vec3 view, mat4 s, mat4 p, vec3 col_in, out vec3 col_out)
{
	col_out = col_in;
	if (s[0].w < 0.5)
		return;
	vec3 v = normalize(view);
	v.z = max(v.z, 0.0);
	v = normalize(v);
	vec4 sky = vec4(sunsky_radiance(v, s, p), 1.0);
	vec4 res;
	bge_ramp_blend(s[2].w, s[3].x, vec4(col_in, 1.0), sky, res);
	col_out = res.rgb;
}

/* aerial perspective on a surface (AtmospherePixleShader, as F12 calls it:
 * the view vector in camera space, the distance its depth) */
void shade_atmosphere(vec3 co, mat4 s, mat4 a, vec4 col_in, out vec4 col_out)
{
	col_out = col_in;
	if (s[3].y < 0.5 || s[0].z < 0.0)
		return;
	vec3 view = normalize(co);
	float dist = -co.z * s[3].z;
	float costheta = dot(view, s[0].xyz);
	float phase1 = 1.0 + costheta * costheta;
	vec3 E1 = exp(a[0].xyz * (-dist / 0.69314718));
	float g = a[0].w;
	float t = 1.0 + g - 2.0 * g * costheta;
	float phase2 = (1.0 - g * g) / (t * sqrt(t));
	vec3 I = (a[1].xyz * phase1 + a[2].xyz * phase2) * (vec3(1.0) - E1) / a[0].xyz;
	I *= a[1].w * s[3].w;
	vec3 E = E1 * a[2].w * a[3].xyz;
	/* (the colour is not premultiplied here) */
	col_out = vec4(col_in.rgb * E + I, col_in.a);
}

/* ---- BGE-Remix P8 Part B (P8.18): reflections ----
 * bge_env: the light from direction R (world) at the point wp (world): the
 * nearest probe that reaches it, blurred by roughness, or the sky, or the
 * fallback (the horizon and zenith colours). bge_ssr: the reflected ray
 * (view space) traced through the scene's depth; the colour the last frame
 * showed where it hits (a: how much to trust it). */

vec3 bge_env(vec3 wp, vec3 R, float rough, vec3 fallback)
{
#ifdef BGE_CLUSTERED
	int best = -1;
	float bd = 1e30;
	for (uint i = 0u; i < bge_probe_head.x; i++) {
		vec4 c = bge_probe[i * 2u];
		float d = distance(wp, c.xyz);
		if (d < c.w && d < bd) {
			bd = d;
			best = int(i);
		}
	}
	if (best >= 0) {
		float lod = clamp(rough, 0.0, 1.0) * float(max(bge_probe_head.y, 1u) - 1u);
		return textureLod(bge_probes, vec4(R, bge_probe[uint(best) * 2u + 1u].x), lod).rgb;
	}
	if (bge_sky_on != 0)
		return textureLod(bge_skycube, R, clamp(rough, 0.0, 1.0) * 4.0).rgb;
#endif
	return fallback;
}

vec4 bge_ssr(vec3 co, vec3 Rv, float rough)
{
#ifdef BGE_CLUSTERED
	if (bge_ssr_on == 0 || bge_scenedepth_on < 0.5 || rough > 0.6)
		return vec4(0.0);
	float stepl = max(-co.z, 0.2) * 0.04;
	vec3 p = co;
	vec3 prev = co;
	for (int i = 0; i < 32; i++) {
		prev = p;
		p += Rv * stepl;
		stepl *= 1.12;
		vec4 c = bge_proj * vec4(p, 1.0);
		if (c.w <= 0.0)
			break;
		vec2 uv = c.xy / c.w * 0.5 + 0.5;
		if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
			break;
		float dz = shadow_view_z(texture(bge_scenedepth, uv).r) - p.z;
		if (dz > 0.0 && dz < max(stepl * 2.0, 0.1)) {
			/* refined between the last two points */
			vec3 a = prev, b = p;
			for (int k = 0; k < 5; k++) {
				vec3 m = 0.5 * (a + b);
				vec4 cm = bge_proj * vec4(m, 1.0);
				vec2 um = cm.xy / cm.w * 0.5 + 0.5;
				if (shadow_view_z(texture(bge_scenedepth, um).r) - m.z > 0.0) b = m; else a = m;
			}
			vec4 cb = bge_proj * vec4(b, 1.0);
			uv = cb.xy / cb.w * 0.5 + 0.5;
			vec2 edge = min(uv, 1.0 - uv);
			float fade = clamp(min(edge.x, edge.y) * 10.0, 0.0, 1.0) * (1.0 - float(i) / 32.0) *
			             clamp(1.0 - rough / 0.6, 0.0, 1.0);
			return vec4(textureLod(bge_history, uv, 0.0).rgb, fade);
		}
	}
#endif
	return vec4(0.0);
}

/* a planar mirror's picture at this pixel, blurred by roughness (a: on) */
vec4 bge_planar(float rough)
{
#ifdef BGE_CLUSTERED
	if (bge_mirror_on != 0) {
		/* the mirror's picture covers the view (the clusters' viewport) */
		vec2 uv = (gl_FragCoord.xy - bge_cl_viewport.xy) / max(bge_cl_viewport.zw, vec2(1.0));
		return vec4(textureLod(bge_mirror, uv, clamp(rough, 0.0, 1.0) * 6.0).rgb, 1.0);
	}
#endif
	return vec4(0.0);
}

/* Blender Internal's ray mirror: how much reflects (fresnel_fac in F12) */
float bge_fresnel_fac(vec3 view, vec3 vn, float grad, float fac)
{
	if (fac == 0.0)
		return 1.0;
	float t1 = dot(view, vn);
	float t2 = (t1 > 0.0) ? 1.0 + t1 : 1.0 - t1;
	return clamp(grad + (1.0 - grad) * pow(t2, fac), 0.0, 1.0);
}

/* a ray mirror material's reflection as the mirror colour input expects it
 * (refcol: amount, colour); mp: reflectivity, Fresnel, Fresnel blend,
 * gloss; planar: the material reads a planar mirror */
void shade_ray_mirror(vec3 co, vec3 vn, vec3 view, mat4 viewinv, vec4 mp, float planar, vec4 refcol_in,
                      out vec4 refcol_out)
{
	float amount = mp.x * bge_fresnel_fac(view, vn, mp.z, mp.y);
	float rough = clamp(1.0 - mp.w, 0.0, 1.0);
	/* Blender Internal's normals point into the surface */
	vec3 N = -normalize(vn);
	vec3 Rv = reflect(normalize(co), N);
	vec3 R = (viewinv * vec4(Rv, 0.0)).xyz;
	vec3 wp = (viewinv * vec4(co, 1.0)).xyz;
	vec3 col = bge_env(wp, R, rough, refcol_in.gba);
	if (planar > 0.5) {
		vec4 m = bge_planar(rough);
		col = mix(col, m.rgb, m.a);
	}
	else {
		vec4 s = bge_ssr(co, Rv, rough);
		col = mix(col, s.rgb, s.a);
	}
	refcol_out = vec4(amount, col);
}

/* an Env Map texture (Static, Animated: a probe; Planar: a mirror) */
void mtex_probe_refl(vec3 co, vec3 vn, mat4 viewinv, float planar, out float value, out vec4 color)
{
	vec3 N = -normalize(vn);
	vec3 Rv = reflect(normalize(co), N);
	vec3 R = (viewinv * vec4(Rv, 0.0)).xyz;
	vec3 wp = (viewinv * vec4(co, 1.0)).xyz;
	color = vec4(bge_env(wp, R, 0.0, vec3(0.0)), 1.0);
	if (planar > 0.5) {
		vec4 m = bge_planar(0.0);
		color.rgb = mix(color.rgb, m.rgb, m.a);
	}
	value = 1.0;
}
