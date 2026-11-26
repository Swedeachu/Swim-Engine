#include "Engine/Systems/Replay/ReplayPlayer.h"
#include "Engine/Systems/Replay/ReplayRecorder.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>

using namespace Swim::Replay;

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

	// A scripted scene: a mover that slides along +X at 1 m/s, a static prop, a light
	// that exists only between t = 1 and t = 2, a camera, and a sound every second.
	class ScriptedSource : public IReplayCaptureSource
	{

	  public:
		std::vector<std::uint8_t> RuntimeMesh{ 1, 2, 3, 4, 5, 6, 7, 8, 9 };

		void Capture(ReplayCapture& capture) override
		{
			const double t = capture.GetTime();
			NodeState& mover = capture.Node(10);
			mover.Kind = capture.Name("mesh");
			mover.Pose.Position = { static_cast<float>(t), 0.0f, 0.0f };
			mover.Assets = { capture.Asset(AssetKind::Mesh, "pack/mover"), capture.Asset(AssetKind::Mesh, "runtime/chunk", RuntimeMesh) };
			mover.Set(capture.Name("tint"), Vec3{ static_cast<float>(t), 0.0f, 0.0f });

			if (first)
			{
				NodeState& prop = capture.Node(20);
				prop.Kind = capture.Name("mesh");
				prop.Pose.Position = { 5.0f, 0.0f, 5.0f };
				// The same bytes under another key are stored once.
				prop.Assets = { capture.Asset(AssetKind::Mesh, "runtime/copy", RuntimeMesh) };
				first = false;
			}
			else
			{
				capture.Keep(20);
			}

			if (t >= 1.0 && t < 2.0)
			{
				NodeState& light = capture.Node(30);
				light.Kind = capture.Name("light");
				light.Set(capture.Name("intensity"), 4.0f);
			}

			NodeState& camera = capture.Node(1);
			camera.Kind = capture.Name("camera");
			camera.Pose.Position = { 0.0f, 2.0f, 10.0f };
			camera.Set(capture.Name("fov"), 70.0f);
			camera.Set(capture.Name("active"), true);

			if (std::abs(t - std::round(t)) < 1.0e-6 && t > 0.0)
			{
				ReplayEvent sound;
				sound.Kind = capture.Name("sound");
				sound.Node = 10;
				sound.Asset = capture.Asset(AssetKind::Sound, "sounds/step");
				capture.Event(std::move(sound));
			}
		}

	  private:
		bool first = true;
	};

	// Records 0..3 s at 10 frames per second.
	ReplayRecorder::Stats Record(const std::filesystem::path& path, double keyframeInterval = 1.0)
	{
		ScriptedSource source;
		ReplayRecorder recorder;
		ReplayRecorderDesc desc;
		desc.KeyframeInterval = keyframeInterval;
		desc.Metadata = "{\"title\":\"test\"}";
		desc.IsResolvable = [](const AssetRecord& asset)
		{
			return asset.Key.starts_with("pack/") || asset.Kind == AssetKind::Sound;
		};
		SWIM_REQUIRE(recorder.Start(path, desc));
		recorder.AddSource(source);

		for (int frame = 0; frame <= 30; ++frame)
		{
			recorder.Capture(frame * 0.1);
		}

		SWIM_REQUIRE(recorder.Stop());
		return recorder.GetStats();
	}

	bool Near(const Vec3& a, const Vec3& b, float tolerance = 1.0e-4f)
	{
		return std::abs(a[0] - b[0]) <= tolerance && std::abs(a[1] - b[1]) <= tolerance && std::abs(a[2] - b[2]) <= tolerance;
	}

	const NodeState* Find(const std::vector<NodeState>& nodes, NodeId id)
	{
		for (const auto& node : nodes)
		{
			if (node.Id == id)
			{
				return &node;
			}
		}

		return nullptr;
	}

} // namespace

