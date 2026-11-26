#include "Engine/Systems/Replay/ReplayAudio.h"
#include "Engine/Systems/Replay/ReplayEditing.h"
#include "Engine/Systems/Replay/ReplayPath.h"
#include "Engine/Systems/Replay/ReplayPlayer.h"
#include "Engine/Systems/Replay/ReplayRecorder.h"
#include "Engine/Systems/Replay/ReplayTools.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <filesystem>
#include <limits>

using namespace Swim::Replay;
using Swim::Tooling::Json;

namespace
{

	struct TempFile
	{
		explicit TempFile(const char* name) : Path(std::filesystem::temp_directory_path() / name) { std::filesystem::remove(Path); }

		~TempFile()
		{
			std::error_code ignored;
			std::filesystem::remove(Path, ignored);
		}

		std::filesystem::path Path;
	};

	// One node sliding along +X at 1 m/s for 4 seconds.
	class MoverSource : public IReplayCaptureSource
	{
	  public:
		void Capture(ReplayCapture& capture) override
		{
			NodeState& mover = capture.Node(10);
			mover.Kind = capture.Name("mesh");
			mover.Pose.Position = { static_cast<float>(capture.GetTime()), 0.0f, 0.0f };
		}
	};

	void RecordMover(const std::filesystem::path& path)
	{
		MoverSource source;
		ReplayRecorder recorder;
		SWIM_REQUIRE(recorder.Start(path, {}));
		recorder.AddSource(source);

		for (int frame = 0; frame <= 40; ++frame)
		{
			recorder.Capture(frame * 0.1);
		}

		SWIM_REQUIRE(recorder.Stop());
	}

	bool Near(const Vec3& a, const Vec3& b, float tolerance = 1.0e-3f)
	{
		return std::abs(a[0] - b[0]) <= tolerance && std::abs(a[1] - b[1]) <= tolerance && std::abs(a[2] - b[2]) <= tolerance;
	}

	Vec3 Forward(const CameraPose& pose)
	{
		return Swim::Animation::Rotate(pose.Rotation, { 0.0f, 0.0f, -1.0f });
	}

	// Clicks at a tempo: a short decaying 1 kHz burst per beat, every beatsPerBar-th louder.
	std::vector<float> ClickTrack(double rate, double seconds, double bpm, int beatsPerBar = 4)
	{
		std::vector<float> mono(static_cast<std::size_t>(rate * seconds), 0.0f);
		const double period = 60.0 / bpm;
		int beat = 0;

		for (double t = 0.25; t < seconds - 0.05; t += period, ++beat)
		{
			const float gain = beat % beatsPerBar == 0 ? 0.9f : 0.5f;
			const auto start = static_cast<std::size_t>(t * rate);

			for (std::size_t i = 0; i < static_cast<std::size_t>(0.03 * rate) && start + i < mono.size(); ++i)
			{
				const double s = static_cast<double>(i) / rate;
				mono[start + i] += gain * static_cast<float>(std::exp(-s * 150.0) * std::sin(2.0 * 3.14159265358979 * 1000.0 * s));
			}
		}

		return mono;
	}

} // namespace

