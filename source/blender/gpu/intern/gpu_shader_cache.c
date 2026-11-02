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

/** \file blender/gpu/intern/gpu_shader_cache.c
 *  \ingroup gpu
 *
 * BGE-Remix P8.3: compiled shader programs kept on disk, so a second start
 * skips building them. A program is stored under a key made from its full
 * source text, the graphics card's name and the driver's version, so a
 * driver update or any change to a shader builds it again.
 *
 * Folder: BGE_REMIX_SHADER_CACHE=<folder> (or "off"), otherwise
 * "shader_cache" next to the user's config folder. At most 256 MB: the
 * oldest files go first when the cache starts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"
#include "BLI_fileops.h"
#include "BLI_fileops_types.h"
#include "BLI_path_util.h"
#include "BLI_string.h"

#include "BKE_appdir.h"

#include "GPU_glew.h"
#include "GPU_shader.h"

#include "gpu_private.h"

#define CACHE_MAGIC 0x43535242u  /* "BRSC" */
#define CACHE_VERSION 1u
#define CACHE_LIMIT ((size_t)256 * 1024 * 1024)

static struct {
	bool init, enabled;
	char dir[FILE_MAX];
	int loaded, built, saved;
} gpu_cache = {false};

static unsigned long long fnv64(unsigned long long h, const char *s)
{
	if (!s) {
		return h;
	}
	for (; *s; s++) {
		h ^= (unsigned char)*s;
		h *= 1099511628211ull;
	}
	return h;
}

unsigned long long gpu_shader_cache_key(const char **parts, int num_parts)
{
	unsigned long long h = 14695981039346656037ull;
	h = fnv64(h, (const char *)glGetString(GL_VENDOR));
	h = fnv64(h, (const char *)glGetString(GL_RENDERER));
	h = fnv64(h, (const char *)glGetString(GL_VERSION));
	for (int i = 0; i < num_parts; i++) {
		h = fnv64(h, parts[i]);
		h = fnv64(h, "\x02");
	}
	return h;
}

static int cmp_mtime(const void *a, const void *b)
{
	const struct direntry *ea = a, *eb = b;
	if (ea->s.st_mtime < eb->s.st_mtime) return -1;
	if (ea->s.st_mtime > eb->s.st_mtime) return 1;
	return 0;
}

/* drops the oldest files until the cache is under its limit */
static void gpu_shader_cache_prune(void)
{
	struct direntry *files = NULL;
	unsigned int n = BLI_filelist_dir_contents(gpu_cache.dir, &files);
	if (!files) {
		return;
	}
	size_t total = 0;
	for (unsigned int i = 0; i < n; i++) {
		if (BLI_testextensie(files[i].relname, ".bin")) {
			total += (size_t)files[i].s.st_size;
		}
	}
	if (total > CACHE_LIMIT) {
		qsort(files, n, sizeof(*files), cmp_mtime);
		for (unsigned int i = 0; i < n && total > CACHE_LIMIT; i++) {
			if (BLI_testextensie(files[i].relname, ".bin")) {
				total -= (size_t)files[i].s.st_size;
				BLI_delete(files[i].path, false, false);
			}
		}
	}
	BLI_filelist_free(files, n);
}

void GPU_shader_cache_init(void)
{
	if (gpu_cache.init) {
		return;
	}
	gpu_cache.init = true;
	gpu_cache.enabled = false;

	if (!GLEW_VERSION_4_1 && !GLEW_ARB_get_program_binary) {
		return;
	}
	GLint formats = 0;
	glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
	if (formats <= 0) {
		printf("BGE-Remix shader cache: the driver offers no program binaries\n");
		return;
	}

	const char *env = getenv("BGE_REMIX_SHADER_CACHE");
	if (env && STREQ(env, "off")) {
		return;
	}
	if (env && env[0]) {
		BLI_strncpy(gpu_cache.dir, env, sizeof(gpu_cache.dir));
	}
	else {
		const char *config = BKE_appdir_folder_id_user_notest(BLENDER_USER_CONFIG, NULL);
		if (!config) {
			return;
		}
		/* next to the config folder, in the version folder */
		char parent[FILE_MAX];
		BLI_strncpy(parent, config, sizeof(parent));
		BLI_del_slash(parent);
		BLI_parent_dir(parent);
		BLI_join_dirfile(gpu_cache.dir, sizeof(gpu_cache.dir), parent, "shader_cache");
	}
	if (!BLI_dir_create_recursive(gpu_cache.dir)) {
		printf("BGE-Remix shader cache: cannot use %s\n", gpu_cache.dir);
		return;
	}
	gpu_cache.enabled = true;
	gpu_shader_cache_prune();
}

