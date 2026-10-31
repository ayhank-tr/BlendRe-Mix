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

/** \file blender/gpu/intern/gpu_translate.c
 *  \ingroup gpu
 *
 * BGE-Remix P8.3: old GLSL to GLSL 4.50 (see GPU_translate.h). The text is
 * read token by token (comments kept, identifiers looked up): old keywords,
 * texture functions and built-ins are replaced; #version is dropped and
 * #extension lines move to the top.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"
#include "BLI_dynstr.h"

#include "GPU_glew.h"
#include "GPU_glcheck.h"
#include "GPU_translate.h"

/* ---- the front ends ---- */

static const char *tfe_uniforms_compat =
        "#define bge_mv gl_ModelViewMatrix\n"
        "#define bge_proj gl_ProjectionMatrix\n"
        "#define bge_mvp gl_ModelViewProjectionMatrix\n"
        "#define bge_mvinv gl_ModelViewMatrixInverse\n"
        "#define bge_projinv gl_ProjectionMatrixInverse\n"
        "#define bge_mvpinv gl_ModelViewProjectionMatrixInverse\n"
        "#define bge_normalmat gl_NormalMatrix\n"
        "#define bge_texturematrix gl_TextureMatrix\n"
        "#define bge_lightsource gl_LightSource\n"
        "#define bge_frontmaterial gl_FrontMaterial\n"
        "#define bge_backmaterial gl_BackMaterial\n"
        "#define bge_lightmodel gl_LightModel\n"
        "#define bge_frontlightmodelproduct gl_FrontLightModelProduct\n"
        "#define bge_frontlightproduct gl_FrontLightProduct\n"
        "#define bge_fog gl_Fog\n";

/* the old built-ins themselves (a 2D filter; a custom shader whose core
 * translation does not build, drawn from the old arrays) */
static const char *tfe_vert_compat =
        "#define bge_pos gl_Vertex\n"
        "#define bge_nor gl_Normal\n"
        "#define bge_col gl_Color\n"
        "#define bge_texco0 gl_MultiTexCoord0\n"
        "#define bge_texco1 gl_MultiTexCoord1\n"
        "#define bge_texco2 gl_MultiTexCoord2\n"
        "#define bge_texco3 gl_MultiTexCoord3\n"
        "#define bge_texco4 gl_MultiTexCoord4\n"
        "#define bge_texco5 gl_MultiTexCoord5\n"
        "#define bge_texco6 gl_MultiTexCoord6\n"
        "#define bge_texco7 gl_MultiTexCoord7\n"
        "#define bge_col2 gl_SecondaryColor\n"
        "#define bge_texcoord gl_TexCoord\n"
        "#define bge_frontcolor gl_FrontColor\n"
        "#define bge_backcolor gl_BackColor\n"
        "#define bge_frontcolor2 gl_FrontSecondaryColor\n"
        "#define bge_backcolor2 gl_BackSecondaryColor\n"
        "#define bge_fogcoord gl_FogFragCoord\n"
        "#define bge_clipvertex gl_ClipVertex\n";

static const char *tfe_frag_compat =
        "#define bge_texcoord gl_TexCoord\n"
        "#define bge_frontcolor gl_Color\n"
        "#define bge_frontcolor2 gl_SecondaryColor\n"
        "#define bge_fogcoord gl_FogFragCoord\n"
        "#define bge_fragcolor gl_FragColor\n"
        "#define bge_fragdata gl_FragData\n";

static const char *tfe_uniforms_core =
        "uniform mat4 bge_mv;\n"
        "uniform mat4 bge_proj;\n"
        "uniform mat4 bge_mvp;\n"
        "uniform mat4 bge_mvinv;\n"
        "uniform mat4 bge_projinv;\n"
        "uniform mat4 bge_mvpinv;\n"
        "uniform mat3 bge_normalmat;\n"
        "uniform mat4 bge_texturematrix[8];\n"
        "struct BgeLightSourceParameters { vec4 ambient; vec4 diffuse; vec4 specular; vec4 position;\n"
        "	vec4 halfVector; vec3 spotDirection; float spotExponent; float spotCutoff; float spotCosCutoff;\n"
        "	float constantAttenuation; float linearAttenuation; float quadraticAttenuation; };\n"
        "uniform BgeLightSourceParameters bge_lightsource[8];\n"
        "struct BgeMaterialParameters { vec4 emission; vec4 ambient; vec4 diffuse; vec4 specular; float shininess; };\n"
        "uniform BgeMaterialParameters bge_frontmaterial;\n"
        "uniform BgeMaterialParameters bge_backmaterial;\n"
        "struct BgeLightModelParameters { vec4 ambient; };\n"
        "uniform BgeLightModelParameters bge_lightmodel;\n"
        "struct BgeLightModelProducts { vec4 sceneColor; };\n"
        "uniform BgeLightModelProducts bge_frontlightmodelproduct;\n"
        "struct BgeLightProducts { vec4 ambient; vec4 diffuse; vec4 specular; };\n"
        "uniform BgeLightProducts bge_frontlightproduct[8];\n"
        "struct BgeFogParameters { vec4 color; float density; float start; float end; float scale; };\n"
        "uniform BgeFogParameters bge_fog;\n";

