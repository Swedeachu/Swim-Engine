#pragma once

#include "Engine/Systems/Replay/ReplayPlayer.h"
#include "Engine/Systems/Replay/ReplayPreview.h"
#include "Engine/Systems/Replay/ReplaySequence.h"
#include "Engine/Systems/Tooling/ToolServer.h"

#include <functional>

namespace Swim::Replay
{

	struct ReplayToolsDesc
	{
		std::string Prefix; // Prepended to every tool name (for example "replay.").
		// Opens a recording (the game resets its presenter). Empty: player.Open.
		std::function<bool(const std::filesystem::path& path, std::string& error)> OpenRecording;
		// Where project.save writes without a path argument.
		std::filesystem::path ProjectPath;
		// Root that relative recording/project paths resolve against.
		std::filesystem::path Root;
		// Decodes a music file to mono samples for music.analyze and music.peaks (the engine's
		// Engine::DecodeAudioFile in Runtime/ReplayMusic.h fits). Unset: those tools fail.
		std::function<bool(const std::filesystem::path& path, std::vector<float>& mono, double& sampleRate, std::string& error)> LoadMusic;
		// Renders frames for preview.render and preview.contact_sheet (the host renders the
		// scene offscreen at the request's time from its camera). Unset: those tools fail.
		PreviewRenderer RenderPreview;
		// Where preview PNGs go without a path argument (default: Root/previews, or the
		// temp directory without a Root).
		std::filesystem::path PreviewDirectory;
		// The multi-replay sequence the sequence.* tools edit and play (unset: no sequence tools).
		SequencePlayer* Sequence = nullptr;
		// Where sequence.save writes without a path argument.
		std::filesystem::path SequencePath;
	};

	// Registers the replay tools on a tool server: session and transport, scene
	// inspection, node edits, added nodes, director cameras and cuts, environment,
	// markers, time remap, paths, shots and presets, jump keys, time scale, music
	// analysis and beat sync, lights and grading, project save/load and undo/redo, offscreen
	// previews and contact sheets, drawing data (paths, camera frusta, keys) and, with a
	// SequencePlayer, multi-replay sequences. Every editing tool is one undo step. Times
	// are timeline seconds and default to the current time.
	void RegisterReplayTools(Tooling::ToolServer& server, ReplayPlayer& player, ReplayToolsDesc desc = {});

	namespace Internal
	{

		void RegisterStudioTools(Tooling::ToolServer& server, ReplayPlayer& player, const ReplayToolsDesc& desc);

	} // namespace Internal

	// JSON description of a node in the player's current view.
	Tooling::Json DescribeNode(ReplayPlayer& player, const NodeState& node, bool withProps);

} // namespace Swim::Replay