SWIM_TEST("Replay.Recording", "KeyframesDeltasAndAssetsRoundTrip")
{
	TempFile file("swim-replay-roundtrip.swrp");
	const auto stats = Record(file.Path);
	SWIM_CHECK(stats.Frames == 31);
	SWIM_CHECK(stats.Keyframes == 4);	 // t = 0, 1, 2, 3.
	SWIM_CHECK(stats.CachedAssets == 1); // One runtime mesh, though registered under two keys.

	ReplayReader reader;
	SWIM_REQUIRE(reader.Open(file.Path));
	SWIM_CHECK(!reader.WasRecovered());
	SWIM_CHECK(reader.GetMetadata() == "{\"title\":\"test\"}");
	SWIM_REQUIRE(reader.GetFrameCount() == 31);
	SWIM_CHECK(reader.IsKeyframe(0) && reader.IsKeyframe(10) && !reader.IsKeyframe(5));
	SWIM_CHECK(reader.FindFrame(1.25) == 12 && reader.FindKeyframe(12) == 10);

	std::vector<NodeState> state;
	SWIM_REQUIRE(reader.ReadState(15, state));
	const auto& names = reader.GetNames();
	const NodeState* mover = Find(state, 10);
	SWIM_REQUIRE(mover != nullptr);
	SWIM_CHECK(Near(mover->Pose.Position, { 1.5f, 0.0f, 0.0f }));
	SWIM_CHECK(names.Get(mover->Kind) == "mesh");
	SWIM_CHECK(Find(state, 20) != nullptr && Find(state, 30) != nullptr && Find(state, 1) != nullptr);

	// The light is removed by a delta after t = 2.
	SWIM_REQUIRE(reader.ReadState(25, state));
	SWIM_CHECK(Find(state, 30) == nullptr && Find(state, 20) != nullptr);

	// Key-only assets keep only their key; runtime data is cached once and loads back.
	const AssetRecord* pack = reader.GetAsset(Find(state, 10)->Assets[0]);
	const AssetRecord* chunk = reader.GetAsset(Find(state, 10)->Assets[1]);
	SWIM_REQUIRE(pack && chunk);
	SWIM_CHECK(pack->Key == "pack/mover" && !pack->HasData);
	SWIM_CHECK(chunk->HasData && chunk->Hash != 0);
	SWIM_CHECK(Find(state, 20)->Assets[0] == Find(state, 10)->Assets[1]);
	SWIM_CHECK(reader.LoadAssetData(Find(state, 10)->Assets[1]) == ScriptedSource().RuntimeMesh);

	// Events ride on their frame.
	std::vector<NodeState> rolling;
	std::vector<ReplayEvent> events;

	for (std::size_t frame = 0; frame < reader.GetFrameCount(); ++frame)
	{
		SWIM_REQUIRE(reader.ApplyFrame(frame, rolling, &events));
	}

	SWIM_CHECK(events.size() == 3);
	SWIM_CHECK(names.Get(events[0].Kind) == "sound" && std::abs(events[0].Time - 1.0) < 1.0e-9);
}

SWIM_TEST("Replay.Recording", "DeltasStayCompactAndAnUnfinishedFileIsRecovered")
{
	TempFile full("swim-replay-full.swrp");
	TempFile sparse("swim-replay-sparse.swrp");
	Record(full.Path, 0.0);	   // Every frame a keyframe.
	Record(sparse.Path, 10.0); // One keyframe, then deltas.
	// The index is the same size in both; frame data shrinks to the changed fields.
	SWIM_CHECK(std::filesystem::file_size(sparse.Path) * 10 < std::filesystem::file_size(full.Path) * 7);

	// Drop the index and footer and a torn tail, as after a crash.
	TempFile torn("swim-replay-torn.swrp");
	{
		std::ifstream in(sparse.Path, std::ios::binary);
		std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		ReplayReader indexed;
		SWIM_REQUIRE(indexed.Open(sparse.Path));
		SWIM_REQUIRE(indexed.GetFrameCount() == 31);
		indexed.Close();
		bytes.resize(bytes.size() / 2 + 3);
		std::ofstream out(torn.Path, std::ios::binary);
		out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}

	ReplayReader reader;
	SWIM_REQUIRE(reader.Open(torn.Path));
	SWIM_CHECK(reader.WasRecovered());
	SWIM_CHECK(reader.GetFrameCount() > 5 && reader.GetFrameCount() < 31);
	std::vector<NodeState> state;
	SWIM_CHECK(reader.ReadState(reader.GetFrameCount() - 1, state));
	SWIM_CHECK(Find(state, 10) != nullptr);
}

