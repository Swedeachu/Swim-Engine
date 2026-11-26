#include "Engine/Systems/Tooling/Runtime/ToolTransports.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <unordered_set>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace Swim::Tooling
{

	namespace
	{

#if defined(_WIN32)
		using NativeSocket = SOCKET;
		constexpr NativeSocket InvalidNativeSocket = INVALID_SOCKET;

		void CloseSocket(NativeSocket s)
		{
			closesocket(s);
		}

		struct NetworkInit
		{
			NetworkInit()
			{
				WSADATA data;
				ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
			}

			~NetworkInit()
			{
				if (ok)
				{
					WSACleanup();
				}
			}

			bool ok = false;
		};
#else
		using NativeSocket = int;
		constexpr NativeSocket InvalidNativeSocket = -1;

		void CloseSocket(NativeSocket s)
		{
			close(s);
		}

		struct NetworkInit
		{
			bool ok = true;
		};
#endif

		constexpr std::size_t MaxHeaderBytes = 64 * 1024;
		constexpr std::size_t MaxBodyBytes = 16 * 1024 * 1024;

		std::string Lower(std::string_view s)
		{
			std::string out(s);
			std::transform(out.begin(), out.end(), out.begin(),
				[](unsigned char c)
				{
					return static_cast<char>(std::tolower(c));
				});
			return out;
		}

		std::string_view Trim(std::string_view s)
		{
			while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
			{
				s.remove_prefix(1);
			}

			while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
			{
				s.remove_suffix(1);
			}

			return s;
		}

		// "localhost", "127.0.0.1" or "[::1]", with an optional port.
		bool IsLocalHostName(std::string_view host)
		{
			if (host.starts_with("[::1]"))
			{
				host.remove_prefix(5);
			}
			else
			{
				const std::size_t colon = host.find(':');
				const std::string name = Lower(host.substr(0, colon));

				if (name != "localhost" && name != "127.0.0.1")
				{
					return false;
				}

				host.remove_prefix(colon == std::string_view::npos ? host.size() : colon);
			}

			if (host.empty())
			{
				return true;
			}

			if (host.front() != ':' || host.size() == 1)
			{
				return false;
			}

			return std::all_of(host.begin() + 1, host.end(),
				[](char c)
				{
					return std::isdigit(static_cast<unsigned char>(c)) != 0;
				});
		}

		bool IsLocalOrigin(std::string_view origin)
		{
			for (const std::string_view scheme : { "http://", "https://" })
			{
				if (origin.starts_with(scheme))
				{
					return IsLocalHostName(origin.substr(scheme.size()));
				}
			}

			return false;
		}

		bool ConstantTimeEqual(std::string_view a, std::string_view b)
		{
			if (a.size() != b.size())
			{
				return false;
			}

			unsigned char diff = 0;

			for (std::size_t i = 0; i < a.size(); ++i)
			{
				diff |= static_cast<unsigned char>(a[i] ^ b[i]);
			}

			return diff == 0;
		}

		struct Request
		{
			std::string Method;
			std::string Path;
			std::map<std::string, std::string> Headers; // Lower-case names.
			std::string Body;

			const std::string* Header(const char* name) const
			{
				const auto found = Headers.find(name);
				return found == Headers.end() ? nullptr : &found->second;
			}
		};

		bool SendAll(NativeSocket s, std::string_view data)
		{
			while (!data.empty())
			{
				const int sent = send(s, data.data(), static_cast<int>(std::min<std::size_t>(data.size(), 1 << 20)), 0);

				if (sent <= 0)
				{
					return false;
				}

				data.remove_prefix(static_cast<std::size_t>(sent));
			}

			return true;
		}

		const char* StatusText(int status)
		{
			switch (status)
			{
			case 200:
				return "OK";
			case 202:
				return "Accepted";
			case 400:
				return "Bad Request";
			case 401:
				return "Unauthorized";
			case 403:
				return "Forbidden";
			case 404:
				return "Not Found";
			case 405:
				return "Method Not Allowed";
			case 411:
				return "Length Required";
			case 413:
				return "Payload Too Large";
			case 503:
				return "Service Unavailable";
			case 504:
				return "Gateway Timeout";
			default:
				return "Error";
			}
		}

		bool Respond(NativeSocket s, int status, std::string_view body,
			const std::vector<std::pair<std::string, std::string>>& headers = {}, bool close = false)
		{
			std::string out = "HTTP/1.1 " + std::to_string(status) + " " + StatusText(status) + "\r\n";

			if (!body.empty())
			{
				out += "Content-Type: application/json\r\n";
			}

			out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
			out += "Cache-Control: no-store\r\n";

			for (const auto& [name, value] : headers)
			{
				out += name + ": " + value + "\r\n";
			}

			if (close)
			{
				out += "Connection: close\r\n";
			}

			out += "\r\n";
			out += body;
			return SendAll(s, out);
		}

		std::string JsonError(std::string_view message)
		{
			Json error = Json::MakeObject();
			error["jsonrpc"] = "2.0";
			error["id"] = Json();
			error["error"]["code"] = -32600;
			error["error"]["message"] = message;
			return error.Dump();
		}

		// Reads one request. Returns false on a closed connection, timeout or bad framing
		// (status is set when an error response should still be sent).
		bool ReadRequest(NativeSocket s, std::string& buffer, Request& request, int& status)
		{
			status = 0;
			std::size_t headerEnd;

			while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos)
			{
				if (buffer.size() > MaxHeaderBytes)
				{
					status = 413;
					return false;
				}

				char chunk[8192];
				const int got = recv(s, chunk, sizeof(chunk), 0);

				if (got <= 0)
				{
					return false;
				}

				buffer.append(chunk, static_cast<std::size_t>(got));
			}

			const std::string_view head(buffer.data(), headerEnd);
			std::size_t lineEnd = head.find("\r\n");
			const std::string_view requestLine = head.substr(0, lineEnd);
			const std::size_t firstSpace = requestLine.find(' ');
			const std::size_t secondSpace = requestLine.find(' ', firstSpace + 1);

			if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos)
			{
				status = 400;
				return false;
			}

			request = {};
			request.Method = std::string(requestLine.substr(0, firstSpace));
			request.Path = std::string(requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1));
			request.Path = request.Path.substr(0, request.Path.find('?'));

			while (lineEnd != std::string_view::npos && lineEnd < head.size())
			{
				const std::size_t next = head.find("\r\n", lineEnd + 2);
				const std::string_view line =
					head.substr(lineEnd + 2, next == std::string_view::npos ? std::string_view::npos : next - lineEnd - 2);
				const std::size_t colon = line.find(':');

				if (colon != std::string_view::npos)
				{
					request.Headers[Lower(Trim(line.substr(0, colon)))] = std::string(Trim(line.substr(colon + 1)));
				}

				lineEnd = next;
			}

			if (request.Header("transfer-encoding"))
			{
				status = 411;
				return false;
			}

			std::size_t length = 0;

			if (const auto* contentLength = request.Header("content-length"))
			{
				try
				{
					length = static_cast<std::size_t>(std::stoull(*contentLength));
				}
				catch (...)
				{
					status = 400;
					return false;
				}
			}

			if (length > MaxBodyBytes)
			{
				status = 413;
				return false;
			}

			buffer.erase(0, headerEnd + 4);

			while (buffer.size() < length)
			{
				char chunk[65536];
				const int got = recv(s, chunk, sizeof(chunk), 0);

				if (got <= 0)
				{
					return false;
				}

				buffer.append(chunk, static_cast<std::size_t>(got));
			}

			request.Body = buffer.substr(0, length);
			buffer.erase(0, length);
			return true;
		}

	} // namespace

	std::string MakeToolToken()
	{
		std::random_device device;
		std::string token;
		const char* digits = "0123456789abcdef";

		for (int i = 0; i < 32; ++i)
		{
			const unsigned value = device() & 0xFFu;
			token += digits[value >> 4];
			token += digits[value & 15];
		}

		return token;
	}

	struct HttpToolTransport::State
	{
		NetworkInit Network;
		NativeSocket Listener = InvalidNativeSocket;
		std::mutex Mutex;
		std::unordered_set<std::string> Sessions;
		std::vector<NativeSocket> Clients;
		std::vector<std::thread> Workers;
	};

	HttpToolTransport::HttpToolTransport() = default;

	HttpToolTransport::~HttpToolTransport()
	{
		Stop();
	}

	bool HttpToolTransport::Start(ToolServer& toolServer, HttpToolTransportDesc transportDesc, std::string* error)
	{
		Stop();
		state = std::make_unique<State>();
		server = &toolServer;
		desc = std::move(transportDesc);

		if (desc.Token.empty())
		{
			desc.Token = MakeToolToken();
		}

		if (desc.Path.empty() || desc.Path.front() != '/')
		{
			desc.Path = "/" + desc.Path;
		}

		auto fail = [&](const char* what)
		{
			if (error)
			{
				*error = what;
			}

			if (state->Listener != InvalidNativeSocket)
			{
				CloseSocket(state->Listener);
			}

			state.reset();
			return false;
		};

		if (!state->Network.ok)
		{
			return fail("socket library failed to start");
		}

		state->Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if (state->Listener == InvalidNativeSocket)
		{
			return fail("socket() failed");
		}

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Never another interface.
		address.sin_port = htons(desc.Port);

		if (bind(state->Listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
		{
			return fail("bind to 127.0.0.1 failed (port in use?)");
		}

		if (listen(state->Listener, 8) != 0)
		{
			return fail("listen() failed");
		}

		socklen_t length = sizeof(address);
		getsockname(state->Listener, reinterpret_cast<sockaddr*>(&address), &length);
		port = ntohs(address.sin_port);
		running = true;
		acceptThread = std::thread(
			[this]
			{
				AcceptLoop();
			});
		return true;
	}

	void HttpToolTransport::Stop()
	{
		if (!state)
		{
			return;
		}

		running = false;

		if (state->Listener != InvalidNativeSocket)
		{
#if defined(_WIN32)
			shutdown(state->Listener, SD_BOTH);
#else
			shutdown(state->Listener, SHUT_RDWR);
#endif
			CloseSocket(state->Listener);
			state->Listener = InvalidNativeSocket;
		}

		if (acceptThread.joinable())
		{
			acceptThread.join();
		}

		std::vector<std::thread> workers;

		{
			std::lock_guard lock(state->Mutex);

			for (const NativeSocket client : state->Clients)
			{
#if defined(_WIN32)
				shutdown(client, SD_BOTH);
#else
				shutdown(client, SHUT_RDWR);
#endif
			}

			workers.swap(state->Workers);
		}

		for (auto& worker : workers)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}

		state.reset();
		port = 0;
	}

	std::string HttpToolTransport::GetUrl() const
	{
		return "http://127.0.0.1:" + std::to_string(port) + desc.Path;
	}

	bool HttpToolTransport::WriteConnectionFile(const std::filesystem::path& path) const
	{
		if (!running)
		{
			return false;
		}

		Json file = Json::MakeObject();
		file["type"] = "http";
		file["url"] = GetUrl();
		file["token"] = desc.Token;
		file["headers"]["Authorization"] = "Bearer " + desc.Token;
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << file.Dump(2) << '\n';
		return static_cast<bool>(out);
	}

	void HttpToolTransport::AcceptLoop()
	{
		while (running)
		{
			sockaddr_in peer{};
			socklen_t length = sizeof(peer);
			const NativeSocket client = accept(state->Listener, reinterpret_cast<sockaddr*>(&peer), &length);

			if (client == InvalidNativeSocket)
			{
				if (!running)
				{
					break;
				}

				continue;
			}

			// Loopback peers only (127.0.0.0/8).
			if (peer.sin_family != AF_INET || (ntohl(peer.sin_addr.s_addr) >> 24) != 127)
			{
				CloseSocket(client);
				continue;
			}

			std::lock_guard lock(state->Mutex);
			state->Clients.push_back(client);
			state->Workers.emplace_back(
				[this, client]
				{
					Serve(static_cast<std::intptr_t>(client));
				});
		}
	}

	void HttpToolTransport::Serve(std::intptr_t handle)
	{
		const auto client = static_cast<NativeSocket>(handle);
#if defined(_WIN32)
		const DWORD timeout = 60000;
		setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
		timeval timeout{ 60, 0 };
		setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
		std::string buffer;

		while (running)
		{
			Request request;
			int status = 0;

			if (!ReadRequest(client, buffer, request, status))
			{
				if (status)
				{
					Respond(client, status, JsonError(StatusText(status)), {}, true);
				}

				break;
			}

			const auto* host = request.Header("host");
			const auto* origin = request.Header("origin");

			if (!host || !IsLocalHostName(*host) || (origin && !IsLocalOrigin(*origin)))
			{
				Respond(client, 403, JsonError("forbidden host or origin"), {}, true);
				break;
			}

			if (request.Path != desc.Path)
			{
				Respond(client, 404, JsonError("not found"));
				continue;
			}

			const auto* authorization = request.Header("authorization");
			const std::string expected = "Bearer " + desc.Token;

			if (!authorization || !ConstantTimeEqual(*authorization, expected))
			{
				Respond(client, 401, JsonError("missing or wrong bearer token"), { { "WWW-Authenticate", "Bearer" } });
				continue;
			}

			const auto* sessionHeader = request.Header("mcp-session-id");

			if (request.Method == "DELETE")
			{
				std::lock_guard lock(state->Mutex);
				const bool known = sessionHeader && state->Sessions.erase(*sessionHeader) > 0;
				Respond(client, known ? 200 : 404, "");
				continue;
			}

			if (request.Method != "POST")
			{
				Respond(client, 405, "", { { "Allow", "POST, DELETE" } });
				continue;
			}

			const auto message = Json::TryParse(request.Body);
			const bool initialize = message && message->IsObject() && (*message)["method"].AsString() == "initialize";
			std::string session;

			if (initialize)
			{
				session = MakeToolToken();
				std::lock_guard lock(state->Mutex);
				state->Sessions.insert(session);
			}
			else
			{
				if (!sessionHeader)
				{
					Respond(client, 400, JsonError("missing Mcp-Session-Id"));
					continue;
				}

				std::lock_guard lock(state->Mutex);

				if (!state->Sessions.contains(*sessionHeader))
				{
					Respond(client, 404, JsonError("unknown session"));
					continue;
				}

				session = *sessionHeader;
			}

			auto reply = server->Enqueue(request.Body);
			const auto wait = std::chrono::duration<double>(desc.RequestTimeout);

			if (reply.wait_for(wait) != std::future_status::ready)
			{
				Respond(client, 504, JsonError("the application did not process the request in time"));
				continue;
			}

			std::optional<std::string> body;

			try
			{
				body = reply.get();
			}
			catch (...)
			{
				Respond(client, 503, JsonError("request failed"));
				continue;
			}

			const std::vector<std::pair<std::string, std::string>> headers{ { "Mcp-Session-Id", session } };

			if (!body)
			{
				Respond(client, 202, "", headers);
			}
			else
			{
				Respond(client, 200, *body, headers);
			}
		}

		std::lock_guard lock(state->Mutex);
		CloseSocket(client);
		std::erase(state->Clients, client);
	}

	StdioToolTransport::~StdioToolTransport()
	{
		Stop();
	}

	void StdioToolTransport::Start(ToolServer& server)
	{
		if (running->exchange(true))
		{
			return;
		}

		std::thread(
			[flag = running, target = &server]
			{
				std::string line;

				while (flag->load() && std::getline(std::cin, line))
				{
					if (Trim(line).empty())
					{
						continue;
					}

					auto reply = target->Enqueue(line);
					std::optional<std::string> body;

					try
					{
						body = reply.get();
					}
					catch (...)
					{
						body = JsonError("request failed");
					}

					if (body && flag->load())
					{
						std::cout << *body << "\n" << std::flush;
					}
				}

				flag->store(false);
			})
			.detach();
	}

	void StdioToolTransport::Stop()
	{
		running->store(false);
	}

} // namespace Swim::Tooling