SWIM_TEST("Replay.Path", "ArcLengthIsUniformAndCurvesPassThroughTheirPoints")
{
	CameraPath linear;
	linear.Kind = PathKind::Linear;
	linear.Points = { { { 0.0f, 0.0f, 0.0f } }, { { 10.0f, 0.0f, 0.0f } }, { { 10.0f, 10.0f, 0.0f } } };
	SWIM_CHECK_NEAR(linear.Length(), 20.0f, 1.0e-3f);
	SWIM_CHECK(Near(linear.Sample(0.25f), { 5.0f, 0.0f, 0.0f }));
	SWIM_CHECK(Near(linear.Sample(0.5f), { 10.0f, 0.0f, 0.0f }));
	SWIM_CHECK(Near(linear.Direction(0.75f), { 0.0f, 1.0f, 0.0f }));

	for (const PathKind kind : { PathKind::CatmullRom, PathKind::Centripetal })
	{
		CameraPath curve;
		curve.Kind = kind;
		// Uneven spacing: uniform Catmull-Rom would overshoot, centripetal must not loop.
		curve.Points = { { { 0.0f, 0.0f, 0.0f } }, { { 1.0f, 0.0f, 0.0f } }, { { 10.0f, 5.0f, 0.0f } }, { { 12.0f, 0.0f, 0.0f } } };
		SWIM_CHECK(Near(curve.Sample(0.0f), { 0.0f, 0.0f, 0.0f }));
		SWIM_CHECK(Near(curve.Sample(1.0f), { 12.0f, 0.0f, 0.0f }));
		SWIM_CHECK(Near(curve.Evaluate(1.0f), { 1.0f, 0.0f, 0.0f }));
		SWIM_CHECK(Near(curve.Evaluate(2.0f), { 10.0f, 5.0f, 0.0f }));
		SWIM_CHECK(Near(curve.Sample(curve.Nearest({ 10.0f, 5.0f, 0.0f })), { 10.0f, 5.0f, 0.0f }, 0.05f));

		// Equal fractions cover equal distances along the curve.
		const float length = curve.Length();
		float shortest = 1.0e9f;
		float longest = 0.0f;

		for (int i = 0; i < 20; ++i)
		{
			float step = 0.0f;

			for (int j = 0; j < 25; ++j)
			{
				const float f0 = (i + j / 25.0f) / 20.0f;
				const float f1 = (i + (j + 1) / 25.0f) / 20.0f;
				step += Swim::Animation::Length(Swim::Animation::Sub(curve.Sample(f1), curve.Sample(f0)));
			}

			shortest = std::min(shortest, step);
			longest = std::max(longest, step);
		}

		SWIM_CHECK(longest - shortest < 0.02f * length);
	}

	// Bezier: explicit handles bend the curve; without handles FromJson makes smooth ones.
	CameraPath bezier;
	bezier.Kind = PathKind::Bezier;
	bezier.Points = { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 4.0f, 0.0f } },
		{ { 10.0f, 0.0f, 0.0f }, { 0.0f, 4.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } };
	SWIM_CHECK(Near(bezier.Sample(0.0f), { 0.0f, 0.0f, 0.0f }));
	SWIM_CHECK(Near(bezier.Sample(1.0f), { 10.0f, 0.0f, 0.0f }));
	SWIM_CHECK(bezier.Sample(0.5f)[1] > 2.0f);

	const CameraPath copy = CameraPath::FromJson(bezier.ToJson());
	SWIM_CHECK(copy.ToJson() == bezier.ToJson());
	const CameraPath automatic = CameraPath::FromJson(Json::Parse(R"({"kind":"bezier","points":[[0,0,0],[5,5,0],[10,0,0]]})"));
	SWIM_CHECK(Near(automatic.Sample(automatic.Nearest({ 5.0f, 5.0f, 0.0f })), { 5.0f, 5.0f, 0.0f }, 0.05f));
	SWIM_CHECK(automatic.Points[1].OutTangent[0] > 0.0f);
}

