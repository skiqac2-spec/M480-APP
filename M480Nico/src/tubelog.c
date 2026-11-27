/* M480Nico - small log file next to the exe (for troubleshooting) */
#include "util.h"
#include "tubelog.h"
#include <stdio.h>
#include <stdarg.h>

static CRITICAL_SECTION g_lcs;
static int g_linit;
static long g_lsize;

void tube_log_init(void)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	InitializeCriticalSection(&g_lcs);
	g_linit = 1;
	app_path(p, L"m480nico_log.txt");
	f = _wfopen(p, L"wb");
	if (f) { fputs("M480Nico log\r\n", f); fclose(f); }
}

void tube_log(const char *fmt, ...)
{
	char line[512];
	va_list ap;
	WCHAR p[MAX_PATH];
	FILE *f;
	int n;
	if (!g_linit) return;
	n = _snprintf(line, 20, "[%lu] ", (unsigned long)(GetTickCount() / 100));
	if (n < 0) n = 0;
	va_start(ap, fmt);
	_vsnprintf(line + n, sizeof line - n - 3, fmt, ap);
	va_end(ap);
	line[sizeof line - 3] = 0;
	strcat(line, "\r\n");
	EnterCriticalSection(&g_lcs);
	if (g_lsize < 200000) {
		app_path(p, L"m480nico_log.txt");
		f = _wfopen(p, L"ab");
		if (f) { fputs(line, f); fclose(f); g_lsize += (long)strlen(line); }
	}
	LeaveCriticalSection(&g_lcs);
}
