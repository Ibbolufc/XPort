// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace xport
{

// Milestone-1 network spike: sends the PlayStation LAN discovery request (the same
// "SRCH" packet lib/src/discovery.c sends, to UDP 987 for PS4 and 9302 for PS5) as a
// broadcast and collects the replies. Its only purpose is to prove that UDP broadcast
// send/receive works for a UWP app on Xbox before the real lib discovery is wired in
// (Phase 3), so it deliberately does not depend on lib/.
class NetProbe
{
public:
	struct Host
	{
		std::string address;
		std::string name;
		std::string type;   // "PS4" / "PS5"
		std::string state;  // "ready" / "standby" / raw status line
	};

	NetProbe();
	~NetProbe();

	void Start();
	bool Running() const { return running.load(); }
	std::string Status() const;
	std::vector<Host> Hosts() const;

private:
	void Run();
	void SetStatus(const std::string &s);
	void AddHost(Host host);

	bool wsa_ok = false;
	std::atomic<bool> running{false};
	std::thread thread;
	mutable std::mutex mutex;
	std::string status;
	std::vector<Host> hosts;
};

} // namespace xport