SWIM_TEST("Replay.Player", "ShotsCutInstantlyFollowPathsAndJumpsTeleport")
{
	TempFile file("swim-replay-shots.swrp");
	RecordMover(file.Path);
	ReplayPlayer player;
	SWIM_REQUIRE(player.Open(file.Path));
	ReplayProject& project = player.GetProject();

	project.EditCamera("wide").Position.Set(0.0, Vec3{ 0.0f, 10.0f, 20.0f });
	project.EditCamera("wide").LookAtNode = 10;
	CameraPath& track = project.EditPath("track");
	track.Kind = PathKind::Linear;
	track.Points = { { { 0.0f, 5.0f, 0.0f } }, { { 10.0f, 5.0f, 0.0f } } };
	project.SetShot({ "a", "wide", 0.0, 1.0 });
	Shot dolly{ "b", "", 1.0, 2.0, "track" };
	dolly.ProgressEase = Ease::Linear;
	project.SetShot(dolly);

	player.Seek(0.99);
	SWIM_CHECK(player.GetView().Camera.Shot == "a" && player.GetView().Camera.Camera == "wide");
	player.Seek(1.5);
	const CameraPose& pose = player.GetView().Camera;
	SWIM_CHECK(pose.Shot == "b");
	SWIM_CHECK(Near(pose.Position, { 5.0f, 5.0f, 0.0f }));
	SWIM_CHECK(Near(Forward(pose), { 1.0f, 0.0f, 0.0f })); // No rig: looks along the path.
	player.Seek(2.5);
	SWIM_CHECK(player.GetView().Camera.Shot.empty());

	// A rig on a path in the mover's frame, aimed at it.
	CameraRig& chase = project.EditCamera("chase");
	chase.Path = "track";
	chase.RelativeTo = 10;
	chase.FollowRotation = false;
	chase.Progress.Set(0.0, 0.0f);
	chase.Progress.Set(4.0, 0.0f);
	chase.Aim = CameraAim::Node;
	chase.LookAtNode = 10;
	project.SetShot({ "c", "chase", 2.0, 4.0 });
	player.Seek(3.0);
	SWIM_CHECK(Near(player.GetView().Camera.Position, { 3.0f, 5.0f, 0.0f }));
	SWIM_CHECK(Near(Forward(player.GetView().Camera), { 0.0f, -1.0f, 0.0f }));

	// An overlapping shot trims its neighbours.
	project.SetShot({ "mid", "wide", 0.5, 1.5 });
	SWIM_REQUIRE(project.FindShot("a") && project.FindShot("b"));
	SWIM_CHECK(project.FindShot("a")->End == 0.5 && project.FindShot("b")->Start == 1.5);

	// Forward playback across a jump key teleports; a seek does not.
	project.AddJump({ "skip", 1.0, 3.0 });
	player.Seek(0.9);
	const std::uint64_t before = player.GetDiscontinuities();
	player.Play();
	player.Update(0.2);
	SWIM_CHECK_NEAR(player.GetTime(), 3.1, 1.0e-6);
	SWIM_CHECK(player.GetDiscontinuities() == before + 1);
	SWIM_CHECK(Near(player.FindNode(10)->Pose.Position, { 3.1f, 0.0f, 0.0f }));
	player.Seek(2.0);
	SWIM_CHECK_NEAR(player.GetTime(), 2.0, 1.0e-9);
}