void GPU_shader_cache_exit(void)
{
	if (gpu_cache.init) {
		printf("BGE-Remix shader cache: %d loaded, %d built, %d saved%s%s\n",
		       gpu_cache.loaded, gpu_cache.built, gpu_cache.saved,
		       gpu_cache.enabled ? " in " : " (off)", gpu_cache.enabled ? gpu_cache.dir : "");
	}
	gpu_cache.init = false;
}

bool gpu_shader_cache_enabled(void)
{
	return gpu_cache.enabled;
}

void GPU_shader_cache_stats(int *r_loaded, int *r_built, int *r_saved)
{
	*r_loaded = gpu_cache.loaded;
	*r_built = gpu_cache.built;
	*r_saved = gpu_cache.saved;
}

static void cache_path(unsigned long long key, char *r_path)
{
	char name[64];
	BLI_snprintf(name, sizeof(name), "%016llx.bin", key);
	BLI_join_dirfile(r_path, FILE_MAX, gpu_cache.dir, name);
}

bool gpu_shader_cache_load(unsigned int program, unsigned long long key)
{
	if (!gpu_cache.enabled) {
		return false;
	}
	char path[FILE_MAX];
	cache_path(key, path);
	FILE *f = BLI_fopen(path, "rb");
	if (!f) {
		return false;
	}
	unsigned int head[4] = {0, 0, 0, 0};
	bool ok = false;
	if (fread(head, sizeof(head), 1, f) == 1 && head[0] == CACHE_MAGIC && head[1] == CACHE_VERSION &&
	    head[3] > 0 && head[3] < (64u << 20))
	{
		void *data = MEM_mallocN(head[3], "shader binary");
		if (fread(data, head[3], 1, f) == 1) {
			glProgramBinary(program, (GLenum)head[2], data, (GLsizei)head[3]);
			GLint status = 0;
			glGetProgramiv(program, GL_LINK_STATUS, &status);
			ok = status != 0;
		}
		MEM_freeN(data);
	}
	fclose(f);
	if (ok) {
		gpu_cache.loaded++;
	}
	else {
		/* stale (another driver): built again and saved over it */
		BLI_delete(path, false, false);
	}
	return ok;
}

void gpu_shader_cache_save(unsigned int program, unsigned long long key)
{
	gpu_cache.built++;
	if (!gpu_cache.enabled) {
		return;
	}
	GLint length = 0;
	glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &length);
	if (length <= 0) {
		return;
	}
	void *data = MEM_mallocN((size_t)length, "shader binary");
	GLenum format = 0;
	GLsizei written = 0;
	glGetProgramBinary(program, length, &written, &format, data);
	if (written > 0) {
		char path[FILE_MAX], tmp[FILE_MAX];
		cache_path(key, path);
		BLI_snprintf(tmp, sizeof(tmp), "%s.tmp", path);
		FILE *f = BLI_fopen(tmp, "wb");
		if (f) {
			const unsigned int head[4] = {CACHE_MAGIC, CACHE_VERSION, (unsigned int)format, (unsigned int)written};
			const bool ok = fwrite(head, sizeof(head), 1, f) == 1 && fwrite(data, (size_t)written, 1, f) == 1;
			fclose(f);
			if (ok) {
				BLI_delete(path, false, false);
				if (BLI_rename(tmp, path) == 0) {
					gpu_cache.saved++;
				}
			}
			else {
				BLI_delete(tmp, false, false);
			}
		}
	}
	MEM_freeN(data);
}
