#pragma once

// Shared by the Replay*Tools.cpp files that register the replay tools.

#include "Engine/Systems/Replay/ReplayAudio.h"
#include "Engine/Systems/Replay/ReplayEditing.h"
#include "Engine/Systems/Replay/ReplayTools.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Swim::Replay::Internal
{

	using Tooling::Json;
	using Tooling::ToolError;
	namespace Schema = Tooling::Schema;

	struct Context
	{
		Tooling::ToolServer* Server = nullptr;
		ReplayPlayer* Player = nullptr;
		ReplayToolsDesc Desc;
	};

	NodeId RequireNode(const Json& args, const char* field = "node");

	NodeId OptionalNode(const Json& args, const char* field);

	Ease ReadEase(const Json& args);

	double ReadTime(const Json& args, const ReplayPlayer& player);

	std::optional<Quat> ReadRotation(const Json& args);

	std::optional<Vec3> ReadVec3(const Json& args, const char* field);

	AssetRef ReadAssetRef(const Json& j);

	Json Ok(ReplayPlayer& player);

	std::filesystem::path Resolve(const Context& c, const std::string& path);

	Json TimeSchema();

	Json EaseSchema();

	Json NodeSchema(std::string_view what = "Node id (string).");

	template <typename T> Json KeyCount(const Track<T>& track)
	{
		return static_cast<std::uint64_t>(track.Keys.size());
	}

	std::vector<double> MarkerTimes(const ReplayProject& project, std::string_view kind, std::string_view name = {});

	// A time given as seconds or as a marker name.
	double ReadTimeOrMarker(const Json& args, const char* field, const ReplayProject& project);

	std::vector<Vec3> ReadPoints(const Json& args, const char* field);

	Json Times(const std::vector<double>& times, double offset = 0.0);

	Json PathSummary(const std::string& name, const CameraPath& path);

	Json ShotJson(const Shot& shot);

	Json EaseOrSmoothSchema();

	Ease ReadEaseOr(const Json& args, Ease fallback);

	// Decoded music and what was found in it, kept for the current path.
	struct MusicCache
	{
		std::string Path;
		std::vector<float> Mono;
		double Rate = 0.0;
		std::optional<OnsetAnalysis> Onsets;
		std::optional<BeatGrid> Beats;
		std::vector<double> Sections;
	};

	MusicCache& LoadMusic(Context& c, MusicCache& cache);

	using AddTool = std::function<void(std::string, std::string, Json, std::function<Json(const Json&)>)>;
	// Runs a change as one undo step, then re-evaluates the view.
	using EditProject = std::function<void(const std::function<void(ReplayProject&)>&)>;

	// Session and transport, and scene inspection.
	void RegisterSessionTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit);

	// Node edits and added nodes.
	void RegisterNodeTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit);

	// Director cameras and cuts.
	void RegisterCameraTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit);

	// Environment, markers, time remap, and the project document with undo/redo.
	void RegisterTimelineTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit);

	// Paths, camera paths and aim, and shots.
	void RegisterPathTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit);

	// Timeline jump keys and time scale.
	void RegisterTimeTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit);

	// Music analysis and beat sync.
	void RegisterMusicTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit);

	// Lights and grading.
	void RegisterLightingTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit);

} // namespace Swim::Replay::Internal