SWIM_TEST("Replay.Project", "TimeScaleBakesAndNewFieldsRoundTrip")
{
	ReplayProject project;
	project.TimeScale.Set(0.0, 0.5f);
	project.BakeTimeScale(3.0);
	SWIM_REQUIRE(project.TimeRemap.size() >= 2);
	SWIM_CHECK_NEAR(project.TimeRemap.back().first, 6.0, 1.0e-6);
	SWIM_CHECK_NEAR(project.TimeRemap.back().second, 3.0, 1.0e-6);

	// A linear ramp from 1x to 0.5x over two seconds covers 1.5 recorded seconds.
	project.TimeScale = {};
	project.TimeScale.Set(0.0, 1.0f, Ease::Linear);
	project.TimeScale.Set(2.0, 0.5f);
	project.BakeTimeScale(10.0);
	const auto at2 = std::find_if(project.TimeRemap.begin(), project.TimeRemap.end(),
		[](const auto& a)
		{
			return std::abs(a.first - 2.0) < 1.0e-6;
		});
	SWIM_REQUIRE(at2 != project.TimeRemap.end());
	SWIM_CHECK_NEAR(at2->second, 1.5, 1.0e-3);

	CameraPath& path = project.EditPath("orbit");
	path.Kind = PathKind::Centripetal;
	path.Closed = true;
	path.Points = { { { 1.0f, 0.0f, 0.0f } }, { { 0.0f, 0.0f, 1.0f } }, { { -1.0f, 0.0f, 0.0f } } };
	CameraRig& rig = project.EditCamera("r");
	rig.Path = "orbit";
	rig.Progress.Set(0.0, 0.0f);
	rig.Aim = CameraAim::Path;
	rig.LookPath = "orbit";
	rig.LookProgress.Set(1.0, 0.5f);
	rig.LookDirection.Set(0.0, Vec3{ 0.0f, 0.0f, -1.0f });
	rig.RelativeTo = 7;
	rig.FollowRotation = false;
	Shot shot{ "s", "r", 1.0, 3.0, "orbit" };
	shot.ProgressFrom = 0.25f;
	project.SetShot(shot);
	project.AddJump({ "j", 4.0, 2.0 });
	project.Music = MusicTrack{ "song.ogg", 1.5, 0.8f };
	project.AddMarker(2.0, "beat 1", "beat");

	const Json json = project.ToJson();
	const ReplayProject copy = ReplayProject::FromJson(Json::Parse(json.Dump(2)));
	SWIM_CHECK(copy.ToJson() == json);
	SWIM_CHECK(copy.Cameras.at("r").Aim == CameraAim::Path && !copy.Cameras.at("r").FollowRotation);
	SWIM_CHECK(copy.Shots.size() == 1 && copy.Shots[0].ProgressFrom == 0.25f && copy.Shots[0].Path == "orbit");
	SWIM_CHECK(copy.Jumps.size() == 1 && copy.Jumps[0].To == 2.0);
	SWIM_CHECK(copy.Music && copy.Music->Offset == 1.5 && copy.Markers[0].Kind == "beat");
	SWIM_CHECK(copy.Paths.at("orbit").Closed && copy.Paths.at("orbit").Points.size() == 3);
	SWIM_CHECK(project.JumpBetween(3.9, 4.1) != nullptr && project.JumpBetween(4.1, 5.0) == nullptr);
}

SWIM_TEST("Replay.Audio", "ClickTrackGivesOnsetsTempoPeaksAndSections")
{
	const double rate = 22050.0;
	const std::vector<float> clicks = ClickTrack(rate, 16.0, 120.0);
	const OnsetAnalysis onsets = DetectOnsets(clicks, rate);
	SWIM_CHECK(onsets.Onsets.size() >= 28 && onsets.Onsets.size() <= 34); // 31 clicks.
	SWIM_REQUIRE(!onsets.Onsets.empty());
	SWIM_CHECK(std::abs(onsets.Onsets.front() - 0.25) < 0.05);

	const BeatGrid grid = EstimateBeats(onsets);
	SWIM_CHECK_NEAR(grid.Bpm, 120.0, 2.0);
	SWIM_CHECK(grid.Confidence > 0.3f);
	SWIM_REQUIRE(grid.Downbeats.size() >= 2);
	SWIM_CHECK_NEAR(grid.Downbeats[1] - grid.Downbeats[0], 2.0, 0.05);
	// The louder clicks are the downbeats (at 0.25 + 2k).
	const double phase = std::fmod(grid.Downbeats[0] - 0.25 + 2.0, 2.0);
	SWIM_CHECK(phase < 0.06 || phase > 1.94);

	const AudioPeaks peaks = ComputePeaks(clicks, rate, 20.0);
	SWIM_CHECK(peaks.Max.size() == 320 && peaks.Min.size() == 320);
	SWIM_CHECK(peaks.Max[5] > 0.5f && peaks.Min[5] < -0.5f); // 0.25..0.30 s holds the first click.
	SWIM_CHECK(peaks.Max[2] == 0.0f);

	// Two halves with different timbre: one boundary near the change.
	std::vector<float> song = ClickTrack(rate, 32.0, 120.0);

	for (std::size_t i = 0; i < song.size(); ++i)
	{
		const double t = static_cast<double>(i) / rate;
		const double tone = t < 16.0 ? std::sin(2.0 * 3.14159265358979 * 220.0 * t) : std::sin(2.0 * 3.14159265358979 * 3520.0 * t);
		song[i] += 0.3f * static_cast<float>(tone);
	}

	const BeatGrid songGrid = EstimateBeats(DetectOnsets(song, rate));
	const std::vector<double> sections = DetectSections(song, rate, songGrid);
	SWIM_REQUIRE(!sections.empty());
	bool nearChange = false;

	for (const double s : sections)
	{
		nearChange = nearChange || std::abs(s - 16.0) < 1.0;
	}

	SWIM_CHECK(nearChange);

	const std::vector<double> targets{ 1.0, 2.0, 3.0 };
	SWIM_CHECK(Snap(2.04, targets, 0.1) == 2.0);
	SWIM_CHECK(Snap(2.5, targets, 0.1) == 2.5);
	SWIM_CHECK(MixToMono(std::vector<float>{ 1.0f, 0.0f, 0.5f, 0.5f }, 2) == (std::vector<float>{ 0.5f, 0.5f }));
}

