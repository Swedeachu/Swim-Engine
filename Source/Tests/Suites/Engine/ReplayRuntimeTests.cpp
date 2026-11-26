#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"
#include "Engine/Systems/Replay/Runtime/SceneReplayPresenter.h"
#include "Engine/Systems/Replay/Runtime/SceneReplaySource.h"
#include "Engine/Systems/Tooling/Json.h"
#include "Engine/Systems/Tooling/Runtime/ToolTransports.h"
#include "Tests/Framework/Test.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <chrono>
#include <cstring>
#include <future>
#include <string>

using namespace Swim::Tooling;

SWIM_TEST("Replay.Assets", "MeshTextureAndEmitterPayloadsRoundTrip")
{
	Engine::ProceduralMeshes::MeshData mesh;
	mesh.AddVertex({ 0, 0, 0 }, { 0, 1, 0 }, { 0, 0 });
	mesh.AddVertex({ 1, 0, 0 }, { 0, 1, 0 }, { 1, 0 });
	mesh.AddVertex({ 0, 0, 1 }, { 0, 1, 0 }, { 0, 1 });
	mesh.AddTriangle(0, 1, 2);
	Engine::ProceduralMeshes::MeshData meshBack;
	const auto meshBytes = Engine::ReplayAssets::EncodeMesh(mesh);
	SWIM_CHECK(Engine::ReplayAssets::DecodeMesh(meshBytes, meshBack));
	SWIM_CHECK(meshBack.Indices == mesh.Indices && meshBack.Vertices.size() == 3);
	SWIM_CHECK(std::memcmp(meshBack.Vertices.data(), mesh.Vertices.data(), sizeof(mesh.Vertices[0]) * 3) == 0);
	SWIM_CHECK(!Engine::ReplayAssets::DecodeMesh(std::span(meshBytes).first(meshBytes.size() - 1), meshBack));

	const std::vector<std::uint8_t> rgba = { 1, 2, 3, 4, 5, 6, 7, 8 };
	Engine::ReplayAssets::TextureData texture;
	SWIM_CHECK(Engine::ReplayAssets::DecodeTexture(Engine::ReplayAssets::EncodeTexture(2, 1, rgba, false, true), texture));
	SWIM_CHECK(texture.Width == 2 && texture.Height == 1 && !texture.Srgb && texture.Mips && texture.Rgba8 == rgba);

	Swim::Render::ParticleEmitterDesc emitter;
	emitter.Rate = 42.0f;
	emitter.Seed = 7;
	emitter.Bursts.push_back({});
	emitter.Bursts.back().Count = 9;
	Swim::Render::ParticleEmitterDesc emitterBack;
	SWIM_CHECK(Engine::ReplayAssets::DecodeEmitter(Engine::ReplayAssets::EncodeEmitter(emitter), emitterBack));
	SWIM_CHECK(emitterBack.Rate == 42.0f && emitterBack.Seed == 7 && emitterBack.Bursts.size() == 1 && emitterBack.Bursts[0].Count == 9);
}

#ifdef _WIN32

namespace
{

	struct HttpReply
	{
		int Status = 0;
		std::string Headers;
		std::string Body;
	};