SWIM_TEST("Replay.Player", "InterpolatesSeeksAndFiresEvents")
{
	TempFile file("swim-replay-player.swrp");
	Record(file.Path);
	ReplayPlayer player;
	SWIM_REQUIRE(player.Open(file.Path));
	SWIM_CHECK(std::abs(player.GetLength() - 3.0) < 1.0e-9);

	// Between frames 12 and 13: positions and float/vector properties blend.
	player.Seek(1.25);
	const NodeState* mover = player.FindNode(10);
	SWIM_REQUIRE(mover != nullptr);
	SWIM_CHECK(Near(mover->Pose.Position, { 1.25f, 0.0f, 0.0f }));
	const Value* tint = mover->Find(*player.GetNames().Find("tint"));
	SWIM_REQUIRE(tint != nullptr);
	SWIM_CHECK(Near(std::get<Vec3>(*tint), { 1.25f, 0.0f, 0.0f }));
	SWIM_CHECK(player.GetView().Camera.Valid && std::abs(player.GetView().Camera.Fov - 70.0f) < 1.0e-4f);

	// Seeking back is exact too.
	player.Seek(0.35);
	SWIM_CHECK(Near(player.FindNode(10)->Pose.Position, { 0.35f, 0.0f, 0.0f }));
	SWIM_CHECK(player.FindNode(30) == nullptr);

	// Playing forward over t = 1 and 2 fires those sounds once each; seeking fires none.
	player.Seek(0.5);
	player.Play();
	std::size_t sounds = 0;

	for (int i = 0; i < 40; ++i)
	{
		sounds += player.Update(1.0 / 20.0).Events.size();
	}

	SWIM_CHECK(sounds == 2);
	SWIM_CHECK(std::abs(player.GetTime() - 2.5) < 1.0e-6);
	player.Seek(0.5);
	SWIM_CHECK(player.GetView().Events.empty());

	player.Step(3);
	SWIM_CHECK(!player.IsPlaying());
	SWIM_CHECK(std::abs(player.GetView().RecordingTime - 0.8) < 1.0e-6);
}

namespace
{

	struct CountingPresenter : IReplayPresenter
	{
		int Added = 0;
		int Updated = 0;
		int Removed = 0;
		int Events = 0;
		std::map<std::string, Value> Environment;

		void OnAdd(const NodeState&) override { ++Added; }

		void OnUpdate(const NodeState&, const NodeState&) override { ++Updated; }

		void OnRemove(const NodeState&) override { ++Removed; }

		void OnEvent(const ReplayEvent&) override { ++Events; }

		void OnEnvironment(const std::map<std::string, Value>& environment) override { Environment = environment; }
	};

} // namespace

SWIM_TEST("Replay.Player", "PresentsOnlyChanges")
{
	TempFile file("swim-replay-present.swrp");
	Record(file.Path);
	ReplayPlayer player;
	SWIM_REQUIRE(player.Open(file.Path));
	CountingPresenter presenter;
	player.Seek(0.5);
	player.Present(presenter);
	SWIM_CHECK(presenter.Added == 3 && presenter.Updated == 0); // Mover, prop, camera.

	player.Seek(1.5); // The mover moved and the light appeared.
	player.Present(presenter);
	SWIM_CHECK(presenter.Added == 4 && presenter.Updated == 1 && presenter.Removed == 0);

	player.Present(presenter); // Nothing changed.
	SWIM_CHECK(presenter.Updated == 1);

	player.Seek(2.5);
	player.Present(presenter);
	SWIM_CHECK(presenter.Removed == 1);

	player.GetProject().Environment["fog.end"].Set(0.0, Value(400.0f));
	player.Refresh();
	player.Present(presenter);
	SWIM_CHECK(presenter.Environment.contains("fog.end"));
}

