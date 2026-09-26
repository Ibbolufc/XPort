// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include <string>
#include <vector>

namespace xport::log
{

// Opens <LocalFolder>\xport.log (truncated on every launch) and mirrors every line to
// OutputDebugString, so logs are readable both from the Device Portal file explorer
// (LocalAppData\<package>\LocalState\xport.log) and from an attached debugger.
void Init();
void Shutdown();

void Write(const char *fmt, ...);

// Full path of the log file (empty before Init()).
std::wstring FilePath();

// The most recent lines, oldest first, for the on-screen log panel.
std::vector<std::string> Recent(size_t max_lines);

} // namespace xport::log

#define XLOG(...) ::xport::log::Write(__VA_ARGS__)