	// One request on a fresh connection; the caller's thread keeps pumping the server.
	HttpReply Send(std::uint16_t port, const std::string& request)
	{
		HttpReply reply;
		SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		if (connect(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
		{
			closesocket(s);
			return reply;
		}

		send(s, request.data(), static_cast<int>(request.size()), 0);
		std::string data;
		char buffer[4096];
		std::size_t headerEnd = std::string::npos;
		std::size_t length = 0;

		while (true)
		{
			const int got = recv(s, buffer, sizeof(buffer), 0);

			if (got <= 0)
			{
				break;
			}

			data.append(buffer, static_cast<std::size_t>(got));

			if (headerEnd == std::string::npos && (headerEnd = data.find("\r\n\r\n")) != std::string::npos)
			{
				const std::size_t field = data.find("Content-Length: ");
				length = field < headerEnd ? std::stoul(data.substr(field + 16)) : 0;
			}

			if (headerEnd != std::string::npos && data.size() >= headerEnd + 4 + length)
			{
				break;
			}
		}

		closesocket(s);

		if (headerEnd != std::string::npos && data.size() > 12)
		{
			reply.Status = std::stoi(data.substr(9, 3));
			reply.Headers = data.substr(0, headerEnd);
			reply.Body = data.substr(headerEnd + 4);
		}

		return reply;
	}

	HttpReply Exchange(ToolServer& server, std::uint16_t port, const std::string& request)
	{
		auto pending = std::async(std::launch::async,
			[&]
			{
				return Send(port, request);
			});

		while (pending.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
		{
			server.Pump();
		}

		return pending.get();
	}

	std::string Post(const std::string& token, const std::string& session, const std::string& body, const std::string& origin = {})
	{
		std::string request = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
							  "Accept: application/json, text/event-stream\r\n";
		request += token.empty() ? "" : "Authorization: Bearer " + token + "\r\n";
		request += session.empty() ? "" : "Mcp-Session-Id: " + session + "\r\n";
		request += origin.empty() ? "" : "Origin: " + origin + "\r\n";
		request += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
		return request;
	}

	std::string HeaderValue(const std::string& headers, const std::string& name)
	{
		const std::size_t at = headers.find(name + ": ");

		if (at == std::string::npos)
		{
			return {};
		}

		const std::size_t start = at + name.size() + 2;
		return headers.substr(start, headers.find("\r\n", start) - start);
	}

} // namespace

SWIM_TEST("Replay.HttpTransport", "ServesMcpOverLoopbackWithTokenSessionAndOriginChecks")
{
	ToolServer server;
	server.Register({ "echo", "Echoes text.", Schema::Object({ { "text", Schema::String("Text.") } }, { "text" }), [](const Json& args)
		{
			return Json(args["text"].AsString());
		} });

	HttpToolTransport transport;
	std::string error;
	SWIM_REQUIRE(transport.Start(server, {}, &error));
	const std::uint16_t port = transport.GetPort();
	const std::string token = transport.GetToken();
	SWIM_CHECK(token.size() == 64);
	SWIM_CHECK(transport.GetUrl() == "http://127.0.0.1:" + std::to_string(port) + "/mcp");

	const std::string initialize =
		R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"t","version":"1"}}})";
	SWIM_CHECK(Exchange(server, port, Post("", "", initialize)).Status == 401);
	SWIM_CHECK(Exchange(server, port, Post("wrong", "", initialize)).Status == 401);
	SWIM_CHECK(Exchange(server, port, Post(token, "", initialize, "http://attacker.example")).Status == 403);

	const HttpReply init = Exchange(server, port, Post(token, "", initialize, "http://localhost:3000"));
	SWIM_CHECK(init.Status == 200);
	const std::string session = HeaderValue(init.Headers, "Mcp-Session-Id");
	SWIM_CHECK(!session.empty());
	SWIM_CHECK(Json::Parse(init.Body)["result"]["protocolVersion"].AsString() == "2025-06-18");

	const std::string list = R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})";
	SWIM_CHECK(Exchange(server, port, Post(token, "", list)).Status == 400);
	SWIM_CHECK(Exchange(server, port, Post(token, "unknown-session", list)).Status == 404);

	const HttpReply tools = Exchange(server, port, Post(token, session, list));
	SWIM_CHECK(tools.Status == 200);
	SWIM_CHECK(Json::Parse(tools.Body)["result"]["tools"][std::size_t{ 0 }]["name"].AsString() == "echo");

	const HttpReply call = Exchange(server, port,
		Post(token, session, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"echo","arguments":{"text":"hi"}}})"));
	SWIM_CHECK(call.Status == 200 && call.Body.find("hi") != std::string::npos);

	const HttpReply note = Exchange(server, port, Post(token, session, R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));
	SWIM_CHECK(note.Status == 202);

	const HttpReply get = Exchange(server, port,
		"GET /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + token + "\r\nMcp-Session-Id: " + session + "\r\n\r\n");
	SWIM_CHECK(get.Status == 405);

	transport.Stop();
	SWIM_CHECK(!transport.IsRunning());
}

#endif

SWIM_TEST("Replay.Presenter", "EnvironmentKeysDrivePostProcessingAndRestoreIt")
{
	Engine::RenderSettings settings;
	settings.Post.Grading.Saturation = 0.8f;
	settings.Post.Bloom.Intensity = 0.1f;
	Engine::SceneReplayPresenterDesc desc;
	desc.Render.Settings = &settings;
	Engine::SceneReplayPresenter presenter(desc);
	using Swim::Replay::Value;
	using Swim::Replay::Vec3;

	presenter.OnEnvironment(
		{ { "grade.saturation", Value(0.0f) }, { "post.exposure", Value(-1.5f) }, { "grade.slope", Value(Vec3{ 1.0f, 0.5f, 0.25f }) } });
	SWIM_CHECK(settings.Post.Grading.Saturation == 0.0f && settings.Post.Exposure.Compensation == -1.5f);
	SWIM_CHECK(settings.Post.Grading.Slope[2] == 0.25f && settings.Post.Bloom.Intensity == 0.1f);

	// A key that stops being set goes back to the original value.
	presenter.OnEnvironment({ { "grade.saturation", Value(0.5f) } });
	SWIM_CHECK(settings.Post.Grading.Saturation == 0.5f && settings.Post.Exposure.Compensation == 0.0f);
	SWIM_CHECK(settings.Post.Grading.Slope[2] == 1.0f);

	presenter.Clear();
	SWIM_CHECK(settings.Post.Grading.Saturation == 0.8f);
}
