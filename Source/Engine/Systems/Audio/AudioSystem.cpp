#include "Engine/Systems/Audio/AudioSystem.h"

#include "Engine/Commands/CommandRegistry.h"

#if SWIM_AUDIO_MINIAUDIO
#include <miniaudio.h>
#endif

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <random>
#include <thread>
#include <unordered_map>

namespace Engine
{

	namespace
	{

		constexpr const char* Category = "Audio";
		constexpr std::uint32_t MaxSlots = 512;	 // Voice slots (a stopping voice keeps its slot until it ends).
		constexpr std::uint32_t MaxBuses = 64;
		constexpr std::uint64_t StreamThreshold = 1u << 20;
		constexpr float StealFadeSeconds = 0.01f;

		// Single producer, single consumer; the producer spins (yields) when it is full.
		template <typename T, std::size_t Capacity> class SpscQueue
		{

		  public:

			void Push(T value)
			{
				const auto tail = writeIndex.load(std::memory_order_relaxed);

				while (tail - readIndex.load(std::memory_order_acquire) >= Capacity)
				{
					std::this_thread::yield();
				}

				slots[tail % Capacity] = std::move(value);
				writeIndex.store(tail + 1, std::memory_order_release);
			}

			bool Pop(T& value)
			{
				const auto head = readIndex.load(std::memory_order_relaxed);

				if (head == writeIndex.load(std::memory_order_acquire))
				{
					return false;
				}

				value = std::move(slots[head % Capacity]);
				readIndex.store(head + 1, std::memory_order_release);
				return true;
			}

		  private:

			std::array<T, Capacity> slots{};
			alignas(64) std::atomic<std::uint64_t> writeIndex{ 0 };
			alignas(64) std::atomic<std::uint64_t> readIndex{ 0 };

		};

		enum class CommandType : std::uint8_t
		{
			Play,
			Stop,
			Pause,
			Volume,
			Pitch,
			Position,
			BusCreate,
			BusVolume,
			Listener,
			Device,
			Preload,
		};

		struct Command
		{
			CommandType Type = CommandType::Play;
			std::uint32_t Slot = 0;
			std::uint32_t Generation = 0;
			std::uint32_t Bus = 0;
			std::uint32_t Parent = 0;
			float Value = 0.0f;
			float Seconds = 0.0f;
			bool Flag = false;
			PlayParams Params; // Play only (callbacks stay on the main thread).
			AudioListener Listener;
			std::filesystem::path Path;
			std::string Text;
		};

		enum class EventType : std::uint8_t
		{
			Finished,
			Marker,
		};

		struct Event
		{
			EventType Type = EventType::Finished;
			std::uint32_t Slot = 0;
			std::uint32_t Generation = 0;
			std::uint32_t Marker = 0;
		};

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

	struct AudioSystem::Impl
	{
		struct Voice
		{
			std::uint32_t Generation = 0;
			bool Active = false;   // Holds its slot until the backend reports the end.
			bool Counted = false;  // Counts against the voice limit (false once stopped or stolen).
			int Priority = 0;
			std::uint64_t Order = 0;
			std::uint32_t Group = UINT32_MAX;
			BusHandle Bus{};
			std::function<void(VoiceHandle)> OnFinished;
			std::function<void(VoiceHandle, std::uint32_t)> OnMarker;
		};

		struct Sound
		{
			std::filesystem::path Path;
			SoundLoad Load = SoundLoad::Auto;
		};

		struct Group
		{
			std::string Name;
			SoundGroupDesc Desc;
			std::vector<SoundHandle> Sounds;
			bool Expanded = false;
			std::uint32_t Next = 0;
			std::uint32_t Last = UINT32_MAX;
			double LastPlay = -1.0e9;
		};

		struct Bus
		{
			std::string Name;
			std::uint32_t Parent = 0;
			float Volume = 1.0f;
			Swim::Commands::ConsoleVariable* Variable = nullptr;
		};

		AudioSystemDesc desc;
		Swim::Commands::CommandRegistry* commands = nullptr;
		AudioOutput output = AudioOutput::Off;
		std::map<std::string, std::filesystem::path, std::less<>> roots;
		std::vector<std::string> warnedRoots;
		std::vector<Sound> sounds{ Sound{} }; // Id 0 is invalid.
		std::unordered_map<std::string, std::uint32_t> soundIds;
		std::vector<Group> groups;
		std::unordered_map<std::string, std::uint32_t> groupIds;
		std::unordered_map<std::string, std::string> events;
		std::vector<Bus> buses;
		std::array<Voice, MaxSlots> voices{};
		std::uint64_t order = 0;
		std::uint64_t played = 0;
		std::uint64_t stolen = 0;
		std::uint64_t refused = 0;
		double time = 0.0;
		std::mt19937 random{ 0x5EEDu };
		std::optional<glm::vec3> lastCameraPosition;
		Swim::Commands::ConsoleVariable* maxVoices = nullptr;
		Swim::Commands::ConsoleVariable* device = nullptr;