SWIM_TEST("Replay.Editing", "ShotPresetsBeatCutsLightsAndPresets")
{
	TempFile file("swim-replay-presets.swrp");
	RecordMover(file.Path);
	ReplayPlayer player;
	SWIM_REQUIRE(player.Open(file.Path));
	ReplayProject& project = player.GetProject();

	// Orbit half way around a world point: starts on +Z, ends on -Z, always looking at it.
	ShotDesc orbit;
	orbit.Name = "spin";
	orbit.Preset = ShotPreset::Orbit;
	orbit.Start = 0.0;
	orbit.End = 2.0;
	orbit.Point = { 0.0f, 0.0f, 0.0f };
	orbit.TargetOffset = { 0.0f, 0.0f, 0.0f };
	orbit.Height = 0.0f;
	orbit.Motion = Ease::Linear;
	CreateShot(project, orbit);
	SWIM_CHECK(project.Cameras.contains("shot:spin") && project.Paths.contains("shot:spin"));
	player.Seek(0.0);
	SWIM_CHECK(Near(player.GetView().Camera.Position, { 0.0f, 0.0f, 6.0f }, 1.0e-2f));
	player.Seek(1.0);
	SWIM_CHECK(Near(player.GetView().Camera.Position, { 6.0f, 0.0f, 0.0f }, 0.1f));
	SWIM_CHECK(Near(Forward(player.GetView().Camera), { -1.0f, 0.0f, 0.0f }, 2.0e-2f));
	player.Seek(1.999);
	SWIM_CHECK(Near(player.GetView().Camera.Position, { 0.0f, 0.0f, -6.0f }, 2.0e-2f));

	// Follow rides with the mover.
	ShotDesc follow;
	follow.Name = "ride";
	follow.Preset = ShotPreset::Follow;
	follow.Start = 2.0;
	follow.End = 4.0;
	follow.Target = 10;
	CreateShot(project, follow);
	player.Seek(3.0);
	SWIM_CHECK(Near(player.GetView().Camera.Position, { 3.0f, 2.0f, 5.0f }));

	// Cuts on beats alternate between sources.
	project.EditCamera("a").Position.Set(0.0, Vec3{ 0.0f, 1.0f, 0.0f });
	project.EditCamera("b").Position.Set(0.0, Vec3{ 0.0f, 2.0f, 0.0f });
	BeatCutDesc cuts;
	cuts.Times = { 0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0 };
	cuts.Every = 2;
	cuts.Sources = { "a", "b" };
	SWIM_CHECK(CutsToBeats(project, cuts) == 3);
	SWIM_REQUIRE(project.Shots.size() == 4); // The follow shot past the last cut is trimmed, not removed.
	SWIM_CHECK(project.Shots[0].Camera == "a" && project.Shots[1].Camera == "b" && project.Shots[2].Camera == "a");
	SWIM_CHECK(project.Shots[3].Name == "ride" && project.Shots[3].Start == 3.0);
	SWIM_CHECK(project.Shots[1].Start == 1.0 && project.Shots[1].End == 2.0);
	player.Seek(1.5);
	SWIM_CHECK(player.GetView().Camera.Camera == "b");

	project.AddMarker(0.25, "hit");
	project.AddJump({ "j", 3.5, 0.75 });
	const auto targets = CollectSnapTargets(project);
	SWIM_CHECK(std::find(targets.begin(), targets.end(), 0.25) != targets.end());
	SWIM_CHECK(std::find(targets.begin(), targets.end(), 0.75) != targets.end());
	SWIM_CHECK(std::is_sorted(targets.begin(), targets.end()));

	LightDesc light;
	light.Type = "spot";
	light.Position = { 1.0f, 2.0f, 3.0f };
	const NodeId id = AddLight(project, light).Node;
	player.Refresh();
	const NodeState* node = player.FindNode(id);
	SWIM_REQUIRE(node != nullptr);
	SWIM_CHECK(player.GetNames().Get(node->Kind) == "light");
	SWIM_CHECK(Near(player.WorldPose(id).Position, { 1.0f, 2.0f, 3.0f }));

	SWIM_CHECK(!ApplyLightingPreset(project, "nope", 0.0));
	SWIM_CHECK(ApplyLightingPreset(project, "noir", 0.0) && ApplyLightingPreset(project, "neutral", 2.0));
	SWIM_CHECK(std::get<float>(*project.Environment.at("grade.saturation").Evaluate(0.0)) == 0.0f);
	SWIM_CHECK(std::get<float>(*project.Environment.at("grade.saturation").Evaluate(2.0)) == 1.0f);
	SWIM_CHECK(LightingPresets().size() == 6);
}

