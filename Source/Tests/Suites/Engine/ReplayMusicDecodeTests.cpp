#include "Engine/Systems/Replay/ReplayAudio.h"
#include "Engine/Systems/Replay/Runtime/ReplayMusic.h"
#include "Tests/Fixtures/Mp3ClickFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <filesystem>
#include <fstream>

SWIM_TEST("Replay.Music", "Mp3DecodesAndItsBeatsAreFound")
{
	const auto path = std::filesystem::temp_directory_path() / "swim-replay-click.mp3";
	{
		const auto bytes = Swim::Testing::Mp3ClickFixture();
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}

	std::string error;
	const auto audio = Engine::DecodeAudioFile(path, &error);
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
	SWIM_REQUIRE_MESSAGE(audio.has_value(), error);
	SWIM_CHECK_NEAR(audio->SampleRate, 16000.0, 1e-9);
	SWIM_CHECK(audio->Channels == 1);
	SWIM_CHECK(std::abs(audio->Duration - 3.0) < 0.1); // MP3 framing pads a little.

	const auto onsets = Swim::Replay::DetectOnsets(audio->Mono, audio->SampleRate);
	const auto beats = Swim::Replay::EstimateBeats(onsets);
	SWIM_CHECK(onsets.Onsets.size() >= 5);
	SWIM_CHECK_NEAR(beats.Bpm, 120.0, 3.0);
}
