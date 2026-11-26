#include "Engine/Systems/Replay/ReplayEditing.h"
#include "Engine/Systems/Replay/ReplayPreview.h"
#include "Engine/Systems/Replay/ReplayRecorder.h"
#include "Engine/Systems/Replay/ReplaySequence.h"
#include "Engine/Systems/Replay/ReplayTools.h"
#include "Engine/Systems/Tooling/ImageEncoding.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <filesystem>
#include <optional>

using namespace Swim::Replay;
using Swim::Tooling::Image;
using Swim::Tooling::Json;

namespace
{

	struct TempFile
	{
		explicit TempFile(std::filesystem::path name) : Path(std::filesystem::temp_directory_path() / name)
		{
			std::error_code ignored;
			std::filesystem::remove_all(Path, ignored);
		}

		~TempFile()
		{
			std::error_code ignored;
			std::filesystem::remove_all(Path, ignored);
		}

		std::filesystem::path Path;
	};

	// One node sliding along +X at speed m/s for seconds.
	class MoverSource : public IReplayCaptureSource
	{
	  public:
		explicit MoverSource(float speed) : speed(speed) {}

		void Capture(ReplayCapture& capture) override
		{
			NodeState& mover = capture.Node(10);
			mover.Kind = capture.Name("mesh");
			mover.Pose.Position = { static_cast<float>(capture.GetTime()) * speed, 0.0f, 0.0f };
		}

	  private:
		float speed;
	};

	void RecordMover(const std::filesystem::path& path, double seconds = 4.0, float speed = 1.0f)
	{
		MoverSource source(speed);
		ReplayRecorder recorder;
		SWIM_REQUIRE(recorder.Start(path, {}));
		recorder.AddSource(source);

		for (int frame = 0; frame <= static_cast<int>(seconds * 10.0 + 0.5); ++frame)
		{
			recorder.Capture(frame * 0.1);
		}

		SWIM_REQUIRE(recorder.Stop());
	}

	Image Solid(std::uint32_t w, std::uint32_t h, std::uint8_t r, std::uint8_t g, std::uint8_t b)
	{
		Image image{ w, h, {} };

		for (std::uint32_t i = 0; i < w * h; ++i)
		{
			image.Rgba.insert(image.Rgba.end(), { r, g, b, 255 });
		}

		return image;
	}

	const std::uint8_t* Pixel(const Image& image, std::uint32_t x, std::uint32_t y)
	{
		return image.Rgba.data() + (std::size_t(y) * image.Width + x) * 4;
	}

	// A renderer that paints each frame with its time (red, tenths of a second) and fov (green).
	PreviewRenderer FakeRenderer(std::vector<PreviewRequest>& seen)
	{
		return [&seen](const PreviewRequest& request, PreviewReply reply)
		{
			seen.push_back(request);
			reply(Solid(request.Width, request.Height, static_cast<std::uint8_t>(std::lround(request.Time * 10.0)),
					  static_cast<std::uint8_t>(std::lround(request.Camera.Fov)), 7),
				{});
		};
	}

	bool Near(const Vec3& a, const Vec3& b, float tolerance = 1.0e-3f)
	{
		return std::abs(a[0] - b[0]) <= tolerance && std::abs(a[1] - b[1]) <= tolerance && std::abs(a[2] - b[2]) <= tolerance;
	}

} // namespace