SWIM_TEST("Replay.Editing", "ToolsDriveShotsJumpsMusicAndSync")
{
	TempFile file("swim-replay-edit-tools.swrp");
	RecordMover(file.Path);
	ReplayPlayer player;
	SWIM_REQUIRE(player.Open(file.Path));
	Swim::Tooling::ToolServer server;
	ReplayToolsDesc desc;
	desc.Prefix = "replay.";
	int loads = 0;
	desc.LoadMusic = [&](const std::filesystem::path&, std::vector<float>& mono, double& rate, std::string&)
	{
		++loads;
		rate = 22050.0;
		mono = ClickTrack(rate, 16.0, 120.0);
		return true;
	};
	RegisterReplayTools(server, player, desc);

	const auto call = [&](const char* name, const char* args)
	{
		return server.Call(name, Json::Parse(args));
	};

	call("replay.marker.add", R"({"time":1.0,"name":"in"})");
	call("replay.marker.add", R"({"time":3.0,"name":"out"})");
	const Json jump = call("replay.jump.add", R"({"from":"in","to":"out"})");
	SWIM_CHECK(jump["from"].AsNumber() == 1.0 && jump["to"].AsNumber() == 3.0);
	SWIM_CHECK(call("replay.jump.list", "{}")["jumps"].Size() == 1);
	SWIM_CHECK_THROWS(call("replay.jump.add", R"({"from":"missing","to":2})"), Swim::Tooling::ToolError);

	call("replay.path.set", R"({"name":"p","points":[[0,0,0],[4,0,0],[8,2,0]]})");
	SWIM_CHECK(call("replay.path.get", R"({"name":"p"})")["polyline"].Size() > 3);
	SWIM_CHECK(call("replay.path.sample", R"({"name":"p","count":5})")["samples"].Size() == 5);
	const Json shot = call("replay.shot.create", R"({"name":"o","preset":"orbit","start":0,"end":2,"target":"10"})");
	SWIM_CHECK(shot["shot"]["path"].AsString() == "shot:o");
	call("replay.shot.set", R"({"name":"fly","path":"p","start":2,"end":3})");
	SWIM_CHECK(call("replay.shot.list", "{}")["shots"].Size() == 2);

	call("replay.music.set", R"({"path":"song.wav","offset":0.5})");
	const Json analysis = call("replay.music.analyze", "{}");
	SWIM_CHECK_NEAR(analysis["bpm"].AsNumber(), 120.0, 2.0);
	SWIM_CHECK(analysis["sections"].IsArray());
	const auto& markers = player.GetProject().Markers;
	const auto downbeats = std::count_if(markers.begin(), markers.end(),
		[](const ReplayMarker& m)
		{
			return m.Kind == "downbeat";
		});
	SWIM_CHECK(downbeats >= 6);
	const Json peaks = call("replay.music.peaks", R"({"bucketsPerSecond":10})");
	SWIM_CHECK(peaks["max"].Size() == 160 && peaks["start"].AsNumber() == 0.5);
	SWIM_CHECK(loads == 1); // Decoded once.

	call("replay.camera.key", R"({"camera":"x","time":0,"position":[0,1,0]})");
	call("replay.camera.key", R"({"camera":"y","time":0,"position":[0,2,0]})");
	const Json cuts = call("replay.sync.cuts_to_beats", R"({"sources":["x","y"],"on":"downbeat","from":4,"to":12})");
	SWIM_CHECK(cuts["shots"].AsInt() >= 3);
	const Json snapped = call("replay.snap", R"({"time":4.27,"tolerance":0.1,"kinds":["beat"]})");
	SWIM_CHECK(snapped["snapped"].AsBool());

	call("replay.time.scale", R"({"keys":[[0,0.5]]})");
	SWIM_CHECK_NEAR(player.GetLength(), 8.0, 1.0e-3);
	call("replay.time.scale", R"({"clear":true})");
	SWIM_CHECK_NEAR(player.GetLength(), 4.0, 1.0e-3);

	const Json light = call("replay.light.add", R"({"type":"point","position":[0,3,0],"color":[1,0.5,0.2]})");
	SWIM_CHECK(!light["node"].AsString().empty());
	call("replay.light.preset", R"({"preset":"golden_hour","time":0})");
	call("replay.grade.key", R"({"time":1,"exposure":-1,"slope":[1,0.9,0.8]})");
	SWIM_CHECK(player.GetProject().Environment.contains("grade.slope"));
	SWIM_CHECK_THROWS(call("replay.light.preset", R"({"preset":"bogus"})"), Swim::Tooling::ToolError);
	SWIM_CHECK(player.GetProject().CanUndo());
}

