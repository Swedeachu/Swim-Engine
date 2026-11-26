#include "Engine/Systems/Tooling/Json.h"
#include "Engine/Systems/Tooling/ToolServer.h"
#include "Tests/Framework/Test.h"

#include <stdexcept>
#include <thread>

using namespace Swim::Tooling;

SWIM_TEST("Replay.Json", "ParsesWritesAndRejects")
{
	const Json doc = Json::Parse(R"({"b":[1,2.5,-3e2,true,null],"a":"x\"é\n","c":{}})");
	SWIM_CHECK(doc["a"].AsString() == "x\"\xC3\xA9\n");
	SWIM_CHECK(doc["b"].Size() == 5 && doc["b"][1].AsNumber() == 2.5 && doc["b"][2].AsNumber() == -300.0);
	SWIM_CHECK(doc["b"][3].AsBool() && doc["b"][4].IsNull() && doc["c"].IsObject());
	SWIM_CHECK(doc["missing"].IsNull());

	// Keys are written sorted, so output is stable; the round trip is lossless.
	const std::string text = doc.Dump();
	SWIM_CHECK(text.find("\"a\"") < text.find("\"b\""));
	SWIM_CHECK(Json::Parse(text) == doc);
	SWIM_CHECK(Json::Parse(doc.Dump(2)) == doc);

	// Copies share storage until written.
	Json copy = doc;
	copy["a"] = "changed";
	SWIM_CHECK(doc["a"].AsString() != "changed");

	SWIM_CHECK(!Json::TryParse("{\"a\":}"));
	SWIM_CHECK(!Json::TryParse("[1,2"));
	SWIM_CHECK(!Json::TryParse("{} x"));
	SWIM_CHECK_THROWS(Json::Parse("\"unterminated"), std::runtime_error);
}

namespace
{

	void AddTools(ToolServer& server, int& calls)
	{
		server.Register({ "add", "Adds a and b.",
			Schema::Object({ { "a", Schema::Number("First.") }, { "b", Schema::Number("Second.") } }, { "a", "b" }),
			[&calls](const Json& args)
			{
				++calls;
				Json r = Json::MakeObject();
				r["sum"] = args["a"].AsNumber() + args["b"].AsNumber();
				return r;
			} });
		server.Register({ "fail", "Always fails.", {}, [](const Json&) -> Json
			{
				throw ToolError("nope");
			} });
	}

	Json Send(ToolServer& server, const std::string& body)
	{
		const auto reply = server.Handle(body);
		return reply ? Json::Parse(*reply) : Json();
	}

} // namespace

SWIM_TEST("Replay.ToolServer", "SpeaksTheMcpToolProtocol")
{
	int calls = 0;
	ToolServer server({ "test-tools", "2.0.0", "Use add." });
	AddTools(server, calls);

	const Json init = Send(server,
		R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"t"}}})");
	SWIM_CHECK(init["id"].AsInt() == 1);
	SWIM_CHECK(init["result"]["protocolVersion"].AsString() == "2025-03-26");
	SWIM_CHECK(init["result"]["capabilities"]["tools"].IsObject());
	SWIM_CHECK(init["result"]["serverInfo"]["name"].AsString() == "test-tools");
	SWIM_CHECK(init["result"]["instructions"].AsString() == "Use add.");

	// Unknown versions get the newest supported one.
	const Json future = Send(server, R"({"jsonrpc":"2.0","id":"x","method":"initialize","params":{"protocolVersion":"2099-01-01"}})");
	SWIM_CHECK(future["result"]["protocolVersion"].AsString() == ToolServer::SupportedProtocolVersions().front());
	SWIM_CHECK(future["id"].AsString() == "x");

	// Notifications get no reply.
	SWIM_CHECK(!server.Handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));

	const Json list = Send(server, R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
	const auto& tools = list["result"]["tools"].GetArray();
	SWIM_REQUIRE(tools.size() == 2);
	SWIM_CHECK(tools[0]["name"].AsString() == "add" && tools[0]["inputSchema"]["type"].AsString() == "object");
	SWIM_CHECK(tools[0]["inputSchema"]["required"].Size() == 2);
	SWIM_CHECK(tools[1]["inputSchema"]["type"].AsString() == "object"); // Filled in for an empty schema.

	const Json call = Send(server, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"add","arguments":{"a":2,"b":3}}})");
	SWIM_CHECK(!call["result"]["isError"].AsBool());
	SWIM_CHECK(call["result"]["structuredContent"]["sum"].AsNumber() == 5.0);
	SWIM_CHECK(call["result"]["content"][0]["type"].AsString() == "text");
	SWIM_CHECK(Json::Parse(call["result"]["content"][0]["text"].AsString())["sum"].AsNumber() == 5.0);

	// A failing tool is a tool result with isError; an unknown tool is a protocol error.
	const Json failed = Send(server, R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"fail"}})");
	SWIM_CHECK(failed["result"]["isError"].AsBool() && failed["result"]["content"][0]["text"].AsString() == "nope");
	const Json unknown = Send(server, R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"nothing"}})");
	SWIM_CHECK(unknown["error"]["code"].AsInt() == -32602);
	SWIM_CHECK(Send(server, R"({"jsonrpc":"2.0","id":6,"method":"bogus"})")["error"]["code"].AsInt() == -32601);
	SWIM_CHECK(Send(server, "{bad json")["error"]["code"].AsInt() == -32700);
	SWIM_CHECK(Send(server, R"({"jsonrpc":"2.0","id":7,"method":"ping"})")["result"].IsObject());
	SWIM_CHECK(calls == 1);
}

