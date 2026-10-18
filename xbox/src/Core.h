// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include <chiaki/log.h>

#include <string>
#include <vector>

namespace xport
{

// Owns the shared Remote Play library (lib/) for the app: initialises it, routes its log into
// xport.log, and runs a startup self-test that exercises the parts most likely to break on UWP
// (Winsock init, the struct layout shared with the lib, BCrypt-backed entropy and the
// secp256k1 key exchange used by the Remote Play handshake).
class Core
{
public:
	struct Check
	{
		std::string name;
		bool ok;
		std::string detail;
	};

	void Init();

	bool Ok() const { return ok; }
	const std::vector<Check> &Checks() const { return checks; }
	std::string Summary() const;
	ChiakiLog *Log() { return &log; }

private:
	void Add(const char *name, bool passed, const std::string &detail);

	ChiakiLog log = {};
	bool initialized = false;
	bool ok = false;
	std::vector<Check> checks;
};

} // namespace xport