SWIM_TEST("Tooling.Images", "PngBase64AndContactSheetTiling")
{
	SWIM_CHECK(Swim::Tooling::EncodeBase64(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("Man"), 3)) == "TWFu");
	SWIM_CHECK(Swim::Tooling::EncodeBase64(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("Ma"), 2)) == "TWE=");
	SWIM_CHECK(Swim::Tooling::EncodeBase64(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("M"), 1)) == "TQ==");

	const std::vector<std::uint8_t> png = Swim::Tooling::EncodePng(Solid(3, 2, 10, 20, 30));
	SWIM_REQUIRE(png.size() > 33);
	SWIM_CHECK(png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
	SWIM_CHECK(png[12] == 'I' && png[13] == 'H' && png[14] == 'D' && png[15] == 'R');
	SWIM_CHECK(png[19] == 3 && png[23] == 2 && png[24] == 8 && png[25] == 6);
	SWIM_CHECK(Swim::Tooling::EncodePng(Image{ 2, 2, { 1, 2, 3 } }).empty());

	const std::vector<Image> frames{ Solid(8, 8, 255, 0, 0), Solid(16, 8, 0, 255, 0), Solid(4, 4, 0, 0, 255) };
	const Image sheet = Swim::Tooling::TileImages(frames, 8, 8, 2, 2, 0xFF000000u);
	SWIM_REQUIRE(sheet.IsValid());
	SWIM_CHECK(sheet.Width == 2 * 8 + 3 * 2 && sheet.Height == 2 * 8 + 3 * 2);
	SWIM_CHECK(Pixel(sheet, 2 + 4, 2 + 4)[0] == 255);						// Red fills its cell.
	SWIM_CHECK(Pixel(sheet, 12 + 4, 2 + 4)[1] == 255);						// Green is letterboxed to 8x4, centred...
	SWIM_CHECK(Pixel(sheet, 12 + 4, 2 + 0)[1] == 0);						// ...so the cell's top row is background.
	SWIM_CHECK(Pixel(sheet, 2 + 4, 12 + 4)[2] == 255);						// Blue scaled up into the second row.
	SWIM_CHECK(Pixel(sheet, 0, 0)[3] == 255 && Pixel(sheet, 0, 0)[0] == 0); // Gap.
}

SWIM_TEST("Tooling.Images", "ToolResultsCarryImageContent")
{
	Swim::Tooling::ToolServer server;
	server.Register({ "img", "", {}, [](const Json&)
		{
			Json r = Json::MakeObject();
			r["path"] = "x.png";
			Json content = Json::MakeArray();
			const std::uint8_t bytes[] = { 1, 2, 3 };
			content.Push(Swim::Tooling::ImageContent(bytes));
			r["$content"] = std::move(content);
			return r;
		} });
	Json call = Json::Parse(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"img","arguments":{}}})");
	const auto response = server.HandleMessage(call);
	SWIM_REQUIRE(response.has_value());
	const Json& result = (*response)["result"];
	SWIM_REQUIRE(result["content"].Size() == 2);
	SWIM_CHECK(result["content"][std::size_t{ 1 }]["type"].AsString() == "image");
	SWIM_CHECK(result["content"][std::size_t{ 1 }]["data"].AsString() == "AQID");
	SWIM_CHECK(result["content"][std::size_t{ 0 }]["text"].AsString().find("$content") == std::string::npos);
	SWIM_CHECK(!result["structuredContent"].Contains("$content") && result["structuredContent"]["path"].AsString() == "x.png");
}

