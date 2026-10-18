// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "pch.h"
#include "Core.h"
#include "Log.h"

#include <chiaki/common.h>
#include <chiaki/ecdh.h>
#include <chiaki/random.h>
#include <chiaki/session.h>
#include <chiaki/version.h>

#include <cstring>

namespace xport
{

namespace
{
void LibLogCb(ChiakiLogLevel level, const char *msg, void *)
{
	XLOG("[lib %c] %s", chiaki_log_level_char(level), msg);
}

#ifdef CHIAKI_LIB_ENABLE_MBEDTLS
constexpr const char *kCryptoBackend = "mbedTLS";
#else
constexpr const char *kCryptoBackend = "OpenSSL";
#endif
} // namespace

void Core::Add(const char *name, bool passed, const std::string &detail)
{
	checks.push_back({name, passed, detail});
	XLOG("Core self-test: %-10s %s  %s", name, passed ? "PASS" : "FAIL", detail.c_str());
}

void Core::Init()
{
	if(initialized)
		return;
	initialized = true;
	chiaki_log_init(&log, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE, LibLogCb, nullptr);
	XLOG("Remote Play library %s (crypto: %s)", CHIAKI_VERSION, kCryptoBackend);

	// 1. lib init (Winsock startup)
	ChiakiErrorCode err = chiaki_lib_init();
	Add("lib init", err == CHIAKI_ERR_SUCCESS, chiaki_error_string(err));

	// 2. The app and the lib must agree on ChiakiSession's layout (see session.h).
	size_t app_size = sizeof(ChiakiSession);
	size_t lib_size = chiaki_session_sizeof();
	Add("ABI", app_size == lib_size,
		"sizeof(ChiakiSession) app=" + std::to_string(app_size) + " lib=" + std::to_string(lib_size));

	// 3. Cryptographic randomness (mbedTLS entropy via BCryptGenRandom on Windows/Xbox)
	uint8_t buf[32] = {};
	err = chiaki_random_bytes_crypt(buf, sizeof(buf));
	bool nonzero = false;
	for(uint8_t b : buf)
		nonzero |= b != 0;
	Add("entropy", err == CHIAKI_ERR_SUCCESS && nonzero, chiaki_error_string(err));

	// 4. secp256k1 ECDH key generation + signed public key (Remote Play session handshake)
	ChiakiECDH ecdh;
	err = chiaki_ecdh_init(&ecdh);
	if(err == CHIAKI_ERR_SUCCESS)
	{
		uint8_t handshake_key[16] = {};
		uint8_t pub[128];
		size_t pub_size = sizeof(pub);
		uint8_t sig[32];
		size_t sig_size = sizeof(sig);
		err = chiaki_ecdh_get_local_pub_key(&ecdh, pub, &pub_size, handshake_key, sig, &sig_size);
		chiaki_ecdh_fini(&ecdh);
		Add("ECDH", err == CHIAKI_ERR_SUCCESS && pub_size == 65 && sig_size == 32,
			std::string(chiaki_error_string(err)) + ", public key " + std::to_string(pub_size) + " bytes");
	}
	else
		Add("ECDH", false, std::string("init: ") + chiaki_error_string(err));

	ok = true;
	for(const auto &c : checks)
		ok &= c.ok;
	XLOG("Core self-test %s", ok ? "passed" : "FAILED");
}

std::string Core::Summary() const
{
	size_t passed = 0;
	for(const auto &c : checks)
		passed += c.ok ? 1 : 0;
	return std::string("Remote Play library ") + CHIAKI_VERSION + " · " + kCryptoBackend + " · self-test " +
		(ok ? "passed" : "FAILED") + " (" + std::to_string(passed) + "/" + std::to_string(checks.size()) + ")";
}

} // namespace xport
