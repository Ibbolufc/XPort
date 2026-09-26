// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Portable replacement for <strings.h>, which the MSVC/UCRT headers (used by
// both cl.exe and clang-cl, and therefore by UWP/Xbox builds) do not provide.

#ifndef CHIAKI_COMPAT_STRINGS_H
#define CHIAKI_COMPAT_STRINGS_H

#ifdef _MSC_VER
#include <string.h>
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
#else
#include <strings.h>
#endif

#endif // CHIAKI_COMPAT_STRINGS_H