		SpscQueue<Command, 1024> commandQueue;
		SpscQueue<Event, 4096> eventQueue;
		std::thread thread;
		std::atomic<bool> running{ false };

#if SWIM_AUDIO_MINIAUDIO
		// Audio thread state.
		struct Slot
		{
			ma_sound Sound{};
			bool Live = false;
			bool Paused = false;
			std::uint32_t Generation = 0;
			std::vector<float> Markers;
			std::uint32_t NextMarker = 0;
			float LastCursor = 0.0f;
		};

		ma_engine engine{};
		ma_context context{};
		ma_device outputDevice{};
		bool engineReady = false;
		bool contextReady = false;
		bool deviceReady = false;
		std::atomic<bool> deviceLost{ false };
		std::atomic<bool> closingDevice{ false };
		std::string deviceName = "default";
		std::string wantedDevice;
		std::array<ma_sound_group, MaxBuses> groupsBackend{};
		std::array<bool, MaxBuses> groupReady{};
		std::vector<Slot> slots = std::vector<Slot>(MaxSlots);
		std::vector<float> nullScratch;
		std::atomic<std::uint32_t> sampleRate{ 0 };
		std::string deviceNameShared; // Written by the audio thread under deviceNameLock.
		std::atomic_flag deviceNameLock = ATOMIC_FLAG_INIT;

		static void DataCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount)
		{
			auto* self = static_cast<Impl*>(device->pUserData);
			ma_engine_read_pcm_frames(&self->engine, output, frameCount, nullptr);
		}

		static void NotificationCallback(const ma_device_notification* notification)
		{
			auto* self = static_cast<Impl*>(notification->pDevice->pUserData);

			if (notification->type == ma_device_notification_type_stopped && !self->closingDevice.load())
			{
				self->deviceLost.store(true);
			}
		}

		void PublishDeviceName(std::string name)
		{
			while (deviceNameLock.test_and_set(std::memory_order_acquire))
			{
			}

			deviceNameShared = std::move(name);
			deviceNameLock.clear(std::memory_order_release);
		}

		std::string ReadDeviceName()
		{
			while (deviceNameLock.test_and_set(std::memory_order_acquire))
			{
			}

			auto copy = deviceNameShared;
			deviceNameLock.clear(std::memory_order_release);
			return copy;
		}

		void CloseDevice()
		{
			if (deviceReady)
			{
				closingDevice.store(true);
				ma_device_uninit(&outputDevice);
				deviceReady = false;
				closingDevice.store(false);
			}
		}

		bool OpenDevice(const std::string& name)
		{
			CloseDevice();

			if (!contextReady)
			{
				return false;
			}

			ma_device_info* playback = nullptr;
			ma_uint32 playbackCount = 0;
			const ma_device_id* id = nullptr;
			std::string opened = "default";

			if (!name.empty() && ma_context_get_devices(&context, &playback, &playbackCount, nullptr, nullptr) == MA_SUCCESS)
			{
				for (ma_uint32 i = 0; i < playbackCount; ++i)
				{
					if (name == playback[i].name)
					{
						id = &playback[i].id;
						opened = playback[i].name;
					}
				}
			}

			ma_device_config config = ma_device_config_init(ma_device_type_playback);
			config.playback.format = ma_format_f32;
			config.playback.channels = desc.Channels;
			config.playback.pDeviceID = id;
			config.sampleRate = desc.SampleRate;
			config.dataCallback = &DataCallback;
			config.notificationCallback = &NotificationCallback;
			config.pUserData = this;

			if (ma_device_init(&context, &config, &outputDevice) != MA_SUCCESS)
			{
				return false;
			}

			deviceReady = true;

			if (ma_device_start(&outputDevice) != MA_SUCCESS)
			{
				CloseDevice();
				return false;
			}

			if (!id)
			{
				ma_device_info info{};

				if (ma_device_get_info(&outputDevice, ma_device_type_playback, &info) == MA_SUCCESS)
				{
					opened = info.name;
				}
			}

			deviceName = opened;
			PublishDeviceName(opened);
			return true;
		}