SWIM_TEST("Replay.Preview", "PeekLeavesTheTransportAndFrustaAreGeometric")
{
	TempFile file("swim-replay-peek.swrp");
	RecordMover(file.Path);
	ReplayPlayer player;
	SWIM_REQUIRE(player.Open(file.Path));
	player.Seek(1.0);
	const auto discontinuities = player.GetDiscontinuities();
	const ReplayView later = player.Peek(3.0);
	SWIM_CHECK_NEAR(later.Time, 3.0, 1e-9);
	SWIM_REQUIRE(!later.Nodes.empty());
	SWIM_CHECK_NEAR(later.Nodes[0].Pose.Position[0], 3.0f, 1e-3f);
	SWIM_CHECK_NEAR(player.GetTime(), 1.0, 1e-9);
	SWIM_CHECK_NEAR(player.GetView().Nodes[0].Pose.Position[0], 1.0f, 1e-3f);
	SWIM_CHECK(player.GetDiscontinuities() == discontinuities);

	// Playback after a peek still crosses frames normally.
	player.Play();
	player.Update(0.5);
	SWIM_CHECK_NEAR(player.GetView().Nodes[0].Pose.Position[0], 1.5f, 1e-3f);

	CameraPose pose;
	pose.Position = { 1.0f, 2.0f, 3.0f };
	pose.Fov = 90.0f;
	const CameraFrustum f = MakeFrustum(pose, 1.0f, 1.0f, 2.0f);
	SWIM_CHECK(Near(f.Forward, { 0.0f, 0.0f, -1.0f }));
	SWIM_CHECK(Near(f.Far[0], { -1.0f, 0.0f, 1.0f }));
	SWIM_CHECK(Near(f.Far[2], { 3.0f, 4.0f, 1.0f }));
	SWIM_CHECK(Near(f.Near[1], { 2.0f, 1.0f, 2.0f }));

	const auto times = SampleTimes(0.0, 4.0, 4);
	SWIM_REQUIRE(times.size() == 4);
	SWIM_CHECK_NEAR(times[0], 0.5, 1e-12);
	SWIM_CHECK_NEAR(times[3], 3.5, 1e-12);

	CameraPath path;
	path.Kind = PathKind::Bezier;
	path.Points = { { { 0.0f, 0.0f, 0.0f } }, { { 10.0f, 0.0f, 0.0f } } };
	path.AutoTangents();
	const PathViz viz = SamplePathViz(path, 8, 4);
	SWIM_CHECK(viz.Keys.size() == 2 && viz.Handles.size() == 2 && viz.Ticks.size() == 5);
	SWIM_CHECK(Near(viz.Ticks[2], { 5.0f, 0.0f, 0.0f }, 1e-2f));
	SWIM_CHECK(Near(viz.TickDirections[2], { 1.0f, 0.0f, 0.0f }, 1e-2f));
	SWIM_CHECK_NEAR(viz.Length, 10.0f, 1e-2f);
}

