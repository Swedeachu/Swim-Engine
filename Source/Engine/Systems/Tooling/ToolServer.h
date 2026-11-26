#pragma once

#include "Engine/Systems/Tooling/Json.h"

#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Tooling
{

	// Thrown by a tool handler for a failure the caller should see as a tool error
	// (isError result) rather than a protocol error.
	struct ToolError : std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};

	struct ToolDesc
	{
		std::string Name;		 // For example "camera.key.set".
		std::string Description; // Shown to the agent; say what it does and what it returns.
		// A JSON Schema object for the arguments. Empty: { "type": "object" } with no properties.
		Json InputSchema;
		std::function<Json(const Json& arguments)> Handler;
		// Instead of Handler, for results that are ready only later (a frame capture written
		// frames from now): call reply or fail exactly once, on the pumping thread, from this
		// call or any later main-thread code. Transports wait up to their request timeout.
		std::function<void(const Json& arguments, std::function<void(Json result)> reply, std::function<void(std::string error)> fail)>
			AsyncHandler;
	};

	struct ToolServerInfo
	{
		std::string Name = "swim-engine-tools";
		std::string Version = "1.0.0";
		std::string Instructions; // Optional text returned by initialize.
	};

	// An MCP-style tool server: JSON-RPC 2.0 with initialize, ping, tools/list and
	// tools/call. It owns no transport. Transports (Tooling/Runtime/ToolTransports.h)
	// hand it request bodies; tool handlers always run on the thread that calls Pump
	// (normally the main thread at a frame boundary), so they may touch the scene.
	class ToolServer
	{

	  public:
		explicit ToolServer(ToolServerInfo info = {});

		// Replaces a tool with the same name. Main thread, or before any transport starts.
		void Register(ToolDesc tool);

		bool Unregister(std::string_view name);

		std::vector<std::string> ListTools() const;

		// Processes one JSON-RPC message synchronously on this thread. Returns the
		// response body, or nothing for notifications and responses. An asynchronous tool
		// that does not reply during the call answers with an error; use Enqueue for those.
		std::optional<std::string> Handle(std::string_view body);

		std::optional<Json> HandleMessage(const Json& message);

		// Processes one message; done receives the response (or nothing) once it exists,
		// which for asynchronous tools may be after this returns.
		void HandleAsync(std::string_view body, std::function<void(std::optional<std::string>)> done);

		// Any thread: queues a body for the next Pump and returns its response.
		std::future<std::optional<std::string>> Enqueue(std::string body);

		// Runs queued messages. Returns how many ran.
		std::size_t Pump();

		// Calls a tool directly (in-process scripting, tests). Throws ToolError, also for an
		// asynchronous tool that does not reply during the call.
		Json Call(std::string_view name, const Json& arguments = Json::MakeObject());

		// Called after every successful tools/call (for example to mark an edit project dirty).
		void SetCallObserver(std::function<void(std::string_view tool, const Json& arguments)> observer);

		const ToolServerInfo& GetInfo() const { return info; }

		// Protocol versions this server speaks, newest first.
		static const std::vector<std::string>& SupportedProtocolVersions();

	  private:
		struct Pending
		{
			std::string Body;
			std::shared_ptr<std::promise<std::optional<std::string>>> Reply;
		};

		const ToolDesc* FindTool(std::string_view name) const;

		void Dispatch(const Json& message, std::function<void(std::optional<Json>)> done);

		void CallToolAsync(const Json& params, std::function<void(Json result)> done);

		ToolServerInfo info;
		std::vector<ToolDesc> tools;
		std::function<void(std::string_view, const Json&)> observer;
		std::mutex queueMutex;
		std::vector<Pending> queue;
	};

	// Schema helpers for tool descriptions.
	namespace Schema
	{

		// { "type": "object", "properties": {...}, "required": [...] }
		Json Object(Json::Object properties = {}, std::vector<std::string> required = {});

		Json Number(std::string_view description);

		Json Integer(std::string_view description);

		Json String(std::string_view description);

		Json Boolean(std::string_view description);

		Json Vector(std::size_t components, std::string_view description);

		Json Enum(std::vector<std::string> values, std::string_view description);

		Json ArrayOf(Json items, std::string_view description);

	} // namespace Schema

} // namespace Swim::Tooling