static const char *tfe_vert_core =
        "in vec4 bge_pos;\n"
        "in vec3 bge_nor;\n"
        "in vec4 bge_col;\n"
        "in vec4 bge_col2;\n"
        "in vec4 bge_texco0;\n"
        "in vec4 bge_texco1;\n"
        "in vec4 bge_texco2;\n"
        "in vec4 bge_texco3;\n"
        "in vec4 bge_texco4;\n"
        "in vec4 bge_texco5;\n"
        "in vec4 bge_texco6;\n"
        "in vec4 bge_texco7;\n"
        "out vec4 bge_texcoord[8];\n"
        "out vec4 bge_frontcolor;\n"
        "out vec4 bge_backcolor;\n"
        "out vec4 bge_frontcolor2;\n"
        "out vec4 bge_backcolor2;\n"
        "out float bge_fogcoord;\n"
        "vec4 bge_clipvertex;\n";

static const char *tfe_frag_core =
        "in vec4 bge_texcoord[8];\n"
        "in vec4 bge_frontcolor;\n"
        "in vec4 bge_frontcolor2;\n"
        "in float bge_fogcoord;\n";

/* the vertex stage a 2D filter gets with the core front end (the old one
 * had none): BGE-Remix P8 Part B, one full-screen triangle (its corners
 * are attribute 0, GPU_draw_fullscreen_triangle), the old texture
 * coordinates 0 (the screen) and 3 (the canvas, bge_canvascoord: left,
 * right, bottom, top) */
static const char *tfe_filter_vert_core =
        "layout(location = 0) in vec4 bge_pos;\n"
        "uniform vec4 bge_canvascoord;\n"
        "out vec4 bge_texcoord[8];\n"
        "out vec4 bge_frontcolor;\n"
        "out vec4 bge_frontcolor2;\n"
        "out float bge_fogcoord;\n"
        "void main() {\n"
        "	gl_Position = vec4(bge_pos.xy, 0.0, 1.0);\n"
        "	vec2 t = bge_pos.xy * 0.5 + 0.5;\n"
        "	bge_texcoord[0] = vec4(t, 0.0, 1.0);\n"
        "	bge_texcoord[3] = vec4(mix(bge_canvascoord.x, bge_canvascoord.y, t.x),\n"
        "	                       mix(bge_canvascoord.z, bge_canvascoord.w, t.y), 0.0, 1.0);\n"
        "	bge_frontcolor = vec4(1.0); bge_frontcolor2 = vec4(0.0); bge_fogcoord = 0.0;\n"
        "}\n";

/* ---- the tables ---- */

typedef struct Rename {
	const char *from, *vert, *frag;  /* NULL: the same as vert */
} Rename;