SWIM_TEST("Replay.ToolServer", "QueuedCallsRunOnThePumpingThread")
{
	int calls = 0;
	ToolServer server({ "test-tools", "2.0.0", "Use add." });
	AddTools(server, calls);
	std::thread::id ranOn;
	server.Register({ "where", "Thread check.", {}, [&ranOn](const Json&)
		{
			ranOn = std::this_thread::get_id();
			return Json::MakeObject();
		} });

	std::future<std::optional<std::string>> reply;
	std::thread client(
		[&]
		{
			reply = server.Enqueue(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"where"}})");
		});
	client.join();
	SWIM_CHECK(reply.wait_for(std::chrono::seconds(0)) != std::future_status::ready); // Nothing runs until Pump.
	SWIM_CHECK(server.Pump() == 1);
	SWIM_CHECK(ranOn == std::this_thread::get_id());
	SWIM_REQUIRE(reply.get().has_value());
	SWIM_CHECK(server.Pump() == 0);
}

SWIM_TEST("Replay.ToolServer", "AsyncToolsReplyFromALaterFrame")
{
	ToolServer server;
	std::function<void(Json)> later;
	ToolDesc capture{ "capture", "Replies later." };
	capture.AsyncHandler = [&later](const Json& args, std::function<void(Json)> reply, std::function<void(std::string)>)
	{
		later = [reply, path = args["path"].AsString()](Json)
		{
			reply(Json(path));
		};
	};
	server.Register(std::move(capture));
	ToolDesc failing{ "failing", "Fails at once." };
	failing.AsyncHandler = [](const Json&, std::function<void(Json)>, std::function<void(std::string)> fail)
	{
		fail("no frame");
	};
	server.Register(std::move(failing));
	int observed = 0;
	server.SetCallObserver(
		[&observed](std::string_view, const Json&)
		{
			++observed;
		});

	auto reply =
		server.Enqueue(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"capture","arguments":{"path":"shot.png"}}})");
	SWIM_CHECK(server.Pump() == 1);
	SWIM_CHECK(reply.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
	SWIM_REQUIRE(later != nullptr);
	later(Json());
	later(Json()); // A second reply is ignored.
	SWIM_REQUIRE(reply.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
	const Json response = Json::Parse(*reply.get());
	SWIM_CHECK(response["id"].AsInt() == 7 && !response["result"]["isError"].AsBool(true));
	SWIM_CHECK(response["result"]["content"][std::size_t{ 0 }]["text"].AsString() == "shot.png");
	SWIM_CHECK(observed == 1);

	auto failed = server.Enqueue(R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"failing"}})");
	server.Pump();
	SWIM_CHECK(Json::Parse(*failed.get())["result"]["isError"].AsBool());
	SWIM_CHECK(observed == 1);

	// Synchronous entry points report a tool that has not replied yet.
	SWIM_CHECK_THROWS(server.Call("capture"), ToolError);
	SWIM_CHECK(
		Json::Parse(*server.Handle(R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"capture"}})"))["result"]["isError"]
			.AsBool());
}
