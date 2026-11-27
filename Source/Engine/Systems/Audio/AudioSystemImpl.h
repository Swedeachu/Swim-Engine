#pragma once
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

// The audio system's internals, shared by AudioSystem.cpp (the API) and AudioSystemImpl.cpp
// (the audio thread and the backend).
namespace Engine
{

	namespace AudioDetail
	{

		constexpr const char* Category = "Audio";
		constexpr std::uint32_t MaxSlots = 512; // Voice slots (a stopping voice keeps its slot until it ends).
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

	} // namespace AudioDetail

	struct AudioSystem::Impl
	{
		struct Voice
		{
			std::uint32_t Generation = 0;
			bool Active = false;  // Holds its slot until the backend reports the end.
			bool Counted = false; // Counts against the voice limit (false once stopped or stolen).
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
			std::string Key; // As given to LoadSound.
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
		std::array<Voice, AudioDetail::MaxSlots> voices{};
		std::uint64_t order = 0;
		std::uint64_t played = 0;
		std::function<void(const AudioPlayRecord&)> playObserver;
		std::uint64_t stolen = 0;
		std::uint64_t refused = 0;
		double time = 0.0;
		std::mt19937 random{ 0x5EEDu };
		std::optional<glm::vec3> lastCameraPosition;
		Swim::Commands::ConsoleVariable* maxVoices = nullptr;
		Swim::Commands::ConsoleVariable* device = nullptr;

		AudioDetail::SpscQueue<AudioDetail::Command, 1024> commandQueue;
		AudioDetail::SpscQueue<AudioDetail::Event, 4096> eventQueue;
		std::thread thread;
		std::atomic<bool> running{ false };

#if SWIM_AUDIO_MINIAUDIO
		// Audio thread state.
		struct Slot
		{
			ma_sound Sound{};
			bool Live = false;
			bool Paused = false;
			bool Stream = false;
			bool Starved = false;
			std::uint32_t Generation = 0;
			std::vector<float> Markers;
			std::uint32_t NextMarker = 0;
			float LastCursor = 0.0f;
		};

		ma_engine engine{};
		ma_resource_manager resourceManager{};
		bool resourceManagerReady = false;
		ma_context context{};
		ma_device outputDevice{};
		bool engineReady = false;
		bool contextReady = false;
		bool deviceReady = false;
		std::atomic<bool> deviceLost{ false };
		std::atomic<bool> closingDevice{ false };
		std::string deviceName = "default";
		std::string wantedDevice;
		std::array<ma_sound_group, AudioDetail::MaxBuses> groupsBackend{};
		std::array<bool, AudioDetail::MaxBuses> groupReady{};
		std::vector<Slot> slots = std::vector<Slot>(AudioDetail::MaxSlots);
		std::vector<float> nullScratch;
		std::atomic<std::uint32_t> sampleRate{ 0 };
		std::string deviceNameShared; // Written by the audio thread under deviceNameLock.
		std::atomic_flag deviceNameLock = ATOMIC_FLAG_INIT;

		// Glitch counters for audio_stats: device callbacks that took longer than the audio they
		// produced, callbacks that came late (the device ran dry before them), and streams that
		// had no decoded audio ready while playing.
		std::atomic<std::uint64_t> callbackOverruns{ 0 };
		std::atomic<std::uint64_t> callbackLate{ 0 };
		std::atomic<std::uint64_t> streamStarved{ 0 };
		std::int64_t lastCallback = 0;

		static void DataCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount);

		static void NotificationCallback(const ma_device_notification* notification);

		void PublishDeviceName(std::string name);

		std::string ReadDeviceName();

		void CloseDevice();

		bool OpenDevice(const std::string& name);

		bool StartBackend();

		void StopBackend();

		void CreateBus(std::uint32_t bus, std::uint32_t parent);

		static ma_attenuation_model Model(SoundAttenuationModel model);

		static std::uint64_t Milliseconds(float seconds) { return static_cast<std::uint64_t>(std::max(seconds, 0.0f) * 1000.0f + 0.5f); }

		void Finish(std::uint32_t index);

		void ExecutePlay(AudioDetail::Command& command);

		void Execute(AudioDetail::Command& command);

		void Poll();

		void ThreadMain();
#endif

		void Send(AudioDetail::Command command);

		std::uint32_t CountedVoices() const;

		void StopSlot(std::uint32_t index, float fadeSeconds);

		Voice* Find(VoiceHandle handle);
	};

} // namespace Engine