static const Rename renames[] = {
	/* keywords */
	{"attribute", "in", "in"},
	{"varying", "out", "in"},
	/* texture functions */
	{"texture1D", "texture", NULL},
	{"texture1DProj", "textureProj", NULL},
	{"texture1DLod", "textureLod", NULL},
	{"texture2D", "texture", NULL},
	{"texture2DProj", "textureProj", NULL},
	{"texture2DLod", "textureLod", NULL},
	{"texture2DProjLod", "textureProjLod", NULL},
	{"texture2DRect", "texture", NULL},
	{"texture3D", "texture", NULL},
	{"texture3DProj", "textureProj", NULL},
	{"texture3DLod", "textureLod", NULL},
	{"textureCube", "texture", NULL},
	{"textureCubeLod", "textureLod", NULL},
	/* matrices */
	{"gl_ModelViewMatrix", "bge_mv", NULL},
	{"gl_ProjectionMatrix", "bge_proj", NULL},
	{"gl_ModelViewProjectionMatrix", "bge_mvp", NULL},
	{"gl_ModelViewMatrixInverse", "bge_mvinv", NULL},
	{"gl_ProjectionMatrixInverse", "bge_projinv", NULL},
	{"gl_ModelViewProjectionMatrixInverse", "bge_mvpinv", NULL},
	{"gl_NormalMatrix", "bge_normalmat", NULL},
	{"gl_TextureMatrix", "bge_texturematrix", NULL},
	{"gl_ModelViewMatrixTranspose", "transpose(bge_mv)", NULL},
	{"gl_ProjectionMatrixTranspose", "transpose(bge_proj)", NULL},
	{"gl_ModelViewProjectionMatrixTranspose", "transpose(bge_mvp)", NULL},
	{"gl_ModelViewMatrixInverseTranspose", "transpose(bge_mvinv)", NULL},
	{"gl_ProjectionMatrixInverseTranspose", "transpose(bge_projinv)", NULL},
	/* lighting, material, fog */
	{"gl_LightSource", "bge_lightsource", NULL},
	{"gl_FrontMaterial", "bge_frontmaterial", NULL},
	{"gl_BackMaterial", "bge_backmaterial", NULL},
	{"gl_LightModel", "bge_lightmodel", NULL},
	{"gl_FrontLightModelProduct", "bge_frontlightmodelproduct", NULL},
	{"gl_FrontLightProduct", "bge_frontlightproduct", NULL},
	{"gl_Fog", "bge_fog", NULL},
	/* vertex inputs, varyings, outputs */
	{"gl_Vertex", "bge_pos", NULL},
	{"gl_Normal", "bge_nor", NULL},
	{"gl_Color", "bge_col", "bge_frontcolor"},
	{"gl_SecondaryColor", "bge_col2", "bge_frontcolor2"},
	{"gl_MultiTexCoord0", "bge_texco0", NULL},
	{"gl_MultiTexCoord1", "bge_texco1", NULL},
	{"gl_MultiTexCoord2", "bge_texco2", NULL},
	{"gl_MultiTexCoord3", "bge_texco3", NULL},
	{"gl_MultiTexCoord4", "bge_texco4", NULL},
	{"gl_MultiTexCoord5", "bge_texco5", NULL},
	{"gl_MultiTexCoord6", "bge_texco6", NULL},
	{"gl_MultiTexCoord7", "bge_texco7", NULL},
	{"gl_TexCoord", "bge_texcoord", NULL},
	{"gl_FrontColor", "bge_frontcolor", NULL},
	{"gl_BackColor", "bge_backcolor", NULL},
	{"gl_FrontSecondaryColor", "bge_frontcolor2", NULL},
	{"gl_BackSecondaryColor", "bge_backcolor2", NULL},
	{"gl_FogFragCoord", "bge_fogcoord", NULL},
	{"gl_ClipVertex", "bge_clipvertex", NULL},
	{"gl_FragColor", "bge_fragcolor", NULL},
	{"gl_FragData", "bge_fragdata", NULL},
};

/* built-ins GLSL 4.50 core still has */
static const char *keeps[] = {
	"gl_Position", "gl_PointSize", "gl_ClipDistance", "gl_FragCoord", "gl_FrontFacing", "gl_PointCoord",
	"gl_FragDepth", "gl_VertexID", "gl_InstanceID", "gl_PrimitiveID", "gl_Layer", "gl_SampleID",
	"gl_SamplePosition", "gl_DepthRange", "gl_MaxDrawBuffers", "gl_MaxTextureImageUnits",
	"gl_MaxVertexAttribs", "gl_MaxClipDistances", "gl_ViewportIndex",
};

/* names old GLSL allowed that GLSL 4.50 reserves (a user's "sample" or
 * "texture" variable): given a suffix */
static const char *reserved_now[] = {
	"texture", "sample", "patch", "subroutine", "precise", "buffer", "shared", "coherent", "restrict",
	"readonly", "writeonly", "resource", "common", "partition", "active", "filter", "noperspective",
	"flat", "smooth", "layout", "uint", "switch", "case", "default", "double", "dvec2", "dvec3",
	"dvec4", "dmat2", "dmat3", "dmat4", "atomic_uint", "precision", "lowp", "mediump", "highp",
	"uvec2", "uvec3", "uvec4", "isampler2D", "usampler2D", "sampler2DArray", "samplerBuffer",
	"image2D", "image3D", "imageCube",
};

static bool reserved_lookup(const char *name, size_t len)
{
	for (size_t i = 0; i < ARRAY_SIZE(reserved_now); i++) {
		if (strlen(reserved_now[i]) == len && strncmp(reserved_now[i], name, len) == 0) {
			return true;
		}
	}
	return false;
}

