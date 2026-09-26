// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "pch.h"
#include "NetProbe.h"
#include "Log.h"

#include <cstring>
#include <sstream>

namespace xport
{

namespace
{
// Keep in sync with lib/include/chiaki/discovery.h
constexpr uint16_t kPortPS4 = 987;
constexpr const char *kProtoPS4 = "00020020";
constexpr uint16_t kPortPS5 = 9302;
constexpr const char *kProtoPS5 = "00030010";

constexpr int kProbeDurationMs = 4000;
constexpr int kResendIntervalMs = 1000;

std::string Trim(const std::string &s)
{
	size_t b = s.find_first_not_of(" \t\r\n");
	size_t e = s.find_last_not_of(" \t\r\n");
	return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}
} // namespace

NetProbe::NetProbe()
{
	WSADATA data;
	wsa_ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
	if(!wsa_ok)
		XLOG("NetProbe: WSAStartup failed");
	status = "Idle";
}

NetProbe::~NetProbe()
{
	if(thread.joinable())
		thread.join();
	if(wsa_ok)
		WSACleanup();
}

void NetProbe::Start()
{
	if(running.exchange(true))
		return;
	if(thread.joinable())
		thread.join();
	{
		std::lock_guard<std::mutex> lock(mutex);
		hosts.clear();
	}
	thread = std::thread([this]() { Run(); running = false; });
}

std::string NetProbe::Status() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return status;
}

std::vector<NetProbe::Host> NetProbe::Hosts() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return hosts;
}

void NetProbe::SetStatus(const std::string &s)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		status = s;
	}
	XLOG("NetProbe: %s", s.c_str());
}

void NetProbe::AddHost(Host host)
{
	std::lock_guard<std::mutex> lock(mutex);
	for(auto &h : hosts)
	{
		if(h.address == host.address)
		{
			h = std::move(host);
			return;
		}
	}
	XLOG("NetProbe: found %s \"%s\" at %s (%s)", host.type.c_str(), host.name.c_str(), host.address.c_str(), host.state.c_str());
	hosts.push_back(std::move(host));
}

void NetProbe::Run()
{
	if(!wsa_ok)
	{
		SetStatus("Winsock unavailable");
		return;
	}

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(sock == INVALID_SOCKET)
	{
		SetStatus("socket() failed: " + std::to_string(WSAGetLastError()));
		return;
	}

	BOOL broadcast = TRUE;
	if(setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast)) != 0)
		XLOG("NetProbe: SO_BROADCAST failed: %d", WSAGetLastError());

	sockaddr_in local = {};
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = 0;
	if(bind(sock, (sockaddr *)&local, sizeof(local)) != 0)
	{
		SetStatus("bind() failed: " + std::to_string(WSAGetLastError()));
		closesocket(sock);
		return;
	}

	auto send_srch = [&](uint16_t port, const char *proto) {
		char buf[128];
		int len = snprintf(buf, sizeof(buf), "SRCH * HTTP/1.1\ndevice-discovery-protocol-version:%s\n", proto);
		sockaddr_in dst = {};
		dst.sin_family = AF_INET;
		dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
		dst.sin_port = htons(port);
		// The console expects the terminating NUL too (lib sends len + 1).
		if(sendto(sock, buf, len + 1, 0, (sockaddr *)&dst, sizeof(dst)) < 0)
			XLOG("NetProbe: sendto port %u failed: %d", (unsigned)port, WSAGetLastError());
	};

	SetStatus("Searching the local network...");
	ULONGLONG start = GetTickCount64();
	ULONGLONG last_send = 0;
	while(true)
	{
		ULONGLONG now = GetTickCount64();
		if(now - start >= (ULONGLONG)kProbeDurationMs)
			break;
		if(last_send == 0 || now - last_send >= (ULONGLONG)kResendIntervalMs)
		{
			send_srch(kPortPS4, kProtoPS4);
			send_srch(kPortPS5, kProtoPS5);
			last_send = now;
		}

		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		timeval tv = {0, 200 * 1000};
		int r = select(0, &fds, nullptr, nullptr, &tv);
		if(r < 0)
		{
			XLOG("NetProbe: select failed: %d", WSAGetLastError());
			break;
		}
		if(r == 0)
			continue;

		char buf[2048];
		sockaddr_in from = {};
		int from_len = sizeof(from);
		int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (sockaddr *)&from, &from_len);
		if(n <= 0)
			continue;
		buf[n] = '\0';

		char addr[INET_ADDRSTRLEN] = {};
		inet_ntop(AF_INET, &from.sin_addr, addr, sizeof(addr));

		Host host;
		host.address = addr;
		std::istringstream lines(std::string(buf, (size_t)n));
		std::string line;
		bool first = true;
		while(std::getline(lines, line))
		{
			line = Trim(line);
			if(first)
			{
				first = false;
				// "HTTP/1.1 200 Ok" = ready, "HTTP/1.1 620 Server Standby" = standby
				if(line.find(" 200") != std::string::npos)
					host.state = "ready";
				else if(line.find(" 620") != std::string::npos)
					host.state = "standby";
				else
					host.state = line;
				continue;
			}
			size_t colon = line.find(':');
			if(colon == std::string::npos)
				continue;
			std::string key = line.substr(0, colon);
			std::string value = Trim(line.substr(colon + 1));
			if(key == "host-name")
				host.name = value;
			else if(key == "host-type")
				host.type = value;
		}
		if(host.type.empty())
			continue; // not a PlayStation discovery reply (e.g. our own broadcast echo)
		AddHost(std::move(host));
	}
	closesocket(sock);

	size_t count;
	{
		std::lock_guard<std::mutex> lock(mutex);
		count = hosts.size();
	}
	SetStatus(count ? "Done: " + std::to_string(count) + " console(s) found"
		: "Done: no consoles answered (are they on the same network and in rest mode or on?)");
}

} // namespace xport