SWIM_TEST("Replay.Preview", "RenderAndContactSheetToolsUseTheHostRenderer")
{
	TempFile file("swim-replay-preview.swrp");
	TempFile out("swim-replay-preview-out");
	RecordMover(file.Path);
	ReplayPlayer player;
	SWIM_REQUIRE(player.Open(file.Path));
	ReplayProject& project = player.GetProject();
	CameraRig& rig = project.EditCamera("wide");
	rig.Position.Set(0.0, Vec3{ 0.0f, 2.0f, 8.0f });
	rig.Fov.Set(0.0, 40.0f);
	project.SetShot({ "s1", "wide", 1.0, 3.0 });
	CameraPath& path = project.EditPath("fly");
	path.Points = { { { 0.0f, 1.0f, 0.0f } }, { { 0.0f, 1.0f, -10.0f } } };
	player.Seek(0.5);

	std::vector<PreviewRequest> seen;
	Swim::Tooling::ToolServer server;
	ReplayToolsDesc desc;
	desc.Prefix = "replay.";
	desc.PreviewDirectory = out.Path;
	desc.RenderPreview = FakeRenderer(seen);
	RegisterReplayTools(server, player, desc);

	Json args = Json::MakeObject();
	args["time"] = 2.0;
	args["width"] = 64;
	args["height"] = 32;
	args["options"]["hud"] = "off";
	Json r = server.Call("replay.preview.render", args);
	SWIM_REQUIRE(seen.size() == 1);
	SWIM_CHECK(seen[0].Width == 64 && seen[0].Height == 32 && seen[0].Options.at("hud") == "off");
	SWIM_CHECK_NEAR(seen[0].Time, 2.0, 1e-9);
	SWIM_CHECK(seen[0].Camera.Camera == "wide" && seen[0].Camera.Shot == "s1");
	SWIM_CHECK(std::filesystem::exists(r["path"].AsString()));
	SWIM_CHECK(r["$content"][std::size_t{ 0 }]["mimeType"].AsString() == "image/png");
	SWIM_CHECK_NEAR(player.GetTime(), 0.5, 1e-9); // Previews never move the transport.

	// An explicit pose overrides the camera; inline can be turned off.
	args = Json::MakeObject();
	args["position"] = ToJson(Vec3{ 5.0f, 5.0f, 5.0f });
	args["lookAt"] = ToJson(Vec3{ 0.0f, 0.0f, 0.0f });
	args["fov"] = 75.0;
	args["inline"] = false;
	r = server.Call("replay.preview.render", args);
	SWIM_CHECK(seen.back().Camera.Fov == 75.0f && Near(seen.back().Camera.Position, { 5.0f, 5.0f, 5.0f }));
	SWIM_CHECK(!r.Contains("$content"));

	// A shot sheet: frames spread over the shot, tiled.
	args = Json::MakeObject();
	args["shot"] = "s1";
	args["frames"] = 4;
	args["cellWidth"] = 32;
	args["cellHeight"] = 16;
	seen.clear();
	r = server.Call("replay.preview.contact_sheet", args);
	SWIM_REQUIRE(seen.size() == 4);
	SWIM_CHECK_NEAR(seen[0].Time, 1.25, 1e-9);
	SWIM_CHECK_NEAR(seen[3].Time, 2.75, 1e-9);
	SWIM_CHECK(r["frames"].Size() == 4 && r["width"].AsInt() == 2 * 32 + 3 * 4);
	SWIM_CHECK(std::filesystem::exists(r["path"].AsString()));

	// A path sheet flies along the path, looking along it.
	args = Json::MakeObject();
	args["pathName"] = "fly";
	args["frames"] = 3;
	seen.clear();
	server.Call("replay.preview.contact_sheet", args);
	SWIM_REQUIRE(seen.size() == 3);
	SWIM_CHECK(Near(seen[2].Camera.Position, { 0.0f, 1.0f, -10.0f }));
	SWIM_CHECK(Near(Swim::Animation::Rotate(seen[1].Camera.Rotation, { 0.0f, 0.0f, -1.0f }), { 0.0f, 0.0f, -1.0f }));

	SWIM_CHECK_THROWS(server.Call("replay.preview.contact_sheet", Json::MakeObject()), Swim::Tooling::ToolError);

	// A host answering later (frames from now) completes the call then.
	std::optional<PreviewReply> pending;
	desc.RenderPreview = [&](const PreviewRequest&, PreviewReply reply)
	{
		pending = std::move(reply);
	};
	Swim::Tooling::ToolServer deferred;
	RegisterReplayTools(deferred, player, desc);
	std::optional<std::string> response;
	deferred.HandleAsync(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"replay.preview.render","arguments":{}}})",
		[&](std::optional<std::string> body)
		{
			response = std::move(body);
		});
	SWIM_CHECK(!response && pending);
	(*pending)(Solid(8, 8, 1, 2, 3), {});
	SWIM_REQUIRE(response.has_value());
	SWIM_CHECK(response->find("\"image\"") != std::string::npos);

	// Failures and a missing renderer are tool errors.
	desc.RenderPreview = [](const PreviewRequest&, PreviewReply reply)
	{
		reply({}, "device lost");
	};
	Swim::Tooling::ToolServer failing;
	RegisterReplayTools(failing, player, desc);
	SWIM_CHECK_THROWS(failing.Call("replay.preview.render"), Swim::Tooling::ToolError);
	desc.RenderPreview = nullptr;
	Swim::Tooling::ToolServer none;
	RegisterReplayTools(none, player, desc);
	SWIM_CHECK_THROWS(none.Call("replay.preview.render"), Swim::Tooling::ToolError);
}