static const Rename *rename_lookup(const char *name, size_t len)
{
	for (size_t i = 0; i < ARRAY_SIZE(renames); i++) {
		if (strlen(renames[i].from) == len && strncmp(renames[i].from, name, len) == 0) {
			return &renames[i];
		}
	}
	return NULL;
}

static bool keep_lookup(const char *name, size_t len)
{
	for (size_t i = 0; i < ARRAY_SIZE(keeps); i++) {
		if (strlen(keeps[i]) == len && strncmp(keeps[i], name, len) == 0) {
			return true;
		}
	}
	return false;
}

/* ---- the translation ---- */

#define MAX_WRAP 32

char *GPU_glsl_translate(const char *source, int stage, bool core, const char *who, int *r_problems)
{
	DynStr *body = BLI_dynstr_new();
	DynStr *exts = BLI_dynstr_new();
	const bool frag = (stage == GPU_TRANSLATE_FRAGMENT);
	int problems = 0, line = 1, depth = 0;
	int wrap_depth[MAX_WRAP], nwrap = 0;   /* parenthesis depths to close an extra ")" at */
	bool uses_fragdata = false, uses_fragcolor = false, at_line_start = true;
	const char *p = source ? source : "";

	while (*p) {
		/* comments */
		if (p[0] == '/' && p[1] == '/') {
			const char *e = strchr(p, '\n');
			const size_t n = e ? (size_t)(e - p) : strlen(p);
			BLI_dynstr_nappend(body, p, (int)n);
			p += n;
			continue;
		}
		if (p[0] == '/' && p[1] == '*') {
			const char *e = strstr(p + 2, "*/");
			const size_t n = e ? (size_t)(e + 2 - p) : strlen(p);
			for (size_t i = 0; i < n; i++) {
				if (p[i] == '\n') {
					line++;
				}
			}
			BLI_dynstr_nappend(body, p, (int)n);
			p += n;
			continue;
		}
		/* directives: #version goes, #extension moves to the top */
		if (at_line_start && *p == '#') {
			const char *e = strchr(p, '\n');
			const size_t n = e ? (size_t)(e - p) : strlen(p);
			const char *q = p + 1;
			while (*q == ' ' || *q == '\t') {
				q++;
			}
			if (strncmp(q, "version", 7) == 0) {
				p += n;
				continue;
			}
			if (strncmp(q, "extension", 9) == 0) {
				BLI_dynstr_nappend(exts, p, (int)n);
				BLI_dynstr_append(exts, "\n");
				p += n;
				continue;
			}
			/* other directives: translated like code (macros may use built-ins) */
		}
		if (*p == '\n') {
			line++;
			at_line_start = true;
			BLI_dynstr_append(body, "\n");
			p++;
			continue;
		}
		if (*p == ' ' || *p == '\t' || *p == '\r') {
			BLI_dynstr_nappend(body, p, 1);
			p++;
			continue;
		}
		at_line_start = false;

		if (isalpha((unsigned char)*p) || *p == '_') {
			const char *s = p;
			while (isalnum((unsigned char)*p) || *p == '_') {
				p++;
			}
			const size_t len = (size_t)(p - s);
			const Rename *r = rename_lookup(s, len);

			/* ftransform() */
			if (len == 10 && strncmp(s, "ftransform", 10) == 0) {
				const char *q = p;
				while (*q == ' ' || *q == '\t') {
					q++;
				}
				if (q[0] == '(') {
					q++;
					while (*q == ' ' || *q == '\t') {
						q++;
					}
					if (*q == ')') {
						BLI_dynstr_append(body, "(bge_mvp * bge_pos)");
						p = q + 1;
						continue;
					}
				}
			}
			/* shadow2D(...) returned a vec4; texture() on a shadow sampler a float */
			if ((len == 8 && strncmp(s, "shadow2D", 8) == 0) ||
			    (len == 12 && strncmp(s, "shadow2DProj", 12) == 0) ||
			    (len == 8 && strncmp(s, "shadow1D", 8) == 0))
			{
				const char *q = p;
				while (*q == ' ' || *q == '\t') {
					q++;
				}
				if (*q == '(' && nwrap < MAX_WRAP) {
					BLI_dynstr_append(body, (len == 12) ? "vec4(textureProj(" : "vec4(texture(");
					wrap_depth[nwrap++] = depth;
					depth++;
					p = q + 1;
					continue;
				}
			}

			if (r) {
				const char *to = (frag && r->frag) ? r->frag : r->vert;
				BLI_dynstr_append(body, to);
				if (STREQ(r->from, "gl_FragData")) {
					uses_fragdata = true;
				}
				if (STREQ(r->from, "gl_FragColor")) {
					uses_fragcolor = true;
				}
			}
			else if (reserved_lookup(s, len)) {
				BLI_dynstr_nappend(body, s, (int)len);
				BLI_dynstr_append(body, "_bge");
			}
			else if (len > 3 && strncmp(s, "gl_", 3) == 0 && !keep_lookup(s, len)) {
				/* a built-in GLSL 4.50 core lacks: kept, and reported */
				printf("BGE-Remix GLSL translate %s: line %d: %.*s is not translated\n",
				       who ? who : "", line, (int)len, s);
				problems++;
				BLI_dynstr_nappend(body, s, (int)len);
			}
			else {
				BLI_dynstr_nappend(body, s, (int)len);
			}
			continue;
		}
		if (*p == '(') {
			depth++;
		}
		else if (*p == ')') {
			depth--;
			BLI_dynstr_append(body, ")");
			p++;
			if (nwrap > 0 && wrap_depth[nwrap - 1] == depth) {
				BLI_dynstr_append(body, ")");
				nwrap--;
			}
			continue;
		}
		BLI_dynstr_nappend(body, p, 1);
		p++;
	}

	/* the stage: version, extensions, front end, the text */
	DynStr *out = BLI_dynstr_new();
	BLI_dynstr_append(out, core ? "#version 450 core\n" : "#version 450 compatibility\n");
	char *ext = BLI_dynstr_get_cstring(exts);
	BLI_dynstr_append(out, ext);
	MEM_freeN(ext);
	BLI_dynstr_append(out, core ? tfe_uniforms_core : tfe_uniforms_compat);
	if (frag) {
		BLI_dynstr_append(out, core ? tfe_frag_core : tfe_frag_compat);
		if (core) {
			if (uses_fragdata) {
				BLI_dynstr_append(out, "out vec4 bge_fragdata[4];\n");
			}
			else if (uses_fragcolor) {
				BLI_dynstr_append(out, "out vec4 bge_fragcolor;\n");
			}
		}
	}
	else {
		BLI_dynstr_append(out, core ? tfe_vert_core : tfe_vert_compat);
	}
	BLI_dynstr_append(out, "#line 1\n");
	char *text = BLI_dynstr_get_cstring(body);
	BLI_dynstr_append(out, text);
	MEM_freeN(text);
	char *result = BLI_dynstr_get_cstring(out);
	BLI_dynstr_free(out);
	BLI_dynstr_free(body);
	BLI_dynstr_free(exts);

	if (r_problems) {
		*r_problems = problems;
	}
	return result;
}

