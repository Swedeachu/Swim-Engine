#include "Engine/Systems/Audio/AudioSystemImpl.h"

namespace Engine
{

	using namespace AudioDetail;

	namespace
	{

		bool GlobMatch(std::string_view pattern, std::string_view text)
		{
			std::size_t p = 0;
			std::size_t t = 0;
			std::size_t star = std::string_view::npos;
			std::size_t mark = 0;

			while (t < text.size())
			{
				const auto lower = [](char c)
				{
					return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				};

				if (p < pattern.size() && (pattern[p] == '?' || lower(pattern[p]) == lower(text[t])))
				{
					++p;
					++t;
				}
				else if (p < pattern.size() && pattern[p] == '*')
				{
					star = p++;
					mark = t;
				}
				else if (star != std::string_view::npos)
				{
					p = star + 1;
					t = ++mark;
				}
				else
				{
					return false;
				}
			}

			while (p < pattern.size() && pattern[p] == '*')
			{
				++p;
			}

			return p == pattern.size();
		}

	} // namespace

	AudioSystem::AudioSystem(const AudioSystemDesc& desc, Swim::Commands::CommandRegistry* commands) : impl(std::make_unique<Impl>())
	{
		auto& I = *impl;
		I.desc = desc;
		I.desc.Channels = std::clamp<std::uint32_t>(desc.Channels, 1, 8);
		I.commands = commands;
		I.output = desc.Output;
		I.buses.push_back({ "master", UINT32_MAX, 1.0f, nullptr });

		if (commands)
		{
			I.maxVoices = &commands->AddInt("audio_max_voices", 64, 4, 256, "Voices that play at once; quieter, older ones are stolen first",
				Category, Swim::Commands::VariableFlags::Saved);
			I.device = &commands->AddString("audio_device", "", "Output device name (empty = the system default; see audio_devices)",
				Category, Swim::Commands::VariableFlags::Saved);
#if SWIM_AUDIO_MINIAUDIO
			I.wantedDevice = I.device->GetString();
#endif
		}

#if SWIM_AUDIO_MINIAUDIO
		if (I.output != AudioOutput::Off)
		{
			if (!I.StartBackend())
			{
				std::cerr << "[Audio] The audio engine could not start; audio is off.\n";
				I.StopBackend();
				I.output = AudioOutput::Off;
			}
		}
#else
		I.output = AudioOutput::Off;
#endif

		for (const char* name : { "music", "world", "players", "ambient", "ui" })
		{
			AddBus(name, AudioBus::Master);
		}

		if (commands)
		{
			RegisterCommands(*commands);
		}

#if SWIM_AUDIO_MINIAUDIO
		if (I.output != AudioOutput::Off)
		{
			I.running.store(true);
			I.thread = std::thread([&I] { I.ThreadMain(); });
		}
#endif
	}