SWIM_TEST("Replay.Preview", "VizToolsDescribePathsCamerasAndKeys")
{
	TempFile file("swim-replay-viz.swrp");
	RecordMover(file.Path);
	ReplayPlayer player;
	SWIM_REQUIRE(player.Open(file.Path));
	ReplayProject& project = player.GetProject();
	project.EditPath("p").Points = { { { 0.0f, 0.0f, 0.0f } }, { { 4.0f, 0.0f, 0.0f } }, { { 4.0f, 0.0f, 4.0f } } };
	CameraRig& rig = project.EditCamera("r");
	rig.Path = "p";
	rig.Progress.Set(0.0, 0.0f);
	rig.Progress.Set(4.0, 1.0f);
	rig.Fov.Set(0.0, 50.0f);
	project.SetCut(0.0, "r");
	player.Refresh();
	Swim::Tooling::ToolServer server;
	RegisterReplayTools(server, player);

	Json args = Json::MakeObject();
	args["name"] = "p";
	args["ticks"] = 2;
	Json r = server.Call("viz.path", args);
	SWIM_CHECK(r["keys"].Size() == 3 && r["ticks"].Size() == 3 && r["curve"].Size() > 3);
	SWIM_CHECK(r["length"].AsNumber() > 7.0 && r["length"].AsNumber() < 8.5);

	args = Json::MakeObject();
	args["time"] = 2.0;
	args["aspect"] = 2.0;
	r = server.Call("viz.camera", args);
	SWIM_CHECK(r["camera"].AsString() == "r" && r["frustum"]["far"].Size() == 4);
	SWIM_CHECK_NEAR(r["fov"].AsNumber(), 50.0, 1e-4);

	args = Json::MakeObject();
	args["from"] = 0.0;
	args["to"] = 4.0;
	args["count"] = 8;
	r = server.Call("viz.shot", args);
	SWIM_REQUIRE(r["samples"].Size() == 8);
	const Vec3 first = Vec3FromJson(r["curve"][std::size_t{ 0 }]);
	const Vec3 last = Vec3FromJson(r["curve"][std::size_t{ 7 }]);
	SWIM_CHECK(first[2] < 1.0f && last[2] > 3.0f); // Along the path from start to end.

	args = Json::MakeObject();
	args["camera"] = "r";
	r = server.Call("viz.keys", args);
	SWIM_CHECK(r["path"].AsString() == "p" && r["progress"].Size() == 2);
	SWIM_CHECK(Near(Vec3FromJson(r["progress"][std::size_t{ 1 }]["position"]), { 4.0f, 0.0f, 4.0f }));
	SWIM_CHECK(r["fov"].Size() == 1);
}

SWIM_TEST("Replay.Sequence", "LayoutAndTransitions")
{
	ReplaySequence sequence;
	sequence.Items = { { "a", "a.swrp" }, { "b", "b.swrp", "", 1.0, 9.0, { TransitionKind::Crossfade, 2.0 } },
		{ "c", "c.swrp", "", 0.0, -1.0, { TransitionKind::Fade, 2.0, { 1.0f, 0.0f, 0.0f } } } };
	const std::vector<double> lengths{ 10.0, 20.0, 6.0 };
	const auto spans = sequence.Layout(lengths);
	SWIM_REQUIRE(spans.size() == 3);
	SWIM_CHECK_NEAR(spans[1].Start, 8.0, 1e-9);
	SWIM_CHECK_NEAR(spans[1].End, 16.0, 1e-9);
	SWIM_CHECK_NEAR(spans[2].Start, 16.0, 1e-9);
	SWIM_CHECK_NEAR(spans[2].End, 22.0, 1e-9);

	SequenceSample s = ReplaySequence::Sample(sequence.Items, spans, 9.0);
	SWIM_CHECK(s.Primary == 0 && s.Secondary == 1);
	SWIM_CHECK_NEAR(s.Weight, 0.5f, 1e-6f);
	SWIM_CHECK_NEAR(s.PrimaryTime, 9.0, 1e-9);
	SWIM_CHECK_NEAR(s.SecondaryTime, 2.0, 1e-9); // In 1 + 1 second.

	s = ReplaySequence::Sample(sequence.Items, spans, 15.5);
	SWIM_CHECK(s.Primary == 1 && s.Secondary == -1);
	SWIM_CHECK_NEAR(s.FadeAmount, 0.5f, 1e-6f);
	SWIM_CHECK(Near(s.FadeColor, { 1.0f, 0.0f, 0.0f }));
	s = ReplaySequence::Sample(sequence.Items, spans, 16.5);
	SWIM_CHECK(s.Primary == 2);
	SWIM_CHECK_NEAR(s.FadeAmount, 0.5f, 1e-6f);
	SWIM_CHECK_NEAR(s.PrimaryTime, 0.5, 1e-9);
	s = ReplaySequence::Sample(sequence.Items, spans, 5.0);
	SWIM_CHECK(s.Primary == 0 && s.FadeAmount == 0.0f);
	s = ReplaySequence::Sample(sequence.Items, spans, 100.0);
	SWIM_CHECK(s.Primary == 2);
	SWIM_CHECK_NEAR(s.PrimaryTime, 6.0, 1e-9);
	SWIM_CHECK(ReplaySequence::Sample({}, {}, 1.0).Primary == -1);

	// A crossfade longer than half an item is limited.
	sequence.Items[1].Enter.Duration = 50.0;
	SWIM_CHECK_NEAR(sequence.Layout(lengths)[1].Start, 6.0, 1e-9);

	const ReplaySequence copy = ReplaySequence::FromJson(Json::Parse(sequence.ToJson().Dump()));
	SWIM_REQUIRE(copy.Items.size() == 3);
	SWIM_CHECK(copy.Items[2].Enter.Kind == TransitionKind::Fade && Near(copy.Items[2].Enter.Color, { 1.0f, 0.0f, 0.0f }));
	SWIM_CHECK_NEAR(copy.Items[1].Out, 9.0, 1e-9);
	SWIM_CHECK_THROWS(ReplaySequence::FromJson(Json::Parse(R"({"items":[{"recording":""}]})")), std::runtime_error);
}

