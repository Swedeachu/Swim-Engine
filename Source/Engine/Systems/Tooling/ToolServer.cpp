#include "Engine/Systems/Tooling/ToolServer.h"

#include <algorithm>
#include <utility>

namespace Swim::Tooling
{

	namespace
	{

		constexpr int ParseError = -32700;
		constexpr int InvalidRequest = -32600;
		constexpr int MethodNotFound = -32601;
		constexpr int InvalidParams = -32602;
		constexpr int InternalError = -32603;

		Json ErrorResponse(const Json& id, int code, std::string message)
		{
			Json response = Json::MakeObject();
			response["jsonrpc"] = "2.0";
			response["id"] = id;
			response["error"]["code"] = code;
			response["error"]["message"] = std::move(message);
			return response;
		}

		Json Result(const Json& id, Json result)
		{
			Json response = Json::MakeObject();
			response["jsonrpc"] = "2.0";
			response["id"] = id;
			response["result"] = std::move(result);
			return response;
		}

		struct InvalidParamsError : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

	} // namespace

	ToolServer::ToolServer(ToolServerInfo info) : info(std::move(info))
	{
	}

	const std::vector<std::string>& ToolServer::SupportedProtocolVersions()
	{
		static const std::vector<std::string> versions{ "2025-06-18", "2025-03-26", "2024-11-05" };
		return versions;
	}

	void ToolServer::Register(ToolDesc tool)
	{
		if (!tool.InputSchema.IsObject())
		{
			tool.InputSchema = Schema::Object();
		}

		if (!tool.InputSchema.Contains("type"))
		{
			tool.InputSchema["type"] = "object";
		}

		const auto found = std::find_if(tools.begin(), tools.end(),
			[&](const ToolDesc& t)
			{
				return t.Name == tool.Name;
			});

		if (found != tools.end())
		{
			*found = std::move(tool);
		}
		else
		{
			tools.push_back(std::move(tool));
		}
	}

	bool ToolServer::Unregister(std::string_view name)
	{
		const auto found = std::find_if(tools.begin(), tools.end(),
			[&](const ToolDesc& t)
			{
				return t.Name == name;
			});

		if (found == tools.end())
		{
			return false;
		}

		tools.erase(found);
		return true;
	}

	std::vector<std::string> ToolServer::ListTools() const
	{
		std::vector<std::string> names;

		for (const auto& tool : tools)
		{
			names.push_back(tool.Name);
		}

		return names;
	}

	void ToolServer::SetCallObserver(std::function<void(std::string_view, const Json&)> callObserver)
	{
		observer = std::move(callObserver);
	}

	const ToolDesc* ToolServer::FindTool(std::string_view name) const
	{
		const auto found = std::find_if(tools.begin(), tools.end(),
			[&](const ToolDesc& t)
			{
				return t.Name == name;
			});
		return found == tools.end() ? nullptr : &*found;
	}

	Json ToolServer::Call(std::string_view name, const Json& arguments)
	{
		const ToolDesc* tool = FindTool(name);

		if (!tool || (!tool->Handler && !tool->AsyncHandler))
		{
			throw ToolError("unknown tool: " + std::string(name));
		}

		const Json args = arguments.IsNull() ? Json::MakeObject() : arguments;
		Json result;

		if (tool->Handler)
		{
			// Copy: a handler may register or unregister tools.
			const auto handler = tool->Handler;
			result = handler(args);
		}
		else
		{
			struct Outcome
			{
				std::optional<Json> Value;
				std::optional<std::string> Error;
			};

			const auto outcome = std::make_shared<Outcome>();
			const auto handler = tool->AsyncHandler;
			handler(
				args,
				[outcome](Json value)
				{
					if (!outcome->Value && !outcome->Error)
					{
						outcome->Value = std::move(value);
					}
				},
				[outcome](std::string error)
				{
					if (!outcome->Value && !outcome->Error)
					{
						outcome->Error = std::move(error);
					}
				});

			if (outcome->Error)
			{
				throw ToolError(*outcome->Error);
			}

			if (!outcome->Value)
			{
				throw ToolError("tool " + std::string(name) + " completes asynchronously; call it through Enqueue or a transport");
			}

			result = std::move(*outcome->Value);
		}

		if (observer)
		{
			observer(name, arguments);
		}

		return result;
	}

	namespace
	{