	// The master bus volume variable (the bus itself exists from the start), the device
	// subscription and the audio console commands.
	void AudioSystem::RegisterCommands(Swim::Commands::CommandRegistry& commands)
	{
		auto& I = *impl;
		I.buses[0].Variable =
			&commands.AddFloat("audio_volume_master", 0.5, 0.0, 1.0, "Master volume", Category, Swim::Commands::VariableFlags::Saved);
		I.buses[0].Variable->Subscribe(
			[this](const Swim::Commands::ConsoleVariable& variable)
			{
				SetBusVolume(AudioBus::Master, variable.GetFloat());
			});
		SetBusVolume(AudioBus::Master, I.buses[0].Variable->GetFloat());
		I.device->Subscribe(
			[this](const Swim::Commands::ConsoleVariable& variable)
			{
				SetDevice(variable.GetString());
			});
		commands.Register({ "audio_devices", "Lists the audio output devices", Category, "" },
			[this](const std::vector<std::string>&)
			{
				const auto names = ListDevices();
				std::cout << "[Audio] " << names.size() << " output device(s); current: " << GetStats().Device << '\n';

				for (const auto& name : names)
				{
					std::cout << "  " << name << '\n';
				}
			});
		commands.Register({ "audio_stats", "Prints voice, sound and device statistics", Category, "" },
			[this](const std::vector<std::string>&)
			{
				const auto stats = GetStats();
				std::cout << "[Audio] voices " << stats.Voices << "/" << stats.MaxVoices << ", sounds " << stats.Sounds << ", groups "
						  << stats.Groups << ", played " << stats.Played << ", stolen " << stats.Stolen << ", refused " << stats.Refused
						  << ", device " << stats.Device << " @ " << stats.SampleRate << " Hz, glitches: callback overruns "
						  << stats.CallbackOverruns << ", late callbacks " << stats.CallbackLate << ", starved streams "
						  << stats.StreamStarved << '\n';
			});
		commands.Register({ "audio_play", "Plays a sound file or group by name (2D, ui bus)", Category, "<path|group> [volume]" },
			[this](const std::vector<std::string>& arguments)
			{
				if (arguments.empty())
				{
					return;
				}

				const float volume = arguments.size() > 1 ? std::strtof(arguments[1].c_str(), nullptr) : 1.0f;

				if (HasGroup(arguments[0]))
				{
					SoundEventParams params;
					params.Volume = volume;
					PlayGroup(arguments[0], params);
					return;
				}

				PlayParams params;
				params.Bus = AudioBus::Ui;
				params.Volume = volume;

				if (!Play(arguments[0], params))
				{
					std::cerr << "[Audio] Could not play " << arguments[0] << '\n';
				}
			});
	}

	AudioSystem::~AudioSystem()
	{
		auto& I = *impl;

		if (I.commands)
		{
			for (const char* name : { "audio_devices", "audio_stats", "audio_play" })
			{
				I.commands->Unregister(name);
			}
		}

#if SWIM_AUDIO_MINIAUDIO
		if (I.thread.joinable())
		{
			I.running.store(false, std::memory_order_release);
			I.thread.join();
		}

		I.StopBackend();
#endif
	}

	void AudioSystem::Update(double deltaSeconds, const std::optional<AudioListener>& camera)
	{
		auto& I = *impl;
		I.time += std::max(deltaSeconds, 0.0);

		if (camera && listenerAutomatic && I.output != AudioOutput::Off)
		{
			AudioListener listener = *camera;

			if (I.lastCameraPosition && deltaSeconds > 1.0e-5)
			{
				listener.Velocity = (camera->Position - *I.lastCameraPosition) / static_cast<float>(deltaSeconds);
			}

			I.lastCameraPosition = camera->Position;
			Command command;
			command.Type = CommandType::Listener;
			command.Listener = listener;
			I.Send(std::move(command));
		}

		Event event;

		while (I.eventQueue.Pop(event))
		{
			if (event.Slot >= MaxSlots)
			{
				continue;
			}

			auto& voice = I.voices[event.Slot];

			if (!voice.Active || voice.Generation != event.Generation)
			{
				continue;
			}

			const VoiceHandle handle{ event.Slot, event.Generation };

			if (event.Type == EventType::Marker)
			{
				if (voice.OnMarker)
				{
					voice.OnMarker(handle, event.Marker);
				}

				continue;
			}

			auto finished = std::move(voice.OnFinished);
			voice.Active = false;
			voice.Counted = false;
			voice.OnMarker = {};

			if (finished)
			{
				finished(handle);
			}
		}
	}

	void AudioSystem::MountRoot(std::string name, std::filesystem::path folder)
	{
		impl->roots[std::move(name)] = std::move(folder);
	}

	bool AudioSystem::IsRootMounted(std::string_view name) const
	{
		return impl->roots.find(name) != impl->roots.end();
	}