SWIM_TEST("Replay.Player", "EditsCamerasCutsAndTimeRemap")
{
	TempFile file("swim-replay-edits.swrp");
	Record(file.Path);
	ReplayPlayer player;
	SWIM_REQUIRE(player.Open(file.Path));
	ReplayProject& project = player.GetProject();

	// Hide the prop from t = 1, lift the mover by an offset, override the light's intensity.
	project.EditNode(20).Visible.Set(1.0, false, Ease::Step);
	project.EditNode(10).Position.Set(0.0, Vec3{ 0.0f, 2.0f, 0.0f });
	project.EditNode(30).Props["intensity"].Set(0.0, Value(9.0f));
	project.EditNode(10).AssetSwaps[0] = AssetRef{ AssetKind::Mesh, "pack/other" };
	player.Seek(1.5);
	SWIM_CHECK(!player.FindNode(20)->Visible);
	SWIM_CHECK(Near(player.FindNode(10)->Pose.Position, { 1.5f, 2.0f, 0.0f }));
	SWIM_CHECK(std::get<float>(*player.FindNode(30)->Find(*player.GetNames().Find("intensity"))) == 9.0f);
	const AssetRecord* swapped = player.GetAsset(player.FindNode(10)->Assets[0]);
	SWIM_REQUIRE(swapped != nullptr);
	SWIM_CHECK(swapped->Key == "pack/other" && !swapped->HasData);
	player.Seek(0.5);
	SWIM_CHECK(player.FindNode(20)->Visible);

	// An added object follows the mover.
	NodeEdit& added = project.AddNode("mesh");
	added.Assets = { AssetRef{ AssetKind::Mesh, "pack/flag" } };
	added.AttachTo = 10;
	added.Position.Set(0.0, Vec3{ 0.0f, 1.0f, 0.0f });
	player.Seek(2.0);
	SWIM_CHECK(Near(player.WorldPose(added.Node).Position, { 2.0f, 3.0f, 0.0f }));

	// A director camera: keyed path, looking at the mover, cut in at t = 1.
	CameraRig& rig = project.EditCamera("orbit");
	rig.Position.Set(0.0, Vec3{ 0.0f, 5.0f, 10.0f }, Ease::Smooth);
	rig.Position.Set(2.0, Vec3{ 10.0f, 5.0f, 10.0f });
	rig.LookAtNode = 10;
	rig.Fov.Set(0.0, 40.0f);
	rig.FocusNode = 10;
	project.SetCut(1.0, "orbit");
	project.SetCut(2.5, "");

	player.Seek(0.5);
	SWIM_CHECK(player.GetView().Camera.Camera.empty()); // Recorded camera before the first cut.
	SWIM_CHECK(Near(player.GetView().Camera.Position, { 0.0f, 2.0f, 10.0f }));

	player.Seek(1.0);
	const CameraPose& pose = player.GetView().Camera;
	SWIM_CHECK(pose.Camera == "orbit" && pose.Fov == 40.0f);
	SWIM_CHECK(Near(pose.Position, { 5.0f, 5.0f, 10.0f })); // Smooth ease at the midpoint.
	// Looking at the mover: camera -Z points at it.
	const Vec3 forward = Swim::Animation::Rotate(pose.Rotation, { 0.0f, 0.0f, -1.0f });
	const Vec3 toMover = Swim::Animation::Sub(player.WorldPose(10).Position, pose.Position);
	const float length = Swim::Animation::Length(toMover);
	SWIM_CHECK(Near(forward, Swim::Animation::Scale(toMover, 1.0f / length), 1.0e-3f));
	SWIM_CHECK(std::abs(pose.FocusDistance - length) < 1.0e-3f);

	player.Seek(2.75);
	SWIM_CHECK(player.GetView().Camera.Camera.empty());

	// Slow motion: the first two timeline seconds cover one recorded second.
	project.TimeRemap = { { 0.0, 0.0 }, { 2.0, 1.0 } };
	player.Seek(1.0);
	SWIM_CHECK(std::abs(player.GetView().RecordingTime - 0.5) < 1.0e-9);
	SWIM_CHECK(std::abs(player.GetLength() - 4.0) < 1.0e-9);
}