SWIM_TEST("Replay.Sequence", "PlayerAndToolsPlayRecordingsBackToBack")
{
	TempFile dir("swim-replay-sequence");
	std::filesystem::create_directories(dir.Path);
	RecordMover(dir.Path / "one.swrp", 4.0, 1.0f);
	RecordMover(dir.Path / "two.swrp", 4.0, -1.0f);

	ReplayPlayer main;
	SequencePlayer sequence;
	sequence.SetRoot(dir.Path);
	Swim::Tooling::ToolServer server;
	ReplayToolsDesc desc;
	desc.Root = dir.Path;
	desc.Sequence = &sequence;
	RegisterReplayTools(server, main, desc);

	Json args = Json::MakeObject();
	args["recording"] = "one.swrp";
	SWIM_CHECK(server.Call("sequence.add", args)["opened"].AsBool());
	args["recording"] = "two.swrp";
	args["transition"] = "crossfade";
	args["duration"] = 1.0;
	args["in"] = 0.5;
	Json r = server.Call("sequence.add", args);
	SWIM_CHECK_NEAR(r["length"].AsNumber(), 4.0 + 3.5 - 1.0, 1e-6);
	args = Json::MakeObject();
	args["recording"] = "missing.swrp";
	r = server.Call("sequence.add", args);
	SWIM_CHECK(!r["opened"].AsBool() && !r["error"].AsString().empty());
	args = Json::MakeObject();
	args["index"] = 2;
	server.Call("sequence.remove", args);

	// Inside the crossfade both players are positioned at their own times.
	args = Json::MakeObject();
	args["time"] = 3.5;
	r = server.Call("sequence.seek", args);
	SWIM_CHECK(r["primary"].AsInt() == 0 && r["secondary"].AsInt() == 1);
	SWIM_CHECK_NEAR(r["weight"].AsNumber(), 0.5, 1e-6);
	const SequenceView& view = sequence.GetView();
	SWIM_REQUIRE(view.Primary && view.Secondary);
	SWIM_CHECK_NEAR(view.Primary->GetTime(), 3.5, 1e-6);
	SWIM_CHECK_NEAR(view.Secondary->GetTime(), 1.0, 1e-6);
	SWIM_CHECK_NEAR(view.Secondary->GetView().Nodes[0].Pose.Position[0], -1.0f, 1e-3f);

	// Playing steps the players forward (no seeks while in step).
	sequence.Play();
	const auto jumps = view.Primary->GetDiscontinuities();
	sequence.Update(0.25);
	SWIM_CHECK_NEAR(view.Primary->GetTime(), 3.75, 1e-6);
	SWIM_CHECK(view.Primary->GetDiscontinuities() == jumps);

	for (int i = 0; i < 40; ++i)
	{
		sequence.Update(0.25);
	}

	SWIM_CHECK(!sequence.IsPlaying());
	SWIM_CHECK(sequence.GetView().PrimaryIndex == 1 && sequence.GetView().Secondary == nullptr);

	// Reorder, change the transition, save and load.
	args = Json::MakeObject();
	args["from"] = 1;
	args["to"] = 0;
	server.Call("sequence.move", args);
	SWIM_CHECK(sequence.GetSequence().Items[0].Recording == "two.swrp");
	args = Json::MakeObject();
	args["index"] = 1;
	args["transition"] = "fade";
	args["duration"] = 2.0;
	args["color"] = ToJson(Vec3{ 1.0f, 1.0f, 1.0f });
	server.Call("sequence.set_transition", args);
	r = server.Call("sequence.list");
	SWIM_REQUIRE(r["items"].Size() == 2);
	SWIM_CHECK(r["items"][std::size_t{ 1 }]["transition"]["kind"].AsString() == "fade");
	SWIM_CHECK_NEAR(r["items"][std::size_t{ 1 }]["start"].AsNumber(), 3.5, 1e-6);
	args = Json::MakeObject();
	args["path"] = "cut.json";
	server.Call("sequence.save", args);
	sequence.GetSequence().Items.clear();
	sequence.Changed();
	server.Call("sequence.load", args);
	SWIM_CHECK(sequence.GetSequence().Items.size() == 2 && sequence.GetSequence().Items[1].Enter.Kind == TransitionKind::Fade);
	args = Json::MakeObject();
	args["time"] = 3.0;
	r = server.Call("sequence.seek", args);
	SWIM_CHECK(r["primary"].AsInt() == 0);
	SWIM_CHECK_NEAR(r["fadeAmount"].AsNumber(), 0.5, 1e-6);
}