	std::filesystem::path AudioSystem::ResolvePath(std::string_view path) const
	{
		auto& I = *impl;
		const auto colon = path.find(":/");

		if (colon != std::string_view::npos && colon > 1)
		{
			const auto root = path.substr(0, colon);
			const auto found = I.roots.find(root);

			if (found == I.roots.end())
			{
				if (std::find(I.warnedRoots.begin(), I.warnedRoots.end(), root) == I.warnedRoots.end())
				{
					I.warnedRoots.emplace_back(root);
					std::cerr << "[Audio] No folder is mounted as " << root << ":/ - its sounds are skipped.\n";
				}

				return {};
			}

			return found->second / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data() + colon + 2), path.size() - colon - 2));
		}

		std::filesystem::path relative(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
		return relative.is_absolute() || I.desc.AssetRoot.empty() ? relative : I.desc.AssetRoot / relative;
	}

	SoundHandle AudioSystem::LoadSound(std::string_view path, SoundLoad load)
	{
		auto& I = *impl;
		const std::string key(path);

		if (const auto found = I.soundIds.find(key); found != I.soundIds.end())
		{
			return { found->second };
		}

		auto resolved = ResolvePath(path);

		if (resolved.empty())
		{
			return {};
		}

		const auto id = static_cast<std::uint32_t>(I.sounds.size());
		I.sounds.push_back({ std::move(resolved), load, key });
		I.soundIds.emplace(key, id);
		return { id };
	}

	void AudioSystem::Preload(SoundHandle sound)
	{
		auto& I = *impl;

		if (sound.Id == 0 || sound.Id >= I.sounds.size())
		{
			return;
		}

		Command command;
		command.Type = CommandType::Preload;
		command.Path = I.sounds[sound.Id].Path;
		I.Send(std::move(command));
	}

	VoiceHandle AudioSystem::Play(SoundHandle sound, const PlayParams& params)
	{
		auto& I = *impl;

		if (I.output == AudioOutput::Off || sound.Id == 0 || sound.Id >= I.sounds.size())
		{
			++I.refused;
			return {};
		}

		const auto limit = static_cast<std::uint32_t>(I.maxVoices ? I.maxVoices->GetInt() : 64);

		if (I.CountedVoices() >= limit)
		{
			// Steal the lowest-priority voice, the oldest among equals.
			std::uint32_t victim = UINT32_MAX;

			for (std::uint32_t i = 0; i < MaxSlots; ++i)
			{
				const auto& voice = I.voices[i];

				if (voice.Counted &&
					(victim == UINT32_MAX || voice.Priority < I.voices[victim].Priority ||
						(voice.Priority == I.voices[victim].Priority && voice.Order < I.voices[victim].Order)))
				{
					victim = i;
				}
			}

			if (victim == UINT32_MAX || I.voices[victim].Priority > params.Priority)
			{
				++I.refused;
				return {};
			}

			I.StopSlot(victim, StealFadeSeconds);
			++I.stolen;
		}

		std::uint32_t index = UINT32_MAX;

		for (std::uint32_t i = 0; i < MaxSlots; ++i)
		{
			if (!I.voices[i].Active)
			{
				index = i;
				break;
			}
		}

		if (index == UINT32_MAX)
		{
			++I.refused;
			return {};
		}

		auto& voice = I.voices[index];
		voice.Generation = voice.Generation + 1 == 0 ? 1 : voice.Generation + 1;
		voice.Active = true;
		voice.Counted = true;
		voice.Priority = params.Priority;
		voice.Order = ++I.order;
		voice.Group = UINT32_MAX;
		voice.Bus = params.Bus;
		voice.OnFinished = params.OnFinished;
		voice.OnMarker = params.OnMarker;
		Command command;
		command.Type = CommandType::Play;
		command.Slot = index;
		command.Generation = voice.Generation;
		command.Bus = params.Bus.Id < I.buses.size() ? params.Bus.Id : 0;
		command.Path = I.sounds[sound.Id].Path;
		command.Params = params;
		command.Params.OnFinished = {};
		command.Params.OnMarker = {};

		if (command.Params.Load == SoundLoad::Auto)
		{
			command.Params.Load = I.sounds[sound.Id].Load;
		}

		if (I.playObserver)
		{
			I.playObserver({ I.sounds[sound.Id].Key, params.Position, params.Volume, params.Pitch, params.Loop });
		}

		I.Send(std::move(command));
		++I.played;
		return { index, voice.Generation };
	}

	void AudioSystem::SetPlayObserver(std::function<void(const AudioPlayRecord&)> observer)
	{
		impl->playObserver = std::move(observer);
	}

	VoiceHandle AudioSystem::Play(std::string_view path, const PlayParams& params)
	{
		return Play(LoadSound(path, params.Load), params);
	}

	void AudioSystem::Stop(VoiceHandle handle, float fadeSeconds)
	{
		if (impl->Find(handle))
		{
			impl->StopSlot(handle.Index, fadeSeconds);
		}
	}

	void AudioSystem::StopBus(BusHandle bus, float fadeSeconds)
	{
		for (std::uint32_t i = 0; i < MaxSlots; ++i)
		{
			if (impl->voices[i].Active && impl->voices[i].Bus == bus)
			{
				impl->StopSlot(i, fadeSeconds);
			}
		}
	}

	void AudioSystem::SetPaused(VoiceHandle handle, bool paused)
	{
		if (impl->Find(handle))
		{
			Command command;
			command.Type = CommandType::Pause;
			command.Slot = handle.Index;
			command.Generation = handle.Generation;
			command.Flag = paused;
			impl->Send(std::move(command));
		}
	}

	void AudioSystem::SetVolume(VoiceHandle handle, float volume, float fadeSeconds)
	{
		if (impl->Find(handle))
		{
			Command command;
			command.Type = CommandType::Volume;
			command.Slot = handle.Index;
			command.Generation = handle.Generation;
			command.Value = volume;
			command.Seconds = fadeSeconds;
			impl->Send(std::move(command));
		}
	}

	void AudioSystem::SetPitch(VoiceHandle handle, float pitch)
	{
		if (impl->Find(handle))
		{
			Command command;
			command.Type = CommandType::Pitch;
			command.Slot = handle.Index;
			command.Generation = handle.Generation;
			command.Value = pitch;
			impl->Send(std::move(command));
		}
	}

	void AudioSystem::SetPosition(VoiceHandle handle, const glm::vec3& position, const glm::vec3& velocity)
	{
		if (impl->Find(handle))
		{
			Command command;
			command.Type = CommandType::Position;
			command.Slot = handle.Index;
			command.Generation = handle.Generation;
			command.Listener.Position = position;
			command.Listener.Velocity = velocity;
			impl->Send(std::move(command));
		}
	}

	bool AudioSystem::IsPlaying(VoiceHandle handle) const
	{
		return impl->Find(handle) != nullptr;
	}

	BusHandle AudioSystem::AddBus(std::string name, BusHandle parent)
	{
		auto& I = *impl;

		if (const auto existing = FindBus(name))
		{
			return *existing;
		}

		if (I.buses.size() >= MaxBuses)
		{
			std::cerr << "[Audio] Bus limit reached; " << name << " plays on master.\n";
			return AudioBus::Master;
		}

		const BusHandle bus{ static_cast<std::uint32_t>(I.buses.size()) };
		I.buses.push_back({ name, parent.Id < bus.Id ? parent.Id : 0u, 1.0f, nullptr });
		Command command;
		command.Type = CommandType::BusCreate;
		command.Bus = bus.Id;
		command.Parent = I.buses.back().Parent;
		I.Send(std::move(command));

		if (I.commands)
		{
			auto& variable = I.commands->AddFloat("audio_volume_" + name, 1.0, 0.0, 1.0, "Volume of the " + name + " bus", Category,
				Swim::Commands::VariableFlags::Saved);
			I.buses[bus.Id].Variable = &variable;
			variable.Subscribe(
				[this, bus](const Swim::Commands::ConsoleVariable& changed)
				{
					SetBusVolume(bus, changed.GetFloat());
				});
			SetBusVolume(bus, variable.GetFloat());
		}

		return bus;
	}

	std::optional<BusHandle> AudioSystem::FindBus(std::string_view name) const
	{
		for (std::uint32_t i = 0; i < impl->buses.size(); ++i)
		{
			if (impl->buses[i].Name == name)
			{
				return BusHandle{ i };
			}
		}

		return std::nullopt;
	}

	void AudioSystem::SetBusVolume(BusHandle bus, float volume)
	{
		auto& I = *impl;

		if (bus.Id >= I.buses.size())
		{
			return;
		}

		I.buses[bus.Id].Volume = std::clamp(volume, 0.0f, 1.0f);
		Command command;
		command.Type = CommandType::BusVolume;
		command.Bus = bus.Id;
		command.Value = I.buses[bus.Id].Volume;
		I.Send(std::move(command));
	}

	float AudioSystem::GetBusVolume(BusHandle bus) const
	{
		return bus.Id < impl->buses.size() ? impl->buses[bus.Id].Volume : 0.0f;
	}

	void AudioSystem::RegisterGroup(std::string name, SoundGroupDesc desc)
	{
		auto& I = *impl;
		Impl::Group group;
		group.Name = name;
		group.Desc = std::move(desc);

		if (const auto found = I.groupIds.find(name); found != I.groupIds.end())
		{
			I.groups[found->second] = std::move(group);
			return;
		}

		I.groupIds.emplace(std::move(name), static_cast<std::uint32_t>(I.groups.size()));
		I.groups.push_back(std::move(group));
	}

	bool AudioSystem::HasGroup(std::string_view name) const
	{
		return impl->groupIds.find(std::string(name)) != impl->groupIds.end();
	}

	VoiceHandle AudioSystem::PlayGroup(std::string_view name, const SoundEventParams& params)
	{
		auto& I = *impl;
		const auto found = I.groupIds.find(std::string(name));

		if (found == I.groupIds.end() || I.output == AudioOutput::Off)
		{
			++I.refused;
			return {};
		}

		const auto groupIndex = found->second;
		auto& group = I.groups[groupIndex];
		const auto& desc = group.Desc;

		if (!group.Expanded)
		{
			// Wildcards list their one folder now (first play), never at startup.
			group.Expanded = true;

			for (const auto& entry : desc.Sounds)
			{
				const auto slash = entry.find_last_of('/');
				const std::string file = slash == std::string::npos ? entry : entry.substr(slash + 1);

				if (file.find_first_of("*?") == std::string::npos)
				{
					if (const auto sound = LoadSound(entry, desc.Load))
					{
						group.Sounds.push_back(sound);
					}

					continue;
				}

				const std::string folder = slash == std::string::npos ? std::string() : entry.substr(0, slash + 1);
				const auto directory = ResolvePath(folder.empty() ? std::string_view(".") : std::string_view(folder));
				std::error_code error;
				std::vector<std::string> names;

				for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
				{
					const auto u8 = it->path().filename().u8string();
					const std::string candidate(reinterpret_cast<const char*>(u8.data()), u8.size());

					if (it->is_regular_file(error) && GlobMatch(file, candidate))
					{
						names.push_back(candidate);
					}
				}

				std::sort(names.begin(), names.end());

				for (const auto& match : names)
				{
					if (const auto sound = LoadSound(folder + match, desc.Load))
					{
						group.Sounds.push_back(sound);
					}
				}
			}

			if (group.Sounds.empty())
			{
				std::cerr << "[Audio] Sound group " << group.Name << " has no playable sounds.\n";
			}
		}

		if (group.Sounds.empty() || (desc.CooldownSeconds > 0.0f && I.time - group.LastPlay < desc.CooldownSeconds))
		{
			++I.refused;
			return {};
		}

		if (desc.MaxInstances > 0)
		{
			std::uint32_t count = 0;
			std::uint32_t oldest = UINT32_MAX;

			for (std::uint32_t i = 0; i < MaxSlots; ++i)
			{
				const auto& voice = I.voices[i];

				if (voice.Counted && voice.Group == groupIndex)
				{
					++count;

					if (oldest == UINT32_MAX || voice.Order < I.voices[oldest].Order)
					{
						oldest = i;
					}
				}
			}

			if (count >= desc.MaxInstances && oldest != UINT32_MAX)
			{
				I.StopSlot(oldest, StealFadeSeconds);
			}
		}

		const auto count = static_cast<std::uint32_t>(group.Sounds.size());
		std::uint32_t pick = 0;

		switch (desc.Pick)
		{
		case SoundPick::Sequence:
			pick = group.Next++ % count;
			break;
		case SoundPick::RandomNoRepeat:
			if (count > 1)
			{
				pick = std::uniform_int_distribution<std::uint32_t>(0, count - 2)(I.random);
				pick += pick >= group.Last ? 1u : 0u;
				break;
			}

			[[fallthrough]];
		default:
			pick = std::uniform_int_distribution<std::uint32_t>(0, count - 1)(I.random);
			break;
		}

		group.Last = pick;
		group.LastPlay = I.time;
		const auto vary = [&](float low, float high)
		{
			return low < high ? std::uniform_real_distribution<float>(low, high)(I.random) : low;
		};
		PlayParams play;
		play.Bus = desc.Bus;
		play.Volume = vary(desc.VolumeMin, desc.VolumeMax) * params.Volume;
		play.Pitch = vary(desc.PitchMin, desc.PitchMax) * params.Pitch;
		play.Loop = desc.Loop;
		play.Position = params.Position;
		play.Velocity = params.Velocity;
		play.Attenuation = desc.Attenuation;
		play.Priority = desc.Priority;
		play.Load = desc.Load;
		play.FadeInSeconds = params.FadeInSeconds;
		play.OnFinished = params.OnFinished;
		const auto voice = Play(group.Sounds[pick], play);

		if (voice)
		{
			I.voices[voice.Index].Group = groupIndex;
		}

		return voice;
	}

	void AudioSystem::MapEvent(std::string event, std::string group)
	{
		impl->events[std::move(event)] = std::move(group);
	}

	VoiceHandle AudioSystem::PlayEvent(std::string_view event, const SoundEventParams& params)
	{
		const auto found = impl->events.find(std::string(event));
		return PlayGroup(found == impl->events.end() ? event : std::string_view(found->second), params);
	}

	void AudioSystem::SetListener(const AudioListener& listener)
	{
		listenerAutomatic = false;
		Command command;
		command.Type = CommandType::Listener;
		command.Listener = listener;
		impl->Send(std::move(command));
	}

	std::vector<std::string> AudioSystem::ListDevices() const
	{
		std::vector<std::string> names;
#if SWIM_AUDIO_MINIAUDIO
		if (impl->output == AudioOutput::Off)
		{
			return names;
		}

		ma_context context;

		if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS)
		{
			return names;
		}

		ma_device_info* playback = nullptr;
		ma_uint32 count = 0;

		if (ma_context_get_devices(&context, &playback, &count, nullptr, nullptr) == MA_SUCCESS)
		{
			for (ma_uint32 i = 0; i < count; ++i)
			{
				names.emplace_back(playback[i].name);
			}
		}

		ma_context_uninit(&context);
