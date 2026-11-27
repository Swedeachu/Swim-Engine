#include "Engine/Systems/Audio/AudioSystemImpl.h"

namespace Engine
{

	using namespace AudioDetail;

#if SWIM_AUDIO_MINIAUDIO
	void AudioSystem::Impl::DataCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount)
	{
		auto* self = static_cast<Impl*>(device->pUserData);
		using Nanoseconds = std::chrono::nanoseconds;
		const auto start = std::chrono::duration_cast<Nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		ma_engine_read_pcm_frames(&self->engine, output, frameCount, nullptr);
		const auto end = std::chrono::duration_cast<Nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		const double period = double(frameCount) * 1.0e9 / double(std::max(device->sampleRate, 1u));

		if (double(end - start) > period)
		{
			self->callbackOverruns.fetch_add(1, std::memory_order_relaxed);
		}

		// A buffer of `periods` periods: a gap longer than all of it played silence.
		if (self->lastCallback != 0 && double(start - self->lastCallback) > period * double(std::max(device->playback.internalPeriods, 2u)))
		{
			self->callbackLate.fetch_add(1, std::memory_order_relaxed);
		}

		self->lastCallback = start;
	}

	void AudioSystem::Impl::NotificationCallback(const ma_device_notification* notification)
	{
		auto* self = static_cast<Impl*>(notification->pDevice->pUserData);

		if (notification->type == ma_device_notification_type_stopped && !self->closingDevice.load())
		{
			self->deviceLost.store(true);
		}
	}

	void AudioSystem::Impl::PublishDeviceName(std::string name)
	{
		while (deviceNameLock.test_and_set(std::memory_order_acquire))
		{
		}

		deviceNameShared = std::move(name);
		deviceNameLock.clear(std::memory_order_release);
	}

	std::string AudioSystem::Impl::ReadDeviceName()
	{
		while (deviceNameLock.test_and_set(std::memory_order_acquire))
		{
		}

		auto copy = deviceNameShared;
		deviceNameLock.clear(std::memory_order_release);
		return copy;
	}

	void AudioSystem::Impl::CloseDevice()
	{
		if (deviceReady)
		{
			closingDevice.store(true);
			ma_device_uninit(&outputDevice);
			deviceReady = false;
			closingDevice.store(false);
		}
	}

	bool AudioSystem::Impl::OpenDevice(const std::string& name)
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
		// 3 x 20 ms: enough slack for the mix to ride out a loaded CPU, still 60 ms at most.
		config.performanceProfile = ma_performance_profile_conservative;
		config.periodSizeInMilliseconds = 20;
		config.periods = 3;
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

	bool AudioSystem::Impl::StartBackend()
	{
		// Two decoding threads, so one slow stream page does not hold up the others.
		ma_resource_manager_config resources = ma_resource_manager_config_init();
		resources.decodedFormat = ma_format_f32;
		resources.decodedChannels = desc.Channels;
		resources.decodedSampleRate = desc.SampleRate;
		resources.jobThreadCount = 2;
		resourceManagerReady = ma_resource_manager_init(&resources, &resourceManager) == MA_SUCCESS;
		ma_engine_config config = ma_engine_config_init();
		config.pResourceManager = resourceManagerReady ? &resourceManager : nullptr;
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
			// The device thread runs at the highest priority: a mix that starts late under a
			// loaded CPU is heard as a periodic chug.
			ma_context_config contextConfig = ma_context_config_init();
			contextConfig.threadPriority = ma_thread_priority_highest;
			contextReady = ma_context_init(nullptr, 0, &contextConfig, &context) == MA_SUCCESS;

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

	void AudioSystem::Impl::StopBackend()
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

		if (resourceManagerReady)
		{
			ma_resource_manager_uninit(&resourceManager);
			resourceManagerReady = false;
		}

		if (contextReady)
		{
			ma_context_uninit(&context);
			contextReady = false;
		}
	}

	void AudioSystem::Impl::CreateBus(std::uint32_t bus, std::uint32_t parent)
	{
		if (bus >= MaxBuses || groupReady[bus])
		{
			return;
		}

		ma_sound_group* parentGroup = parent < MaxBuses && groupReady[parent] ? &groupsBackend[parent] : nullptr;
		groupReady[bus] = ma_sound_group_init(&engine, 0, parentGroup, &groupsBackend[bus]) == MA_SUCCESS;
	}

	ma_attenuation_model AudioSystem::Impl::Model(SoundAttenuationModel model)
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

	void AudioSystem::Impl::Finish(std::uint32_t index)
	{
		auto& slot = slots[index];

		if (slot.Live)
		{
			ma_sound_uninit(&slot.Sound);
			slot.Live = false;
		}

		eventQueue.Push({ EventType::Finished, index, slot.Generation, 0 });
	}

	void AudioSystem::Impl::ExecutePlay(Command& command)
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

		const bool stream = params.Load == SoundLoad::Stream || (params.Load == SoundLoad::Auto && size > StreamThreshold);
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
		slot.Stream = stream;
		slot.Starved = false;
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

	void AudioSystem::Impl::Execute(Command& command)
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

	void AudioSystem::Impl::Poll()
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

			if (slot.Stream)
			{
				// Counted once per dry spell: nothing decoded ahead of a stream that is not at its end.
				ma_uint64 available = 0;
				ma_uint64 cursor = 0;
				ma_uint64 length = 0;
				auto* source = slot.Sound.pResourceManagerDataSource;
				const bool known = source && ma_resource_manager_data_source_get_available_frames(source, &available) == MA_SUCCESS;
				const bool ended = ma_sound_get_cursor_in_pcm_frames(&slot.Sound, &cursor) == MA_SUCCESS &&
					ma_sound_get_length_in_pcm_frames(&slot.Sound, &length) == MA_SUCCESS && length > 0 && cursor + 64 >= length;
				const bool starved = known && available == 0 && !ended;

				if (starved && !slot.Starved)
				{
					streamStarved.fetch_add(1, std::memory_order_relaxed);
				}

				slot.Starved = starved;
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

	void AudioSystem::Impl::ThreadMain()
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

	void AudioSystem::Impl::Send(Command command)
	{
		if (output != AudioOutput::Off)
		{
			commandQueue.Push(std::move(command));
		}
	}

	std::uint32_t AudioSystem::Impl::CountedVoices() const
	{
		std::uint32_t count = 0;

		for (const auto& voice : voices)
		{
			count += voice.Counted ? 1u : 0u;
		}

		return count;
	}

	void AudioSystem::Impl::StopSlot(std::uint32_t index, float fadeSeconds)
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

	AudioSystem::Impl::Voice* AudioSystem::Impl::Find(VoiceHandle handle)
	{
		if (handle.Index >= MaxSlots)
		{
			return nullptr;
		}

		auto& voice = voices[handle.Index];
		return voice.Active && voice.Generation == handle.Generation ? &voice : nullptr;
	}

} // namespace Engine