namespace
{

	bool Finite(const Vec3& v)
	{
		return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
	}

	bool FinitePose(const CameraPose& pose)
	{
		return Finite(pose.Position) && std::isfinite(pose.Rotation[0]) && std::isfinite(pose.Rotation[1]) &&
			std::isfinite(pose.Rotation[2]) && std::isfinite(pose.Rotation[3]) && pose.Fov >= 1.0f && pose.Fov <= 170.0f;
	}

} // namespace

SWIM_TEST("Replay.Path", "DegeneratePathsStayFinite")
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const std::vector<std::vector<PathPoint>> cases{
		{}, { { { 1.0f, 2.0f, 3.0f } } }, { { { 1.0f, 2.0f, 3.0f } }, { { 1.0f, 2.0f, 3.0f } } },					// Zero length.
		{ { { 0.0f, 0.0f, 0.0f } }, { { 0.0f, 0.0f, 0.0f } }, { { 0.0f, 0.0f, 0.0f } }, { { 4.0f, 0.0f, 0.0f } } }, // Duplicates.
		{ { { 0.0f, 0.0f, 0.0f } }, { { nan, 0.0f, 0.0f } }, { { 4.0f, 0.0f, 0.0f } } },							// A broken point.
	};

	for (const PathKind kind : { PathKind::Linear, PathKind::CatmullRom, PathKind::Centripetal, PathKind::Bezier })
	{
		for (const bool closed : { false, true })
		{
			for (const auto& points : cases)
			{
				CameraPath path;
				path.Kind = kind;
				path.Closed = closed;
				path.Points = points;

				if (kind == PathKind::Bezier && !points.empty())
				{
					path.AutoTangents();
				}

				SWIM_CHECK(std::isfinite(path.Length()));

				for (const float f : { 0.0f, 0.3f, 1.0f, -2.0f, 5.0f, nan, std::numeric_limits<float>::infinity() })
				{
					SWIM_CHECK(Finite(path.Sample(f)));
					SWIM_CHECK(Finite(path.Direction(f)));
					SWIM_CHECK(Finite(path.Evaluate(f)));
				}

				for (const Vec3& p : path.Polyline(4))
				{
					SWIM_CHECK(Finite(p));
				}

				SWIM_CHECK(std::isfinite(path.Nearest({ 1.0f, 1.0f, 1.0f })));
				SWIM_CHECK(std::isfinite(path.Nearest({ nan, 0.0f, 0.0f })));
			}
		}
	}

	// Points edited without Invalidate: the cached table follows them.
	CameraPath path;
	path.Kind = PathKind::Linear;
	path.Points = { { { 0.0f, 0.0f, 0.0f } }, { { 10.0f, 0.0f, 0.0f } } };
	SWIM_CHECK_NEAR(path.Length(), 10.0f, 1.0e-3f);
	path.Points.push_back({ { 10.0f, 10.0f, 0.0f } });
	SWIM_CHECK_NEAR(path.Length(), 20.0f, 1.0e-3f);
	path.Points.clear();
	SWIM_CHECK(path.Length() == 0.0f && Finite(path.Sample(0.5f)));
}