		Json ToolResult(Json value)
		{
			Json result = Json::MakeObject();
			Json content = Json::MakeArray();
			Json extra;

			// Extra content items (images) travel in "$content" and stay out of the text.
			if (value.IsObject() && value.Contains("$content"))
			{
				extra = value["$content"];
				value.GetObject().erase("$content");
			}

			Json text = Json::MakeObject();
			text["type"] = "text";
			text["text"] = value.IsString() ? value.AsString() : value.Dump();
			content.Push(std::move(text));

			for (const Json& item : extra.GetArray())
			{
				content.Push(item);
			}

			if (value.IsObject())
			{
				result["structuredContent"] = std::move(value);
			}

			result["isError"] = false;
			result["content"] = std::move(content);
			return result;
		}

		Json ToolErrorResult(const std::string& message)
		{
			Json result = Json::MakeObject();
			Json content = Json::MakeArray();
			Json text = Json::MakeObject();
			text["type"] = "text";
			text["text"] = message;
			content.Push(std::move(text));
			result["isError"] = true;
			result["content"] = std::move(content);
			return result;
		}

	} // namespace

	void ToolServer::CallToolAsync(const Json& params, std::function<void(Json result)> done)
	{
		const std::string name = params["name"].AsString();
		const ToolDesc* tool = name.empty() ? nullptr : FindTool(name);

		if (!tool)
		{
			throw InvalidParamsError("unknown tool: " + name);
		}

		if (!tool->AsyncHandler)
		{
			try
			{
				done(ToolResult(Call(name, params["arguments"])));
			}
			catch (const std::exception& e)
			{
				done(ToolErrorResult(e.what()));
			}

			return;
		}

		const Json arguments = params["arguments"].IsNull() ? Json::MakeObject() : params["arguments"];
		const auto finished = std::make_shared<bool>(false);
		const auto finish = std::make_shared<std::function<void(Json)>>(std::move(done));
		auto reply = [this, finished, finish, name, arguments](Json value)
		{
			if (!std::exchange(*finished, true))
			{
				if (observer)
				{
					observer(name, arguments);
				}

				(*finish)(ToolResult(std::move(value)));
			}
		};
		auto fail = [finished, finish](std::string error)
		{
			if (!std::exchange(*finished, true))
			{
				(*finish)(ToolErrorResult(error));
			}
		};

		// Copy: a handler may register or unregister tools.
		const auto handler = tool->AsyncHandler;

		try
		{
			handler(arguments, reply, fail);
		}
		catch (const std::exception& e)
		{
			fail(e.what());
		}
	}

	std::optional<Json> ToolServer::HandleMessage(const Json& message)
	{
		if (!message.IsObject() || message["jsonrpc"].AsString() != "2.0")
		{
			return ErrorResponse(Json(), InvalidRequest, "expected a JSON-RPC 2.0 object");
		}

		const Json* id = message.Find("id");
		const std::string& method = message["method"].AsString();

		if (method.empty())
		{
			// A response or malformed message from the client: nothing to answer.
			return std::nullopt;
		}

		if (!id)
		{
			// Notifications (notifications/initialized, cancelled, ...) need no reply.
			return std::nullopt;
		}

		try
		{
			if (method == "initialize")
			{
				const std::string& requested = message["params"]["protocolVersion"].AsString();
				const auto& supported = SupportedProtocolVersions();
				const bool known = std::find(supported.begin(), supported.end(), requested) != supported.end();
				Json result = Json::MakeObject();
				result["protocolVersion"] = known ? requested : supported.front();
				result["capabilities"]["tools"]["listChanged"] = false;
				result["serverInfo"]["name"] = info.Name;
				result["serverInfo"]["version"] = info.Version;

				if (!info.Instructions.empty())
				{
					result["instructions"] = info.Instructions;
				}

				return Result(*id, std::move(result));
			}

			if (method == "ping")
			{
				return Result(*id, Json::MakeObject());
			}

			if (method == "tools/list")
			{
				Json list = Json::MakeArray();

				for (const auto& tool : tools)
				{
					Json entry = Json::MakeObject();
					entry["name"] = tool.Name;
					entry["description"] = tool.Description;
					entry["inputSchema"] = tool.InputSchema;
					list.Push(std::move(entry));
				}

				Json result = Json::MakeObject();
				result["tools"] = std::move(list);
				return Result(*id, std::move(result));
			}

			if (method == "tools/call")
			{
				std::optional<Json> result;
				CallToolAsync(message["params"],
					[&result](Json value)
					{
						result = std::move(value);
					});

				if (!result)
				{
					result = ToolErrorResult("the tool completes asynchronously; call it through a transport");
				}

				return Result(*id, std::move(*result));
			}

			return ErrorResponse(*id, MethodNotFound, "method not found: " + method);
		}
		catch (const InvalidParamsError& e)
		{
			return ErrorResponse(*id, InvalidParams, e.what());
		}
		catch (const std::exception& e)
		{
			return ErrorResponse(*id, InternalError, e.what());
		}
	}