		bool StartBackend()
		{
			ma_engine_config config = ma_engine_config_init();
			config.noDevice = MA_TRUE;
			config.channels = desc.Channels;
			config.sampleRate = desc.SampleRate;
			config.listenerCount = 1;

			if (ma_engine_init(&config, &engine) != MA_SUCCESS)
			{
				return false;
			}

			engineReady = true;
			sampleRate.store(ma_engine_get_sample_rate(&engine));

			if (output == AudioOutput::Device)
			{
				contextReady = ma_context_init(nullptr, 0, nullptr, &context) == MA_SUCCESS;

				if (!OpenDevice(wantedDevice) && !OpenDevice({}))
				{
					std::cerr << "[Audio] No output device could be opened; using the null output.\n";
					output = AudioOutput::Null;
				}
			}

			if (output == AudioOutput::Null)
			{
				PublishDeviceName("null");
			}

			CreateBus(0, UINT32_MAX);
			return true;
		}

		void StopBackend()
		{
			CloseDevice();

			for (auto& slot : slots)
			{
				if (slot.Live)
				{
					ma_sound_uninit(&slot.Sound);
					slot.Live = false;
				}
			}

			for (std::uint32_t i = MaxBuses; i-- > 0;)
			{
				if (groupReady[i])
				{
					ma_sound_group_uninit(&groupsBackend[i]);
					groupReady[i] = false;
				}
			}

			if (engineReady)
			{
				ma_engine_uninit(&engine);
				engineReady = false;
			}

			if (contextReady)
			{
				ma_context_uninit(&context);
				contextReady = false;
			}
		}

		void CreateBus(std::uint32_t bus, std::uint32_t parent)
		{
			if (bus >= MaxBuses || groupReady[bus])
			{
				return;
			}

			ma_sound_group* parentGroup = parent < MaxBuses && groupReady[parent] ? &groupsBackend[parent] : nullptr;
			groupReady[bus] = ma_sound_group_init(&engine, 0, parentGroup, &groupsBackend[bus]) == MA_SUCCESS;
		}

		static ma_attenuation_model Model(SoundAttenuationModel model)
		{
			switch (model)
			{
			case SoundAttenuationModel::Linear:
				return ma_attenuation_model_linear;
			case SoundAttenuationModel::Exponential:
				return ma_attenuation_model_exponential;
			default:
				return ma_attenuation_model_inverse;
			}
		}

		static std::uint64_t Milliseconds(float seconds) { return static_cast<std::uint64_t>(std::max(seconds, 0.0f) * 1000.0f + 0.5f); }

		void Finish(std::uint32_t index)
		{
			auto& slot = slots[index];

			if (slot.Live)
			{
				ma_sound_uninit(&slot.Sound);
				slot.Live = false;
			}

			eventQueue.Push({ EventType::Finished, index, slot.Generation, 0 });
		}

		void ExecutePlay(Command& command)
		{
			auto& slot = slots[command.Slot];

			if (slot.Live)
			{
				ma_sound_uninit(&slot.Sound);
				slot.Live = false;
			}

			slot.Generation = command.Generation;
			const auto& params = command.Params;
			std::error_code error;
			const auto size = std::filesystem::file_size(command.Path, error);

			if (error)
			{
				Finish(command.Slot);
				return;
			}

			const bool stream =
				params.Load == SoundLoad::Stream || (params.Load == SoundLoad::Auto && size > StreamThreshold);
			ma_uint32 flags = MA_SOUND_FLAG_ASYNC | (stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE);

			if (!params.Position)
			{
				flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
			}

			ma_sound_group* group = command.Bus < MaxBuses && groupReady[command.Bus] ? &groupsBackend[command.Bus] : nullptr;
#ifdef _WIN32
			const ma_result result = ma_sound_init_from_file_w(&engine, command.Path.c_str(), flags, group, nullptr, &slot.Sound);
#else
			const ma_result result = ma_sound_init_from_file(&engine, command.Path.c_str(), flags, group, nullptr, &slot.Sound);
#endif

			if (result != MA_SUCCESS)
			{
				Finish(command.Slot);
				return;
			}

			slot.Live = true;
			slot.Paused = false;
			slot.Markers = params.Markers;
			std::sort(slot.Markers.begin(), slot.Markers.end());
			slot.NextMarker = 0;
			slot.LastCursor = params.StartSeconds;
			auto* sound = &slot.Sound;
			ma_sound_set_looping(sound, params.Loop ? MA_TRUE : MA_FALSE);
			ma_sound_set_pitch(sound, std::clamp(params.Pitch, 0.05f, 8.0f));

			if (params.Position)
			{
				const auto& p = *params.Position;
				const auto& a = params.Attenuation;
				ma_sound_set_position(sound, p.x, p.y, p.z);
				ma_sound_set_velocity(sound, params.Velocity.x, params.Velocity.y, params.Velocity.z);
				ma_sound_set_attenuation_model(sound, Model(a.Model));
				ma_sound_set_min_distance(sound, std::max(a.MinDistance, 0.01f));
				ma_sound_set_max_distance(sound, std::max(a.MaxDistance, a.MinDistance));
				ma_sound_set_rolloff(sound, std::max(a.Rolloff, 0.0f));
				ma_sound_set_doppler_factor(sound, std::max(a.Doppler, 0.0f));
			}
			else
			{
				ma_sound_set_pan(sound, std::clamp(params.Pan, -1.0f, 1.0f));
			}

			if (params.StartSeconds > 0.0f)
			{
				ma_sound_seek_to_second(sound, params.StartSeconds);
			}

			const float volume = std::max(params.Volume, 0.0f);
			ma_sound_set_fade_in_milliseconds(sound, params.FadeInSeconds > 0.0f ? 0.0f : volume, volume, Milliseconds(params.FadeInSeconds));
			ma_sound_start(sound);
		}