SWIM_TEST("Replay.Music", "BakedTimeScaleIsExactForStepAndLinearKeys")
{
	ReplayProject project;
	project.TimeScale.Set(0.0, 0.5f, Ease::Step);
	project.TimeScale.Set(2.0, 2.0f, Ease::Linear);
	project.TimeScale.Set(3.0, 1.0f, Ease::Linear);
	project.BakeTimeScale(10.0);
	SWIM_CHECK_NEAR(project.ToRecordingTime(2.0), 1.0, 1e-9);
	SWIM_CHECK_NEAR(project.ToRecordingTime(3.0), 2.5, 1e-9);
	SWIM_CHECK_NEAR(project.ToRecordingTime(4.0), 3.5, 1e-9);
}

SWIM_TEST("Replay.Music", "RegionSyncLandsEventsOnBeats")
{
	for (const bool smooth : { true, false })
	{
		ReplayProject project;
		project.AddMarker(4.0, "hit", "user");
		project.AddMarker(4.5, "hit2", "user");
		project.AddMarker(9.0, "later", "user");
		RegionSyncDesc d;
		d.From = 2.0;
		d.To = 6.0;
		d.Pairs = { { 3.0, 4.0 }, { 5.0, 4.5 } };
		d.Smooth = smooth;
		d.RetimeMarkerKind = "user";
		const RegionSyncResult result = SyncRegion(project, d, 20.0);
		SWIM_CHECK(result.Anchors.size() == 4 && result.Dropped == 0);
		SWIM_CHECK(result.MaxError < 1e-6);
		SWIM_CHECK_NEAR(project.ToRecordingTime(1.0), 1.0, 1e-6); // Before the region nothing moves.
		SWIM_CHECK_NEAR(project.ToRecordingTime(2.0), 2.0, 1e-4); // A 10 us ramp into the region's first speed.
		SWIM_CHECK_NEAR(project.ToRecordingTime(3.0), 4.0, 1e-6);
		SWIM_CHECK_NEAR(project.ToRecordingTime(5.0), 4.5, 1e-6);
		SWIM_CHECK_NEAR(project.ToRecordingTime(6.0), 6.0, 1e-6);
		SWIM_CHECK_NEAR(project.Markers[0].Time, 3.0, 1e-4); // The event markers moved onto their beats.
		SWIM_CHECK_NEAR(project.Markers[1].Time, 5.0, 1e-4);
		SWIM_CHECK_NEAR(project.Markers[2].Time, 9.0, 1e-9); // Outside the content.

		for (double t = 2.0; t <= 6.0; t += 0.01)
		{
			SWIM_CHECK(*project.TimeScale.Evaluate(t) >= d.MinScale - 1e-4f);
		}

		SWIM_CHECK_NEAR(*project.TimeScale.Evaluate(7.0), 1.0f, 1e-6f); // The old speed resumes.
	}

	// Auto-matching onto the nearest beats, with an existing slow-motion track.
	ReplayProject project;
	project.TimeScale.Set(0.0, 0.5f, Ease::Linear);
	project.BakeTimeScale(20.0);
	RegionSyncDesc d;
	d.From = 2.0;
	d.To = 6.0;
	d.ContentTo = 8.0;
	for (double b = 2.0; b <= 6.0; b += 0.5)
	{
		d.Beats.push_back(b);
	}
	d.Events = { 4.1, 6.9 }; // Recording 2.05 and 3.45 of the content 1..4.
	const RegionSyncResult result = SyncRegion(project, d, 20.0);
	SWIM_CHECK(result.Anchors.size() == 4);
	SWIM_CHECK_NEAR(result.Anchors[1].first, 3.5, 1e-9);
	SWIM_CHECK_NEAR(result.Anchors[2].first, 5.5, 1e-9);
	SWIM_CHECK(result.MaxError < 1e-6);
	SWIM_CHECK_NEAR(project.ToRecordingTime(2.0), 1.0, 1e-6);
	SWIM_CHECK_NEAR(project.ToRecordingTime(3.5), 2.05, 1e-6);
	SWIM_CHECK_NEAR(project.ToRecordingTime(6.0), 4.0, 1e-6);

	// Regions that cannot fit are refused.
	ReplayProject strict;
	RegionSyncDesc tooFast;
	tooFast.From = 0.0;
	tooFast.To = 1.0;
	tooFast.ContentTo = 50.0;
	SWIM_CHECK_THROWS(SyncRegion(strict, tooFast, 100.0), std::invalid_argument);
	tooFast.To = tooFast.From;
	SWIM_CHECK_THROWS(SyncRegion(strict, tooFast, 100.0), std::invalid_argument);
}

