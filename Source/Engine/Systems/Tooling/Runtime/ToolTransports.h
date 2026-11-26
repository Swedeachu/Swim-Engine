#pragma once

#include "Engine/Systems/Tooling/ToolServer.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace Swim::Tooling
{

	struct HttpToolTransportDesc
	{
		std::uint16_t Port = 0;		  // 0: any free port.
		std::string Path = "/mcp";	  // The single MCP endpoint.
		std::string Token;			  // Bearer token; empty: a random one per start.
		double RequestTimeout = 30.0; // Seconds to wait for the main thread's Pump.
	};

	// MCP "Streamable HTTP" on 127.0.0.1 only. One endpoint: POST takes a JSON-RPC
	// message and answers with application/json (no SSE stream; GET is 405), DELETE
	// ends the session. initialize creates the session and returns Mcp-Session-Id;
	// later requests must carry it. Every request needs "Authorization: Bearer
	// <token>", a loopback peer, a localhost Host header and, when present, a
	// localhost Origin (DNS rebinding protection). Requests are executed by
	// ToolServer::Pump on the thread that pumps it.
	class HttpToolTransport
	{

	  public:
		HttpToolTransport();

		~HttpToolTransport();

		HttpToolTransport(const HttpToolTransport&) = delete;

		HttpToolTransport& operator=(const HttpToolTransport&) = delete;

		// False (with the reason in error) when the socket cannot be bound.
		bool Start(ToolServer& server, HttpToolTransportDesc desc = {}, std::string* error = nullptr);

		void Stop();

		bool IsRunning() const { return running; }

		std::uint16_t GetPort() const { return port; }

		std::string GetUrl() const; // http://127.0.0.1:<port><path>

		const std::string& GetToken() const { return desc.Token; }

		// Writes { "url", "token", "headers": { "Authorization" } } for an agent's config.
		bool WriteConnectionFile(const std::filesystem::path& path) const;

	  private:
		struct State;

		void AcceptLoop();

		void Serve(std::intptr_t socket);

		std::unique_ptr<State> state;
		ToolServer* server = nullptr;
		HttpToolTransportDesc desc;
		std::atomic<bool> running{ false };
		std::uint16_t port = 0;
		std::thread acceptThread;
	};

	// Line-delimited JSON-RPC on stdin/stdout (the MCP stdio transport), for a process
	// an agent launches itself. Stop cannot interrupt a blocked read; the reader thread
	// is detached and exits at end of input.
	class StdioToolTransport
	{

	  public:
		~StdioToolTransport();

		void Start(ToolServer& server);

		void Stop();

		bool IsRunning() const { return running->load(); }

	  private:
		std::shared_ptr<std::atomic<bool>> running = std::make_shared<std::atomic<bool>>(false);
	};

	// 32 random bytes as hex.
	std::string MakeToolToken();

} // namespace Swim::Tooling
