#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Commands
{
	class CommandRegistry;
}

namespace Engine
{

	// A sound file registered with the audio system (loaded lazily on first play).
	struct SoundHandle
	{
		std::uint32_t Id = 0;

		explicit operator bool() const { return Id != 0; }

		bool operator==(const SoundHandle&) const = default;
	};

	// One playing instance. Stale handles (finished, stolen) are ignored by every call.
	struct VoiceHandle
	{
		std::uint32_t Index = 0;
		std::uint32_t Generation = 0;

		explicit operator bool() const { return Generation != 0; }

		bool operator==(const VoiceHandle&) const = default;
	};

	// A mixer bus (a volume and a parent). The built-in buses always exist.
	struct BusHandle
	{
		std::uint32_t Id = 0;

		bool operator==(const BusHandle&) const = default;
	};

	namespace AudioBus
	{
		inline constexpr BusHandle Master{ 0 };
		inline constexpr BusHandle Music{ 1 };
		inline constexpr BusHandle World{ 2 };
		inline constexpr BusHandle Players{ 3 };
		inline constexpr BusHandle Ambient{ 4 };
		inline constexpr BusHandle Ui{ 5 };
	} // namespace AudioBus

	enum class AudioOutput : std::uint8_t
	{
		Device, // The default (or audio_device) output device.
		Null,	// No device: voices advance in real time and finish, nothing is heard (headless runs).
		Off,	// No audio at all: every call is a no-op.
	};

	enum class SoundLoad : std::uint8_t
	{
		Auto,	 // Stream files over 1 MB, decode the rest.
		Decoded, // Decoded once on a job thread and cached by path (short effects).
		Stream,	 // Decoded while playing (music, long ambience).
	};

	enum class SoundAttenuationModel : std::uint8_t
	{
		Inverse,
		Linear,
		Exponential,
	};

	struct SoundAttenuation
	{
		SoundAttenuationModel Model = SoundAttenuationModel::Inverse;
		float MinDistance = 1.0f;  // Full volume inside.
		float MaxDistance = 64.0f; // No further attenuation beyond.
		float Rolloff = 1.0f;
		float Doppler = 0.0f; // 0 = no pitch shift from relative velocity.
	};

	struct PlayParams
	{
		BusHandle Bus = AudioBus::World;
		float Volume = 1.0f;
		float Pitch = 1.0f;
		float Pan = 0.0f; // -1..1, non-positional voices only.
		bool Loop = false;
		float StartSeconds = 0.0f;
		float FadeInSeconds = 0.0f;
		// Set: a positional voice attenuated and panned against the listener.
		std::optional<glm::vec3> Position;
		glm::vec3 Velocity{ 0.0f };
		SoundAttenuation Attenuation;
		// When the voice limit is reached the lowest-priority, oldest voice is stolen if its
		// priority is not above this one's; otherwise this play is refused.
		int Priority = 0;
		SoundLoad Load = SoundLoad::Auto;
		// Main thread, from AudioSystem::Update: the voice ended (finished, stopped or stolen).
		std::function<void(VoiceHandle)> OnFinished;
		// Timeline markers in seconds; OnMarker(voice, index into Markers) as playback passes each.
		std::vector<float> Markers;
		std::function<void(VoiceHandle, std::uint32_t)> OnMarker;
	};

	enum class SoundPick : std::uint8_t
	{
		Random,
		RandomNoRepeat,
		Sequence,
	};

	// A named set of variations. Sounds may use * and ? in the file name (expanded from
	// that one folder the first time the group plays).
	struct SoundGroupDesc
	{
		std::vector<std::string> Sounds;
		SoundPick Pick = SoundPick::RandomNoRepeat;
		float VolumeMin = 1.0f;
		float VolumeMax = 1.0f;
		float PitchMin = 1.0f;
		float PitchMax = 1.0f;
		std::uint32_t MaxInstances = 0; // 0 = unlimited; at the limit the oldest instance is stopped.
		float CooldownSeconds = 0.0f;	// Plays sooner than this after the last one are dropped.
		BusHandle Bus = AudioBus::World;
		int Priority = 0;
		bool Loop = false;
		SoundLoad Load = SoundLoad::Auto;
		SoundAttenuation Attenuation;
	};

	// Per-play overrides of a group or event (multiplied with the group's variation).
	struct SoundEventParams
	{
		std::optional<glm::vec3> Position;
		glm::vec3 Velocity{ 0.0f };
		float Volume = 1.0f;
		float Pitch = 1.0f;
		float FadeInSeconds = 0.0f;
		std::function<void(VoiceHandle)> OnFinished;
	};