SWIM_TEST("Replay.Music", "TimeScaleSuggestionsFollowTheMusic")
{
	OnsetAnalysis onsets;
	onsets.FrameRate = 10.0;
	onsets.Duration = 20.0;
	onsets.Envelope.assign(200, 0.1f);

	for (std::size_t i = 100; i < 200; ++i)
	{
		onsets.Envelope[i] = 1.0f; // Loud second half.
	}

	onsets.Onsets = { 4.0, 11.5, 15.0 };
	onsets.Strength = { 0.2f, 1.0f, 0.3f };
	TimeScaleSuggestDesc d;
	d.From = 0.0;
	d.To = 20.0;
	d.Onsets = &onsets;

	for (double b = 0.0; b <= 20.0; b += 0.5)
	{
		d.Beats.push_back(b);
	}

	d.Sections = { 10.0 };
	const auto calm = SuggestTimeScale(d);
	SWIM_REQUIRE(calm.size() > 4);
	float quiet = 0.0f;
	float loud = 0.0f;

	for (const auto& k : calm)
	{
		SWIM_CHECK(k.Data >= 0.6f - 1e-5f && k.Data <= 1.0f + 1e-5f && k.Time >= 0.0 && k.Time <= 20.0);
		(k.Time < 8.0 ? quiet : loud) = k.Data;
	}

	SWIM_CHECK(quiet < loud);

	d.Style = TimeScaleStyle::Punchy;
	const auto punchy = SuggestTimeScale(d);
	const auto at = [&](double t) -> const TrackKey<float>*
	{
		for (const auto& k : punchy)
		{
			if (std::abs(k.Time - t) < 1e-6)
			{
				return &k;
			}
		}

		return nullptr;
	};
	SWIM_REQUIRE(at(10.0) && at(11.5) == nullptr); // The section hit; the strong onset right after it is too close.
	SWIM_CHECK_NEAR(at(10.0)->Data, 0.3f, 1e-6f);
	SWIM_CHECK(at(10.5) && at(10.5)->Data > 1.0f);
	SWIM_CHECK(SuggestTimeScale({ 5.0, 5.0 }).empty());
}