SWIM_TEST("Replay.Player", "BrokenCameraEditsNeverProduceANonFiniteCamera")
{
	TempFile file("swim-replay-broken-camera.swrp");
	RecordMover(file.Path);
	ReplayPlayer player;
	SWIM_REQUIRE(player.Open(file.Path));
	ReplayProject& project = player.GetProject();
	const float nan = std::numeric_limits<float>::quiet_NaN();

	// A rig on a path that is then removed, a single-point path, a NaN key, a zero fov, a
	// look-at target on the eye, and a shot over a missing path.
	CameraPath& one = project.EditPath("one");
	one.Points = { { { 1.0f, 2.0f, 3.0f } } };
	CameraRig& rig = project.EditCamera("rig");
	rig.Path = "gone";
	rig.Progress.Set(0.0, 0.5f);
	project.EditPath("gone").Points = { { { 0.0f, 0.0f, 0.0f } }, { { 5.0f, 0.0f, 0.0f } } };
	project.SetCut(0.0, "rig");
	player.Seek(1.0);
	SWIM_CHECK(FinitePose(player.GetView().Camera));
	project.Paths.erase("gone");
	player.Refresh();
	SWIM_CHECK(FinitePose(player.GetView().Camera));

	rig.Path = "one";
	rig.Aim = CameraAim::Direction;
	player.Refresh();
	SWIM_CHECK(FinitePose(player.GetView().Camera));
	SWIM_CHECK(Near(player.GetView().Camera.Position, { 1.0f, 2.0f, 3.0f }));

	rig.Path.clear();
	rig.Position.Set(0.0, Vec3{ nan, 0.0f, 0.0f });
	rig.Fov.Set(0.0, 0.0f);
	player.Refresh();
	SWIM_CHECK(FinitePose(player.GetView().Camera));

	rig.Position = {};
	rig.Position.Set(0.0, Vec3{ 2.0f, 2.0f, 2.0f });
	rig.Aim = CameraAim::Point;
	rig.LookAt.Set(0.0, Vec3{ 2.0f, 2.0f, 2.0f });
	player.Refresh();
	SWIM_CHECK(FinitePose(player.GetView().Camera));

	project.SetShot({ "s", "", 0.0, 4.0, "missing" });
	player.Refresh();
	SWIM_CHECK(FinitePose(player.GetView().Camera));

	// Non-finite times are refused instead of corrupting the sorted lists.
	const auto shots = project.Shots.size();
	project.SetShot({ "bad", "rig", nan, 1.0 });
	project.AddMarker(nan, "m");
	project.AddJump({ "j", nan, 1.0 });
	project.SetCut(nan, "rig");
	rig.Fov.Set(nan, 50.0f);
	SWIM_CHECK(project.Shots.size() == shots && project.Markers.empty() && project.Jumps.empty());
	SWIM_CHECK(rig.Fov.Keys.size() == 1);
}