	struct AudioListener
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Forward{ 0.0f, 0.0f, -1.0f };
		glm::vec3 Up{ 0.0f, 1.0f, 0.0f };
		glm::vec3 Velocity{ 0.0f };
	};

	struct AudioStats
	{
		std::uint32_t Voices = 0;
		std::uint32_t MaxVoices = 0;
		std::uint32_t Sounds = 0;
		std::uint32_t Groups = 0;
		std::uint64_t Played = 0;
		std::uint64_t Stolen = 0;
		std::uint64_t Refused = 0; // Voice limit, cooldown or a missing file.
		std::string Device;		   // Output device name ("null" / "off" without one).
		std::uint32_t SampleRate = 0;
		// Glitches since start: device callbacks slower than the audio they produced, callbacks
		// that came after the device's buffer ran dry, and dry spells of playing streams.
		std::uint64_t CallbackOverruns = 0;
		std::uint64_t CallbackLate = 0;
		std::uint64_t StreamStarved = 0;
	};

	struct AudioSystemDesc
	{
		AudioOutput Output = AudioOutput::Device;
		std::uint32_t SampleRate = 48000;
		std::uint32_t Channels = 2;
		// Relative sound paths resolve against this folder.
		std::filesystem::path AssetRoot;
	};

	// Playback, 3D sound, buses, sound groups and events over a backend running on its
	// own threads. Every call is main-thread only and never blocks on decoding or the
	// device: requests go through a lock-free queue to the audio thread, and finished and
	// marker callbacks come back through Update.
	//
	// Console variables (category "Audio", saved): audio_volume_<bus> 0..1 per bus
	// (master, music, world, players, ambient, ui and AddBus buses), audio_device (empty =
	// system default), audio_max_voices. Commands: audio_devices, audio_stats,
	// audio_play <path> [volume].
	class AudioSystem
	{

	  public:

		AudioSystem(const AudioSystemDesc& desc, Swim::Commands::CommandRegistry* commands);

		~AudioSystem();

		AudioSystem(const AudioSystem&) = delete;

		AudioSystem& operator=(const AudioSystem&) = delete;

		// Delivers callbacks and applies the listener (camera: the engine camera, used while
		// the listener is automatic; its velocity is derived from the position change).
		void Update(double deltaSeconds, const std::optional<AudioListener>& camera = std::nullopt);

		// "<name>:/relative/path" resolves to folder/relative/path (read only).
		void MountRoot(std::string name, std::filesystem::path folder);

		bool IsRootMounted(std::string_view name) const;

		std::filesystem::path ResolvePath(std::string_view path) const;

		// Registers a file (no IO until it plays). The same path returns the same handle.
		SoundHandle LoadSound(std::string_view path, SoundLoad load = SoundLoad::Auto);

		// Starts decoding now so the first play has no load delay.
		void Preload(SoundHandle sound);

		VoiceHandle Play(SoundHandle sound, const PlayParams& params = {});

		VoiceHandle Play(std::string_view path, const PlayParams& params = {});

		void Stop(VoiceHandle voice, float fadeSeconds = 0.0f);

		void StopBus(BusHandle bus, float fadeSeconds = 0.0f);

		void SetPaused(VoiceHandle voice, bool paused);

		void SetVolume(VoiceHandle voice, float volume, float fadeSeconds = 0.0f);

		void SetPitch(VoiceHandle voice, float pitch);

		void SetPosition(VoiceHandle voice, const glm::vec3& position, const glm::vec3& velocity = glm::vec3(0.0f));

		bool IsPlaying(VoiceHandle voice) const;

		// A bus under parent with its own saved volume variable audio_volume_<name>.
		BusHandle AddBus(std::string name, BusHandle parent = AudioBus::Master);

		std::optional<BusHandle> FindBus(std::string_view name) const;

		void SetBusVolume(BusHandle bus, float volume);

		float GetBusVolume(BusHandle bus) const;

		void RegisterGroup(std::string name, SoundGroupDesc desc);

		bool HasGroup(std::string_view name) const;

		VoiceHandle PlayGroup(std::string_view group, const SoundEventParams& params = {});

		// Gameplay events name what happened; MapEvent points one at a group (an unmapped
		// event plays the group of the same name).
		void MapEvent(std::string event, std::string group);

		VoiceHandle PlayEvent(std::string_view event, const SoundEventParams& params = {});

		// The listener follows the engine camera until SetListener is called
		// (SetListenerAutomatic(true) returns it to the camera).
		void SetListener(const AudioListener& listener);

		void SetListenerAutomatic(bool automatic) { listenerAutomatic = automatic; }

		bool IsListenerAutomatic() const { return listenerAutomatic; }

		// Output device names (empty when audio is off).
		std::vector<std::string> ListDevices() const;

		// Switches the output device (empty = system default); also the audio_device variable.
		void SetDevice(std::string name);

		AudioStats GetStats() const;

		AudioOutput GetOutput() const;

	  private:

		struct Impl;
		std::unique_ptr<Impl> impl;
		bool listenerAutomatic = true;

	};

} // namespace Engine