SWIM_TEST("Replay.Project", "JsonRoundTripAndUndo")
{
	ReplayProject project;
	project.Recording = "match.swrp";
	project.EditNode(42).Visible.Set(1.0, false, Ease::Step);
	project.EditNode(42).Props["color"].Set(0.5, Value(Vec3{ 1.0f, 0.5f, 0.0f }), Ease::Smooth);
	project.EditNode(0xFFFFFFFFFFFFull).Position.Set(0.0, Vec3{ 1.0f, 2.0f, 3.0f });
	project.AddNode("light").Assets = { AssetRef{ AssetKind::Effect, "glow" } };
	CameraRig& rig = project.EditCamera("a");
	rig.Position.Set(0.0, Vec3{ 1.0f, 2.0f, 3.0f }, Ease::Spline);
	rig.Rotation.Set(0.0, EulerRotation(90.0f, -10.0f));
	rig.RelativeTo = 42;
	project.SetCut(0.0, "a");
	project.AddMarker(1.5, "beat");
	project.Environment["sun.elevation"].Set(0.0, Value(30.0f));
	project.TimeRemap = { { 0.0, 0.0 }, { 4.0, 2.0 } };

	const auto json = project.ToJson();
	const ReplayProject copy = ReplayProject::FromJson(Swim::Tooling::Json::Parse(json.Dump(2)));
	SWIM_CHECK(copy.ToJson() == json);
	SWIM_CHECK(copy.Nodes.contains(0xFFFFFFFFFFFFull)); // 64-bit ids survive (written as strings).
	SWIM_CHECK(copy.Cameras.at("a").RelativeTo == 42 && copy.Cameras.at("a").Position.Keys[0].Interp == Ease::Spline);
	SWIM_CHECK(copy.Nodes.at(42).Visible.Evaluate(2.0) == std::optional<bool>(false));

	project.Checkpoint();
	project.Markers.clear();
	project.Checkpoint();
	project.Cameras.clear();
	SWIM_CHECK(project.Undo() && !project.Cameras.empty() && project.Markers.empty());
	SWIM_CHECK(project.Undo() && project.Markers.size() == 1);
	SWIM_CHECK(!project.Undo());
	SWIM_CHECK(project.Redo() && project.Markers.empty() && !project.Cameras.empty());
}

SWIM_TEST("Replay.Track", "EasesStepsAndSplines")
{
	Track<float> track;
	SWIM_CHECK(!track.Evaluate(0.0));
	track.Set(0.0, 0.0f);
	track.Set(1.0, 10.0f, Ease::Step);
	track.Set(2.0, 20.0f, Ease::Spline);
	track.Set(3.0, 30.0f);
	track.Set(4.0, 40.0f);
	SWIM_CHECK(*track.Evaluate(-1.0) == 0.0f && *track.Evaluate(9.0) == 40.0f);
	SWIM_CHECK(std::abs(*track.Evaluate(0.5) - 5.0f) < 1.0e-5f);
	SWIM_CHECK(*track.Evaluate(1.5) == 10.0f);					  // Step holds.
	SWIM_CHECK(std::abs(*track.Evaluate(2.5) - 25.0f) < 1.0e-4f); // Spline through evenly spaced keys is linear.
	track.Set(1.0, 12.0f);										  // Replaces the key at the same time.
	SWIM_CHECK(track.Keys.size() == 5 && track.Keys[1].Data == 12.0f);
	SWIM_CHECK(track.Remove(1.02, 0.05) && track.Keys.size() == 4);
	SWIM_CHECK(std::abs(ApplyEase(Ease::Smooth, 0.25f) - 0.15625f) < 1.0e-6f);
	SWIM_CHECK(ApplyEase(Ease::In, 0.5f) == 0.25f && ApplyEase(Ease::Out, 0.5f) == 0.75f);

	Track<Quat> spin;
	spin.Set(0.0, Swim::Animation::IdentityQuat);
	spin.Set(1.0, Swim::Animation::FromAxisAngle({ 0.0f, 1.0f, 0.0f }, 3.14159265f / 2.0f));
	const Quat half = *spin.Evaluate(0.5);
	const Quat expected = Swim::Animation::FromAxisAngle({ 0.0f, 1.0f, 0.0f }, 3.14159265f / 4.0f);
	SWIM_CHECK(std::abs(half[1] - expected[1]) < 1.0e-5f && std::abs(half[3] - expected[3]) < 1.0e-5f);
}
