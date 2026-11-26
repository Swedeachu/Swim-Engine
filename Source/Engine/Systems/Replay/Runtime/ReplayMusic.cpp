#include "Engine/Systems/Replay/Runtime/ReplayMusic.h"

#include "Engine/Systems/Replay/ReplayAudio.h"
#include "Engine/Systems/Replay/ReplayPlayer.h"

#if SWIM_AUDIO_MINIAUDIO || SWIM_AUDIO_DECODE_MINIAUDIO
#include <miniaudio.h>
#endif

#include <algorithm>
#include <span>

namespace Engine
{

	std::optional<DecodedAudio> DecodeAudioFile(const std::filesystem::path& path, std::string* error)
	{
#if SWIM_AUDIO_MINIAUDIO || SWIM_AUDIO_DECODE_MINIAUDIO
		ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
		ma_decoder decoder;
#ifdef _WIN32
		const ma_result opened = ma_decoder_init_file_w(path.wstring().c_str(), &config, &decoder);
#else
		const ma_result opened = ma_decoder_init_file(path.string().c_str(), &config, &decoder);
#endif

		if (opened != MA_SUCCESS)
		{
			if (error)
			{
				*error = "cannot decode " + path.string() + ": " + ma_result_description(opened);
			}

			return std::nullopt;
		}

		const std::uint32_t channels = std::max<std::uint32_t>(decoder.outputChannels, 1);
		DecodedAudio audio;
		audio.SampleRate = static_cast<double>(decoder.outputSampleRate);
		audio.Channels = channels;
		std::vector<float> interleaved;
		std::vector<float> chunk(4096 * static_cast<std::size_t>(channels));

		while (true)
		{
			ma_uint64 read = 0;
			const ma_result result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), 4096, &read);
			interleaved.insert(interleaved.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(read * channels));

			if (result != MA_SUCCESS || read < 4096)
			{
				break;
			}
		}

		ma_decoder_uninit(&decoder);
		audio.Mono = Swim::Replay::MixToMono(interleaved, channels);
		audio.Duration = audio.SampleRate > 0.0 ? static_cast<double>(audio.Mono.size()) / audio.SampleRate : 0.0;
		return audio;
#else
		if (error)
		{
			*error = "the engine was built without audio decoding";
		}

		(void)path;
		return std::nullopt;
#endif
	}

	ReplayMusic::ReplayMusic(ReplayMusicDesc d) : desc(std::move(d))
	{
	}

	ReplayMusic::~ReplayMusic()
	{
		Stop();
	}

	std::filesystem::path ReplayMusic::Resolve(const std::string& path) const
	{
		return desc.ResolvePath ? desc.ResolvePath(path) : std::filesystem::path(path);
	}

	void ReplayMusic::Stop()
	{
		if (desc.Audio && static_cast<bool>(voice))
		{
			desc.Audio->Stop(voice);
		}

		voice = {};
		playingPath.clear();
	}

	bool ReplayMusic::IsPlaying() const
	{
		return desc.Audio && static_cast<bool>(voice) && desc.Audio->IsPlaying(voice);
	}

	void ReplayMusic::SetGain(float g)
	{
		gain = std::max(g, 0.0f);

		if (IsPlaying())
		{
			desc.Audio->SetVolume(voice, volume * gain);
		}
	}

	void ReplayMusic::Sync(const Swim::Replay::ReplayPlayer& player)
	{
		if (!desc.Audio)
		{
			return;
		}

		const auto& music = player.GetProject().Music;
		const double rate = player.GetSpeed();
		const double local = music ? player.GetTime() - music->Offset : 0.0;
		const std::uint64_t jumps = player.GetDiscontinuities();
		bool want = music && !music->Path.empty() && player.IsPlaying() && rate > 0.0 && rate <= 4.0 && local >= 0.0;

		if (want)
		{
			if (const auto found = loaded.find(music->Path); found != loaded.end() && found->second->Duration > 0.0)
			{
				want = local < found->second->Duration;
			}
		}

		const bool playing = IsPlaying();
		const bool resync = jumps != discontinuities;
		discontinuities = jumps;

		if (playing && (!want || resync || music->Path != playingPath))
		{
			Stop();
		}
		else if (!playing && (!want || resync))
		{
			voice = {}; // Ended or never started: free to start again.
		}

		if (!want)
		{
			return;
		}

		if (IsPlaying())
		{
			if (rate != speed)
			{
				desc.Audio->SetPitch(voice, static_cast<float>(rate));
				speed = rate;
			}

			if (music->Volume != volume)
			{
				volume = music->Volume;
				desc.Audio->SetVolume(voice, volume * gain);
			}

			return;
		}

		// Not started yet, finished or refused: (re)start at the timeline position. A voice
		// that ended on its own past the song's end is not restarted every frame.
		if (static_cast<bool>(voice) && playingPath == music->Path)
		{
			return;
		}

		PlayParams params;
		params.Bus = desc.Bus;
		params.Volume = music->Volume * gain;
		params.Pitch = static_cast<float>(rate);
		params.StartSeconds = static_cast<float>(local);
		params.Priority = 100;
		voice = desc.Audio->Play(Resolve(music->Path).string(), params);
		playingPath = music->Path;
		speed = rate;
		volume = music->Volume;
	}

	const DecodedAudio* ReplayMusic::Load(const std::string& path, std::string* error)
	{
		if (const auto found = loaded.find(path); found != loaded.end())
		{
			return found->second.get();
		}

		std::optional<DecodedAudio> audio = DecodeAudioFile(Resolve(path), error);

		if (!audio)
		{
			return nullptr;
		}

		return loaded.emplace(path, std::make_unique<DecodedAudio>(std::move(*audio))).first->second.get();
	}

} // namespace Engine