/* ---- building ---- */

static GLuint build_stage(GLenum type, const char *src, const char *who, const char *what, bool quiet)
{
	GLuint sh = glCreateShader(type);
	glShaderSource(sh, 1, &src, NULL);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		if (!quiet) {
			char log[2048];
			glGetShaderInfoLog(sh, sizeof(log), NULL, log);
			printf("BGE-Remix GLSL translate %s: the %s %s stage does not build:\n%s\n",
			       who ? who : "", what, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
		}
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}

static GLuint build_program(const char *vert, const char *frag, const char *who, const char *what)
{
	GLuint vs = 0, fs = 0;
	if (vert && !(vs = build_stage(GL_VERTEX_SHADER, vert, who, what, false))) {
		return 0;
	}
	if (frag && !(fs = build_stage(GL_FRAGMENT_SHADER, frag, who, what, false))) {
		if (vs) {
			glDeleteShader(vs);
		}
		return 0;
	}
	GLuint prog = glCreateProgram();
	if (vs) {
		glAttachShader(prog, vs);
	}
	if (fs) {
		glAttachShader(prog, fs);
	}
	/* BGE-Remix P8.4: a custom shader's "Tangent" attribute is fed at place 1
	 * (setAttrib, BL_Shader::SHD_TANGENT); stock 2.79 got it there by the
	 * linker's order, which the front end's own attributes change.
	 * BGE-Remix P8 Part B: the core front end's position at place 0 (a
	 * compatibility context draws only when attribute 0 is fed) */
	if (vs) {
		glBindAttribLocation(prog, 1, "Tangent");
		if (STREQ(what, "core")) {
			glBindAttribLocation(prog, 0, "bge_pos");
		}
	}
	glLinkProgram(prog);
	GLint ok = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (vs) {
		glDeleteShader(vs);
	}
	if (fs) {
		glDeleteShader(fs);
	}
	if (!ok) {
		char log[2048];
		glGetProgramInfoLog(prog, sizeof(log), NULL, log);
		printf("BGE-Remix GLSL translate %s: the %s program does not link:\n%s\n", who ? who : "", what, log);
		glDeleteProgram(prog);
		return 0;
	}
	return prog;
}

unsigned int GPU_glsl_translate_program(const char *vert, const char *frag, const char *who)
{
	return GPU_glsl_translate_program_ex(vert, frag, who, false);
}

unsigned int GPU_glsl_translate_program_ex(const char *vert, const char *frag, const char *who, bool game)
{
	int pv = 0, pf = 0, cv = 0, cf = 0;
	const bool has_vert = vert && vert[0];
	const bool has_frag = frag && frag[0];
	if (!has_vert && !has_frag) {
		return 0;
	}

	/* the core translation (the drawing layer's front end) */
	char *cvs = has_vert ? GPU_glsl_translate(vert, GPU_TRANSLATE_VERTEX, true, game ? who : NULL, &cv) : NULL;
	char *cfs = has_frag ? GPU_glsl_translate(frag, GPU_TRANSLATE_FRAGMENT, true, NULL, &cf) : NULL;
	char *filter_vert = NULL;
	const char *cv_src = cvs;
	if (!cvs) {
		DynStr *ds = BLI_dynstr_new();
		BLI_dynstr_append(ds, "#version 450 core\n");
		BLI_dynstr_append(ds, tfe_filter_vert_core);
		filter_vert = BLI_dynstr_get_cstring(ds);
		BLI_dynstr_free(ds);
		cv_src = filter_vert;
	}

	GLuint prog = 0;
	bool core_ok = false;
	if (game) {
		/* BGE-Remix P8 Part B: a custom shader or 2D filter draws with the
		 * core translation (its matrices, lamps, material and fog are
		 * uniforms the game sets; a filter is one full-screen triangle);
		 * the compatibility one when that does not build */
		prog = build_program(cv_src, cfs, who, "core");
		core_ok = prog != 0;
		pv = cv;
		pf = cf;
	}
	if (!prog) {
		/* the program the old drawing code draws with */
		char *v = has_vert ? GPU_glsl_translate(vert, GPU_TRANSLATE_VERTEX, false, who, &pv) : NULL;
		char *f = has_frag ? GPU_glsl_translate(frag, GPU_TRANSLATE_FRAGMENT, false, NULL, &pf) : NULL;
		prog = build_program(v, f, who, "translated");
		if (v) MEM_freeN(v);
		if (f) MEM_freeN(f);

		/* and whether the core translation builds too */
		if (prog && !game) {
			const GLuint core_prog = build_program(cv_src, cfs, who, "core");
			core_ok = core_prog != 0;
			if (core_prog) {
				glDeleteProgram(core_prog);
			}
		}
	}
	if (cvs) MEM_freeN(cvs);
	if (cfs) MEM_freeN(cfs);
	if (filter_vert) MEM_freeN(filter_vert);

	printf("BGE-Remix GLSL translate %s: %s, %d not translated, core %s\n", who ? who : "",
	       prog ? "built" : "not built (the original is used)", pv + pf, core_ok ? "ok" : "FAILED");
	fflush(stdout);
	return prog;
}

/* ---- names ---- */

const char *GPU_glsl_translated_name(const char *name, char *buf, int buflen)
{
	if (name && reserved_lookup(name, strlen(name)) && buflen > 0) {
		snprintf(buf, (size_t)buflen, "%s_bge", name);
		return buf;
	}
	return name;
}

int GPU_glsl_uniform_location(unsigned int program, const char *name)
{
	int loc = glGetUniformLocation(program, name);
	if (loc < 0) {
		char buf[256];
		const char *alt = GPU_glsl_translated_name(name, buf, sizeof(buf));
		if (alt != name) {
			loc = glGetUniformLocation(program, alt);
		}
	}
	return loc;
}

int GPU_glsl_attrib_location(unsigned int program, const char *name)
{
	int loc = glGetAttribLocation(program, name);
	if (loc < 0) {
		char buf[256];
		const char *alt = GPU_glsl_translated_name(name, buf, sizeof(buf));
		if (alt != name) {
			loc = glGetAttribLocation(program, alt);
		}
	}
	return loc;
}
