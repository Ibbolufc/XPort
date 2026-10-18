// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// XPort: mbedTLS configuration overrides for Windows, including UWP/Xbox.
// Included at the end of mbedtls/config.h via MBEDTLS_USER_CONFIG_FILE (root CMakeLists.txt).

#ifndef XPORT_MBEDTLS_WINDOWS_H
#define XPORT_MBEDTLS_WINDOWS_H

// mbedTLS's Windows entropy source (CryptAcquireContext/CryptGenRandom) is unavailable to UWP
// apps. Use the BCryptGenRandom hook in lib/src/mbedtls_entropy_win32.c instead.
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

// The lib only uses mbedTLS crypto primitives, never file I/O. File I/O pulls in desktop-only
// Win32 calls (x509_crt.c's directory scan uses lstrlenW), so it is disabled, together with
// the PSA key storage that requires it.
#undef MBEDTLS_FS_IO
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C

#endif // XPORT_MBEDTLS_WINDOWS_H
