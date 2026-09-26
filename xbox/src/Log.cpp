// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "pch.h"
#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

namespace xport::log
{

namespace
{
std::mutex g_mutex;
FILE *g_file = nullptr;
std::wstring g_path;
std::deque<std::string> g_recent;
constexpr size_t kRecentMax = 64;
ULONGLONG g_start_ms = 0;
} // namespace

void Init()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if(g_file)
		return;
	g_start_ms = GetTickCount64();
	try
	{
		g_path = std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path()) + L"\\xport.log";
		if(_wfopen_s(&g_file, g_path.c_str(), L"w") != 0)
			g_file = nullptr;
	}
	catch(...)
	{
		g_file = nullptr;
	}
}

void Shutdown()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if(g_file)
	{
		fclose(g_file);
		g_file = nullptr;
	}
}

void Write(const char *fmt, ...)
{
	char msg[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);

	ULONGLONG t = GetTickCount64() - g_start_ms;
	char line[1100];
	snprintf(line, sizeof(line), "[%5llu.%03llu] %s", t / 1000, t % 1000, msg);

	std::lock_guard<std::mutex> lock(g_mutex);
	OutputDebugStringA(line);
	OutputDebugStringA("\n");
	if(g_file)
	{
		fputs(line, g_file);
		fputc('\n', g_file);
		fflush(g_file);
	}
	g_recent.emplace_back(line);
	while(g_recent.size() > kRecentMax)
		g_recent.pop_front();
}

std::wstring FilePath()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_path;
}

std::vector<std::string> Recent(size_t max_lines)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	size_t n = g_recent.size() < max_lines ? g_recent.size() : max_lines;
	return std::vector<std::string>(g_recent.end() - (ptrdiff_t)n, g_recent.end());
}

} // namespace xport::log