#endif
		return names;
	}

	void AudioSystem::SetDevice(std::string name)
	{
		auto& I = *impl;

		if (I.device && I.device->GetString() != name)
		{
			I.device->SetString(name); // Its subscription sends the command.
			return;
		}

		if (I.output != AudioOutput::Device)
		{
			return;
		}

		Command command;
		command.Type = CommandType::Device;
		command.Text = std::move(name);
		I.Send(std::move(command));
	}

	AudioStats AudioSystem::GetStats() const
	{
		auto& I = *impl;
		AudioStats stats;
		stats.Voices = I.CountedVoices();
		stats.MaxVoices = static_cast<std::uint32_t>(I.maxVoices ? I.maxVoices->GetInt() : 64);
		stats.Sounds = static_cast<std::uint32_t>(I.sounds.size() - 1);
		stats.Groups = static_cast<std::uint32_t>(I.groups.size());
		stats.Played = I.played;
		stats.Stolen = I.stolen;
		stats.Refused = I.refused;
#if SWIM_AUDIO_MINIAUDIO
		stats.Device = I.output == AudioOutput::Off ? "off" : I.ReadDeviceName();
		stats.SampleRate = I.sampleRate.load();
		stats.CallbackOverruns = I.callbackOverruns.load(std::memory_order_relaxed);
		stats.CallbackLate = I.callbackLate.load(std::memory_order_relaxed);
		stats.StreamStarved = I.streamStarved.load(std::memory_order_relaxed);
#else
		stats.Device = "off";
#endif
		return stats;
	}

	AudioOutput AudioSystem::GetOutput() const
	{
		return impl->output;
	}

} // namespace Engine
