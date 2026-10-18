// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Entropy source for mbedTLS on Windows, including UWP/Xbox.
//
// mbedTLS 2.x gathers platform entropy on Windows with CryptAcquireContext/CryptGenRandom,
// which are not available to UWP apps. Windows builds therefore compile mbedTLS with
// MBEDTLS_NO_PLATFORM_ENTROPY + MBEDTLS_ENTROPY_HARDWARE_ALT
// (third-party/mbedtls-config/xport_mbedtls_windows.h),
// and this file provides the hardware entropy hook on top of BCryptGenRandom, which is
// available to both desktop and UWP apps. mbedTLS registers it as a strong source.

#if defined(_WIN32) && defined(CHIAKI_LIB_ENABLE_MBEDTLS)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#include <limits.h>
#include <stddef.h>

#ifndef BCRYPT_SUCCESS
#define BCRYPT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

// Declared by <mbedtls/entropy_poll.h> when MBEDTLS_ENTROPY_HARDWARE_ALT is defined.
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
	(void)data;
	*olen = 0;
	if(len > ULONG_MAX)
		len = ULONG_MAX;
	if(!BCRYPT_SUCCESS(BCryptGenRandom(NULL, output, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
		return -1;
	*olen = len;
	return 0;
}

#endif
