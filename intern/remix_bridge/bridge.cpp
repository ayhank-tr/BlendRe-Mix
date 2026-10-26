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

/** \file bridge.cpp
 *  \ingroup remix_bridge
 *
 * BGE-Remix bridge: shared state, log and status.
 */

#include <cstdarg>
#include <cstdio>

#include "bridge_intern.h"

namespace brb {

brb_status g_status = BRB_STATUS_NOT_LOADED;
std::string g_text = "RTX Remix has not been loaded.";
static std::string g_log_path;

remixapi_Interface g_remix = {};
remixapi_HMODULE g_dll = nullptr;
bool g_started = false;
HWND g_hwnd = nullptr;
std::wstring g_runtime_dir;
HMODULE g_impl = nullptr;

std::string utf8(const std::wstring &w)
{
	if (w.empty()) {
		return std::string();
	}
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
	return s;
}

const std::string &log_path()
{
	if (g_log_path.empty()) {
		wchar_t tmp[MAX_PATH + 1] = {0};
		DWORD n = GetTempPathW(MAX_PATH, tmp);
		std::wstring p = (n > 0) ? std::wstring(tmp) : std::wstring(L".\\");
		g_log_path = utf8(p + L"bge-remix.log");
	}
	return g_log_path;
}

void log(const char *fmt, ...)
{
	char msg[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);

	SYSTEMTIME t;
	GetLocalTime(&t);
	std::wstring wpath;
	{
		const std::string &p = log_path();
		int n = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
		wpath.resize(n);
		MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &wpath[0], n);
	}
	FILE *f = _wfopen(wpath.c_str(), L"ab");
	if (f) {
		fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d  %s\r\n",
		        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, msg);
		fclose(f);
	}
}

void set_status(brb_status status, const std::string &text)
{
	g_status = status;
	g_text = text;
	log("status %d: %s", (int)status, text.c_str());
}

bool file_exists(const std::wstring &path)
{
	DWORD a = GetFileAttributesW(path.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

}  /* namespace brb */

using namespace brb;

extern "C" {

BRB_API int brb_abi_version(void)
{
	return BRB_ABI_VERSION;
}

BRB_API const char *brb_api_version(void)
{
	static char buf[32];
	snprintf(buf, sizeof(buf), "%d.%d.%d",
	         REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);
	return buf;
}

BRB_API brb_status brb_get_status(void)
{
	return g_status;
}

BRB_API const char *brb_status_text(void)
{
	return g_text.c_str();
}

BRB_API const char *brb_log_path(void)
{
	return log_path().c_str();
}

}  /* extern "C" */
