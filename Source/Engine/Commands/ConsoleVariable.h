#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Commands
{

	enum class VariableType : uint8_t
	{
		Bool,
		Int,
		Float,
		Enum,
		String,
		Color
	};

	enum class VariableFlags : uint32_t
	{
		None = 0,
		// Written to the user config file by CommandRegistry::SaveVariables.
		Saved = 1u << 0,
		// A change made after startup is stored (and saved) but takes effect on the next
		// run; until then the getters keep returning the active value.
		RequiresRestart = 1u << 1,
		// Only code may change it (SetValue with force); the console reports it read-only.
		ReadOnly = 1u << 2,
		// Left out of settings listings (still reachable by name).
		Hidden = 1u << 3,
		// Meant for development tools only (settings screens can filter these out).
		Development = 1u << 4
	};

	constexpr VariableFlags operator|(VariableFlags a, VariableFlags b)
	{
		return static_cast<VariableFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}

	constexpr VariableFlags operator&(VariableFlags a, VariableFlags b)
	{
		return static_cast<VariableFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
	}

	constexpr bool HasFlag(VariableFlags flags, VariableFlags flag)
	{
		return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
	}

	// Linear RGBA; parsed from "#RRGGBB", "#RRGGBBAA" or "r g b [a]" (0..1 floats).
	struct VariableColor
	{
		float R = 1.0f;
		float G = 1.0f;
		float B = 1.0f;
		float A = 1.0f;

		bool operator==(const VariableColor&) const = default;
	};

	// Everything the settings UI and tools need to present a variable. Numbers (bool,
	// int, float, enum index) share DefaultNumber/Min/Max; Min > Max means unbounded.
	struct VariableDesc
	{
		std::string Name;
		std::string Description;
		// Slash-separated settings path, for example "Video/Display".
		std::string Category;
		VariableType Type = VariableType::Float;
		VariableFlags Flags = VariableFlags::None;
		double DefaultNumber = 0.0;
		double Min = 1.0;
		double Max = 0.0;
		// Optional step for sliders (0 = continuous / 1 for integers).
		double Step = 0.0;
		std::string DefaultText;
		VariableColor DefaultColor{};
		// Enum choices; the value is an index into this list.
		std::vector<std::string> Choices;
	};

	// One typed console variable. Owned by the CommandRegistry; the reference stays valid
	// until the variable is unregistered, so hot code keeps it and reads the value
	// directly (no lookup, no allocation). Reads and writes are main-thread only.
	class ConsoleVariable
	{

	public:

		using ChangeCallback = std::function<void(const ConsoleVariable&)>;
		using SubscriptionId = uint32_t;

		explicit ConsoleVariable(VariableDesc desc);

		const VariableDesc& GetDesc() const { return desc; }

		const std::string& GetName() const { return desc.Name; }

		VariableType GetType() const { return desc.Type; }

		bool GetBool() const { return active.Number != 0.0; }

		int32_t GetInt() const { return static_cast<int32_t>(active.Number); }

		float GetFloat() const { return static_cast<float>(active.Number); }

		double GetDouble() const { return active.Number; }

		uint32_t GetEnum() const { return static_cast<uint32_t>(active.Number); }

		std::string_view GetEnumName() const;

		const std::string& GetString() const { return active.Text; }

		VariableColor GetColor() const { return active.Color; }

		// Parses console text for this variable's type (bool words, numbers, enum names or
		// indices, colors). Numbers are clamped to the range. False when unparsable or
		// read-only (unless force).
		bool SetFromText(std::string_view text, bool force = false);

		void SetBool(bool value) { SetNumber(value ? 1.0 : 0.0); }

		void SetInt(int64_t value) { SetNumber(static_cast<double>(value)); }

		void SetFloat(double value) { SetNumber(value); }

		void SetEnum(uint32_t index) { SetNumber(static_cast<double>(index)); }

		bool SetEnum(std::string_view name);

		void SetString(std::string value);

		void SetColor(VariableColor value);

		void Reset();

		// The stored value (the pending one when a restart is pending), as console text.
		std::string ToString() const;

		std::string DefaultToString() const;

		bool IsDefault() const;

		bool IsRestartPending() const { return restartPending; }

		// Called after every effective change (including a latched restart value).
		SubscriptionId Subscribe(ChangeCallback callback);

		void Unsubscribe(SubscriptionId id);

	private:

		friend class CommandRegistry;

		struct Value
		{
			double Number = 0.0;
			std::string Text;
			VariableColor Color{};

			bool operator==(const Value&) const = default;
		};

		void SetNumber(double value);

		void Apply(Value value);

		std::string Format(const Value& value) const;

		VariableDesc desc;
		Value active;
		Value pending;
		bool restartPending = false;
		// Set by the registry once startup is over: from then on RequiresRestart latches.
		bool latchRestart = false;
		std::vector<std::pair<SubscriptionId, ChangeCallback>> subscribers;
		SubscriptionId nextSubscription = 1;
		// Registry-level change hook (dirty tracking and global listeners).
		std::function<void(const ConsoleVariable&)> registryHook;

	};

} // namespace Swim::Commands