	std::optional<std::string> ToolServer::Handle(std::string_view body)
	{
		std::string error;
		const auto message = Json::TryParse(body, &error);

		if (!message)
		{
			return ErrorResponse(Json(), ParseError, error).Dump();
		}

		if (message->IsArray())
		{
			return ErrorResponse(Json(), InvalidRequest, "batches are not supported").Dump();
		}

		const auto response = HandleMessage(*message);
		return response ? std::optional<std::string>(response->Dump()) : std::nullopt;
	}

	void ToolServer::Dispatch(const Json& message, std::function<void(std::optional<Json>)> done)
	{
		const Json* id = message.IsObject() ? message.Find("id") : nullptr;

		if (!id || message["jsonrpc"].AsString() != "2.0" || message["method"].AsString() != "tools/call")
		{
			done(HandleMessage(message));
			return;
		}

		const Json requestId = *id;

		try
		{
			CallToolAsync(message["params"],
				[done, requestId](Json result)
				{
					done(Result(requestId, std::move(result)));
				});
		}
		catch (const InvalidParamsError& e)
		{
			done(ErrorResponse(requestId, InvalidParams, e.what()));
		}
		catch (const std::exception& e)
		{
			done(ErrorResponse(requestId, InternalError, e.what()));
		}
	}

	void ToolServer::HandleAsync(std::string_view body, std::function<void(std::optional<std::string>)> done)
	{
		std::string error;
		const auto message = Json::TryParse(body, &error);

		if (!message)
		{
			done(ErrorResponse(Json(), ParseError, error).Dump());
			return;
		}

		if (message->IsArray())
		{
			done(ErrorResponse(Json(), InvalidRequest, "batches are not supported").Dump());
			return;
		}

		Dispatch(*message,
			[done = std::move(done)](std::optional<Json> response)
			{
				done(response ? std::optional<std::string>(response->Dump()) : std::nullopt);
			});
	}

	std::future<std::optional<std::string>> ToolServer::Enqueue(std::string body)
	{
		auto reply = std::make_shared<std::promise<std::optional<std::string>>>();
		auto future = reply->get_future();
		std::lock_guard lock(queueMutex);
		queue.push_back({ std::move(body), std::move(reply) });
		return future;
	}

	std::size_t ToolServer::Pump()
	{
		std::vector<Pending> work;

		{
			std::lock_guard lock(queueMutex);
			work.swap(queue);
		}

		for (auto& item : work)
		{
			const auto reply = item.Reply;
			const auto answered = std::make_shared<bool>(false);

			try
			{
				HandleAsync(item.Body,
					[reply, answered](std::optional<std::string> response)
					{
						if (!std::exchange(*answered, true))
						{
							reply->set_value(std::move(response));
						}
					});
			}
			catch (...)
			{
				if (!std::exchange(*answered, true))
				{
					reply->set_exception(std::current_exception());
				}
			}
		}

		return work.size();
	}

	namespace Schema
	{

		Json Object(Json::Object properties, std::vector<std::string> required)
		{
			Json schema = Json::MakeObject();
			schema["type"] = "object";
			schema["properties"] = Json(std::move(properties));

			if (!required.empty())
			{
				Json list = Json::MakeArray();

				for (auto& name : required)
				{
					list.Push(std::move(name));
				}

				schema["required"] = std::move(list);
			}

			return schema;
		}

		namespace
		{

			Json Typed(const char* type, std::string_view description)
			{
				Json schema = Json::MakeObject();
				schema["type"] = type;
				schema["description"] = description;
				return schema;
			}

		} // namespace

		Json Number(std::string_view description)
		{
			return Typed("number", description);
		}

		Json Integer(std::string_view description)
		{
			return Typed("integer", description);
		}

		Json String(std::string_view description)
		{
			return Typed("string", description);
		}

		Json Boolean(std::string_view description)
		{
			return Typed("boolean", description);
		}

		Json Vector(std::size_t components, std::string_view description)
		{
			Json schema = Typed("array", description);
			schema["items"]["type"] = "number";
			schema["minItems"] = static_cast<std::uint64_t>(components);
			schema["maxItems"] = static_cast<std::uint64_t>(components);
			return schema;
		}

		Json Enum(std::vector<std::string> values, std::string_view description)
		{
			Json schema = Typed("string", description);
			Json list = Json::MakeArray();

			for (auto& value : values)
			{
				list.Push(std::move(value));
			}

			schema["enum"] = std::move(list);
			return schema;
		}

		Json ArrayOf(Json items, std::string_view description)
		{
			Json schema = Typed("array", description);
			schema["items"] = std::move(items);
			return schema;
		}

	} // namespace Schema

} // namespace Swim::Tooling
