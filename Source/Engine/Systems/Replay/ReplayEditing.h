#pragma once

#include "Engine/Systems/Replay/ReplayAudio.h"
#include "Engine/Systems/Replay/ReplayProject.h"

#include <string>
#include <string_view>
#include <vector>

namespace Swim::Replay
{

	// High-level edits built from the project's primitives (rigs, paths, shots, markers,
	// environment and added nodes). Each returns what it created so tools can report it.

	enum class ShotPreset : std::uint8_t
	{
		Static,		// A fixed camera looking at the target.
		Orbit,		// Circles the target (Degrees of sweep from StartAngle).
		Dolly,		// Moves straight toward (or away from) the target.
		Follow,		// Rides behind the target at Offset (in the target's frame), looking at it.
		Flythrough, // Flies along Points, looking where it goes.
	};

	const char* ToString(ShotPreset preset);

	bool ParseShotPreset(std::string_view text, ShotPreset& preset);

	struct ShotDesc
	{
		std::string Name;
		ShotPreset Preset = ShotPreset::Orbit;
		double Start = 0.0;
		double End = 4.0;
		NodeId Target = NoNode;			// The subject; Point when none.
		Vec3 Point{ 0.0f, 0.0f, 0.0f }; // World target without a node.
		Vec3 TargetOffset{ 0.0f, 1.0f, 0.0f };
		float Radius = 6.0f;			 // Orbit radius, static and dolly start distance.
		float EndRadius = 2.5f;			 // Dolly end distance.
		float Height = 2.0f;			 // Above the target.
		float StartAngle = 0.0f;		 // Degrees around +Y (0 = +Z side of the target).
		float Degrees = 180.0f;			 // Orbit sweep (negative: clockwise).
		Vec3 Offset{ 0.0f, 2.0f, 5.0f }; // Follow: camera position in the target's frame.
		std::vector<Vec3> Points;		 // Flythrough path (world).
		float Fov = 0.0f;				 // 0: keep the recorded field of view.
		Ease Motion = Ease::Smooth;		 // Progress along the move.
	};

	// Creates the rig "shot:<name>" (and the path "shot:<name>" for moving presets) and
	// sets the shot. Replaces a shot, rig and path of the same name.
	const Shot& CreateShot(ReplayProject& project, const ShotDesc& desc);

	struct BeatCutDesc
	{
		std::vector<double> Times; // Cut candidates (beats, downbeats, section starts...).
		std::uint32_t Every = 1;   // Cut on every n-th time.
		double From = 0.0;
		double To = 1.0e30;
		// Rotated through for consecutive shots: shot names (copied as templates: camera,
		// path and progress) or, when no shot has the name, rig names.
		std::vector<std::string> Sources;
		std::string Prefix = "cut";
	};

	// Replaces the shots in [From, To) with consecutive shots cut at the chosen times.
	// Returns how many shots were created.
	std::size_t CutsToBeats(ReplayProject& project, const BeatCutDesc& desc);

	// Every time a cursor may snap to: markers, shot edges, cuts and jump keys.
	std::vector<double> CollectSnapTargets(const ReplayProject& project);

	struct LightDesc
	{
		std::string Type = "point"; // "point", "spot", "directional".
		Vec3 Color{ 1.0f, 1.0f, 1.0f };
		float Intensity = 10.0f;
		float Range = 10.0f;
		float InnerCone = 0.35f;
		float OuterCone = 0.6f;
		bool CastShadows = false;
		Vec3 Position{ 0.0f, 0.0f, 0.0f };
		Quat Rotation = Animation::IdentityQuat;
		NodeId AttachTo = NoNode; // Follow a recorded node (pose relative to it).
		std::string Label;
	};

	// An added "light" node (keyed at time 0; edit its tracks to animate it).
	NodeEdit& AddLight(ReplayProject& project, const LightDesc& desc);

	// Names of the lighting presets ("golden_hour", "night", "noir", "studio", "neon", "neutral").
	const std::vector<std::string>& LightingPresets();

	// Keys exposure and grading environment values for a preset at a time. False for an
	// unknown preset.
	bool ApplyLightingPreset(ReplayProject& project, std::string_view preset, double time, Ease ease = Ease::Smooth);

	// Retimes a song region so chosen moments land on beats: over the timeline range
	// [From, To] (where the music plays) the time scale is replaced by a fitted curve
	// that shows the content currently between From and ContentTo, passing each event
	// exactly at its beat. Before From nothing moves; after To the old speed resumes.
	struct RegionSyncDesc
	{
		double From = 0.0;
		double To = 0.0;
		double ContentTo = -1.0; // Current timeline time the region ends on (not after From: To).
		// (beat timeline time, event current timeline time) to land exactly; out-of-order
		// pairs are dropped.
		std::vector<std::pair<double, double>> Pairs;
		// Without Pairs: each event (current timeline) lands on the nearest beat within
		// MaxShift seconds of where an even retime would put it.
		std::vector<double> Beats;
		std::vector<double> Events;
		double MaxShift = 0.5;
		bool Smooth = true; // Linear speed ramps through the anchors; false: constant speed between them.
		float MinScale = 0.05f;
		float MaxScale = 8.0f;
		std::string RetimeMarkerKind; // Markers of this kind in the content move with it ("" none).
	};

	struct RegionSyncResult
	{
		std::vector<std::pair<double, double>> Anchors; // (timeline time, recording time), ends included.
		std::size_t Dropped = 0;						// Pairs or matches left out (order or speed range).
		float MinSpeed = 0.0f;							// Mean speeds between anchors.
		float MaxSpeed = 0.0f;
		double MaxError = 0.0; // Largest anchor miss after baking (recording seconds).
	};

	// Throws std::invalid_argument for an empty region or one the speed range cannot fit.
	RegionSyncResult SyncRegion(ReplayProject& project, const RegionSyncDesc& desc, double recordingLength);

	enum class TimeScaleStyle : std::uint8_t
	{
		Calm,	// Speed follows the music's energy, slow in quiet passages (smooth, gentle).
		Punchy, // Real time with slow-motion hits on the strongest onsets and section starts.
	};

	const char* ToString(TimeScaleStyle style);

	bool ParseTimeScaleStyle(std::string_view text, TimeScaleStyle& style);

	struct TimeScaleSuggestDesc
	{
		double From = 0.0; // Timeline range.
		double To = 0.0;
		TimeScaleStyle Style = TimeScaleStyle::Calm;
		const OnsetAnalysis* Onsets = nullptr; // Music seconds.
		double MusicOffset = 0.0;			   // Timeline time of music second 0.
		std::vector<double> Beats;			   // Timeline times.
		std::vector<double> Sections;		   // Timeline times.
		float Low = 0.0f;					   // Slowest speed (0: the style's default).
		float High = 0.0f;					   // Fastest speed (0: the style's default).
	};

	// Time scale keys for the range (timeline times; apply with Track::Set and bake).
	std::vector<TrackKey<float>> SuggestTimeScale(const TimeScaleSuggestDesc& desc);

} // namespace Swim::Replay