		void Execute(Command& command)
		{
			Slot* slot = command.Slot < MaxSlots ? &slots[command.Slot] : nullptr;
			const bool live = slot && slot->Live && slot->Generation == command.Generation;

			switch (command.Type)
			{
			case CommandType::Play:
				ExecutePlay(command);
				break;
			case CommandType::Stop:
				if (live)
				{
					slot->Paused = false;

					if (command.Seconds > 0.0f && ma_sound_is_playing(&slot->Sound))
					{
						ma_sound_stop_with_fade_in_milliseconds(&slot->Sound, Milliseconds(command.Seconds));
					}
					else
					{
						Finish(command.Slot);
					}
				}

				break;
			case CommandType::Pause:
				if (live && slot->Paused != command.Flag)
				{
					slot->Paused = command.Flag;
					command.Flag ? ma_sound_stop(&slot->Sound) : ma_sound_start(&slot->Sound);
				}

				break;
			case CommandType::Volume:
				if (live)
				{
					ma_sound_set_fade_in_milliseconds(&slot->Sound, -1.0f, std::max(command.Value, 0.0f), Milliseconds(command.Seconds));
				}

				break;
			case CommandType::Pitch:
				if (live)
				{
					ma_sound_set_pitch(&slot->Sound, std::clamp(command.Value, 0.05f, 8.0f));
				}

				break;
			case CommandType::Position:
				if (live)
				{
					const auto& p = command.Listener.Position;
					const auto& v = command.Listener.Velocity;
					ma_sound_set_position(&slot->Sound, p.x, p.y, p.z);
					ma_sound_set_velocity(&slot->Sound, v.x, v.y, v.z);
				}

				break;
			case CommandType::BusCreate:
				CreateBus(command.Bus, command.Parent);
				break;
			case CommandType::BusVolume:
				if (command.Bus < MaxBuses && groupReady[command.Bus])
				{
					ma_sound_group_set_volume(&groupsBackend[command.Bus], std::max(command.Value, 0.0f));
				}

				break;
			case CommandType::Listener:
			{
				const auto& l = command.Listener;
				ma_engine_listener_set_position(&engine, 0, l.Position.x, l.Position.y, l.Position.z);
				ma_engine_listener_set_direction(&engine, 0, l.Forward.x, l.Forward.y, l.Forward.z);
				ma_engine_listener_set_world_up(&engine, 0, l.Up.x, l.Up.y, l.Up.z);
				ma_engine_listener_set_velocity(&engine, 0, l.Velocity.x, l.Velocity.y, l.Velocity.z);
				break;
			}
			case CommandType::Device:
				wantedDevice = command.Text;

				if (output == AudioOutput::Device && !OpenDevice(wantedDevice) && !OpenDevice({}))
				{
					std::cerr << "[Audio] Could not open an output device.\n";
				}

				break;
			case CommandType::Preload:
#ifdef _WIN32
				ma_resource_manager_register_file_w(ma_engine_get_resource_manager(&engine), command.Path.c_str(),
					MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE | MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC);
#else
				ma_resource_manager_register_file(ma_engine_get_resource_manager(&engine), command.Path.c_str(),
					MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE | MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC);
#endif
				break;
			}
		}

