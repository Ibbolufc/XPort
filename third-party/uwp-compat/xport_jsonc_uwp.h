// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// XPort: UWP compatibility for json-c, force-included into the json-c target only on UWP builds
// (third-party/CMakeLists.txt, CHIAKI_TARGET_UWP).
//
// json-c seeds its hash tables in random_seed.c with GetVersion + CryptAcquireContextA +
// CryptGenRandom, none of which are available to UWP apps. Map them onto BCryptGenRandom so
// json-c keeps a cryptographically random seed without modifying its sources.

#ifndef XPORT_JSONC_UWP_H
#define XPORT_JSONC_UWP_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h> // HCRYPTPROV, PROV_RSA_FULL, CRYPT_* (types and constants are available to UWP)
#include <bcrypt.h>

static __inline DWORD xport_jsonc_GetVersion(void)
{
	return 0x0A00; // only used to decide whether CRYPT_SILENT is supported (Windows > NT4)
}

static __inline BOOL xport_jsonc_CryptAcquireContextA(HCRYPTPROV *prov, LPCSTR container, LPCSTR provider,
	DWORD prov_type, DWORD flags)
{
	(void)container; (void)provider; (void)prov_type; (void)flags;
	*prov = 0;
	return TRUE;
}

static __inline BOOL xport_jsonc_CryptGenRandom(HCRYPTPROV prov, DWORD len, BYTE *buf)
{
	(void)prov;
	return BCryptGenRandom(NULL, buf, len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}

static __inline BOOL xport_jsonc_CryptReleaseContext(HCRYPTPROV prov, DWORD flags)
{
	(void)prov; (void)flags;
	return TRUE;
}

#define GetVersion xport_jsonc_GetVersion
#define CryptAcquireContextA xport_jsonc_CryptAcquireContextA
#define CryptGenRandom xport_jsonc_CryptGenRandom
#define CryptReleaseContext xport_jsonc_CryptReleaseContext

#endif // XPORT_JSONC_UWP_H
