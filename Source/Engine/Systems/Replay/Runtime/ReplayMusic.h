#pragma once

#include "Engine/Systems/Audio/AudioSystem.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Swim::Replay
{
	class ReplayPlayer;
}

namespace Engine
{

	// A decoded audio file for analysis (peaks, onsets, beats, sections).
	struct DecodedAudio
	{
		std::vector<float> Mono;
		double SampleRate = 0.0;
		std::uint32_t Channels = 0;
		double Duration = 0.0;
	};

	// wav, flac, mp3 and ogg (nothing when the engine is built without audio).
	std::optional<DecodedAudio> DecodeAudioFile(const std::filesystem::path& path, std::string* error = nullptr);

	struct ReplayMusicDesc
	{
		AudioSystem* Audio = nullptr;
		BusHandle Bus = AudioBus::Music;
		// Maps a project music path to a file (relative to the project, a content root...).
		// Unset: used as given.
		std::function<std::filesystem::path(const std::string&)> ResolvePath;
	};

	// Plays the project's music track in step with a player's timeline: the song is at
	// (timeline time - Offset), so with the player at 1x the output is in sync for screen
	// capture. Seeks, steps and jump keys restart it at the new position, the player's
	// speed is its pitch, and it is silent while paused, before Offset, past the end and at
	// speeds outside (0, 4].
	class ReplayMusic
	{
	  public:
		explicit ReplayMusic(ReplayMusicDesc desc);

		~ReplayMusic();

		ReplayMusic(const ReplayMusic&) = delete;
		ReplayMusic& operator=(const ReplayMusic&) = delete;

		// Call once per frame after the player's Update.
		void Sync(const Swim::Replay::ReplayPlayer& player);

		void Stop();

		bool IsPlaying() const;

		// Extra gain on top of the track's volume (a mute toggle, a mixer slider).
		void SetGain(float gain);

		// Decodes once per path (cached) for analysis; nullptr if the file cannot be read.
		const DecodedAudio* Load(const std::string& path, std::string* error = nullptr);

		void ForgetLoaded() { loaded.clear(); }

	  private:
		std::filesystem::path Resolve(const std::string& path) const;

		ReplayMusicDesc desc;
		VoiceHandle voice{};
		std::string playingPath;
		std::uint64_t discontinuities = 0;
		double speed = 1.0;
		float volume = 1.0f;
		float gain = 1.0f;
		std::map<std::string, std::unique_ptr<DecodedAudio>> loaded;
	};

} // namespace Engine