		void Poll()
		{
			for (std::uint32_t i = 0; i < MaxSlots; ++i)
			{
				auto& slot = slots[i];

				if (!slot.Live || slot.Paused)
				{
					continue;
				}

				if (!ma_sound_is_playing(&slot.Sound))
				{
					Finish(i);
					continue;
				}

				if (slot.Markers.empty())
				{
					continue;
				}

				float cursor = 0.0f;

				if (ma_sound_get_cursor_in_seconds(&slot.Sound, &cursor) != MA_SUCCESS)
				{
					continue;
				}

				if (cursor + 0.001f < slot.LastCursor)
				{
					slot.NextMarker = 0; // Looped.
				}

				while (slot.NextMarker < slot.Markers.size() && slot.Markers[slot.NextMarker] <= cursor)
				{
					eventQueue.Push({ EventType::Marker, i, slot.Generation, slot.NextMarker++ });
				}

				slot.LastCursor = cursor;
			}
		}

		void ThreadMain()
		{
			using Clock = std::chrono::steady_clock;
			auto last = Clock::now();
			auto lastRetry = last;

			while (running.load(std::memory_order_acquire))
			{
				Command command;

				while (commandQueue.Pop(command))
				{
					Execute(command);
				}

				const auto now = Clock::now();

				if (output == AudioOutput::Null)
				{
					// Advance the mix in real time so voices end and markers pass without a device.
					const auto frames = static_cast<std::uint64_t>(std::chrono::duration<double>(now - last).count() * sampleRate.load());
					nullScratch.resize(std::size_t(std::min<std::uint64_t>(frames, 48000)) * desc.Channels);

					if (frames > 0)
					{
						ma_engine_read_pcm_frames(&engine, nullScratch.data(), std::min<std::uint64_t>(frames, 48000), nullptr);
					}
				}
				else if (deviceLost.load() && now - lastRetry > std::chrono::seconds(1))
				{
					lastRetry = now;

					if (OpenDevice(wantedDevice) || OpenDevice({}))
					{
						deviceLost.store(false);
						std::cerr << "[Audio] Output device reopened: " << deviceName << '\n';
					}
				}

				last = now;
				Poll();
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
		}
#endif

		void Send(Command command)
		{
			if (output != AudioOutput::Off)
			{
				commandQueue.Push(std::move(command));
			}
		}

		std::uint32_t CountedVoices() const
		{
			std::uint32_t count = 0;

			for (const auto& voice : voices)
			{
				count += voice.Counted ? 1u : 0u;
			}

			return count;
		}

		void StopSlot(std::uint32_t index, float fadeSeconds)
		{
			auto& voice = voices[index];

			if (!voice.Active)
			{
				return;
			}

			voice.Counted = false;
			Command command;
			command.Type = CommandType::Stop;
			command.Slot = index;
			command.Generation = voice.Generation;
			command.Seconds = fadeSeconds;
			Send(std::move(command));
		}

		Voice* Find(VoiceHandle handle)
		{
			if (handle.Index >= MaxSlots)
			{
				return nullptr;
			}

			auto& voice = voices[handle.Index];
			return voice.Active && voice.Generation == handle.Generation ? &voice : nullptr;
		}
	};

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

		// The master bus volume variable (the bus itself exists from the start).
		if (commands)
		{
			I.buses[0].Variable = &commands->AddFloat("audio_volume_master", 0.5, 0.0, 1.0, "Master volume", Category,
				Swim::Commands::VariableFlags::Saved);
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
			commands->Register({ "audio_devices", "Lists the audio output devices", Category, "" },
				[this](const std::vector<std::string>&)
				{
					const auto names = ListDevices();
					std::cout << "[Audio] " << names.size() << " output device(s); current: " << GetStats().Device << '\n';

					for (const auto& name : names)
					{
						std::cout << "  " << name << '\n';
					}
				});
			commands->Register({ "audio_stats", "Prints voice, sound and device statistics", Category, "" },
				[this](const std::vector<std::string>&)
				{
					const auto stats = GetStats();
					std::cout << "[Audio] voices " << stats.Voices << "/" << stats.MaxVoices << ", sounds " << stats.Sounds << ", groups "
							  << stats.Groups << ", played " << stats.Played << ", stolen " << stats.Stolen << ", refused " << stats.Refused
							  << ", device " << stats.Device << " @ " << stats.SampleRate << " Hz\n";
				});
			commands->Register({ "audio_play", "Plays a sound file or group by name (2D, ui bus)", Category, "<path|group> [volume]" },
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

#if SWIM_AUDIO_MINIAUDIO
		if (I.output != AudioOutput::Off)
		{
			I.running.store(true);
			I.thread = std::thread([&I] { I.ThreadMain(); });
		}
#endif
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
		I.sounds.push_back({ std::move(resolved), load });
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

		I.Send(std::move(command));
		++I.played;
		return { index, voice.Generation };
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
