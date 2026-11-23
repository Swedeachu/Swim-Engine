#include "ConsoleVariable.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <utility>

namespace Swim::Commands
{

	namespace
	{

		std::string Lower(std::string_view text)
		{
			std::string lower(text);
			std::transform(lower.begin(), lower.end(), lower.begin(),
				[](unsigned char c)
				{
					return static_cast<char>(std::tolower(c));
				});
			return lower;
		}

		bool ParseDouble(std::string_view text, double& value)
		{
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
			{
				text.remove_prefix(1);
			}

			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
			{
				text.remove_suffix(1);
			}

			if (!text.empty() && text.front() == '+')
			{
				text.remove_prefix(1);
			}

			const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
			return !text.empty() && result.ec == std::errc() && result.ptr == text.data() + text.size() && std::isfinite(value);
		}

		bool ParseBool(std::string_view text, double& value)
		{
			const std::string lower = Lower(text);

			if (lower == "1" || lower == "true" || lower == "on" || lower == "yes")
			{
				value = 1.0;
				return true;
			}

			if (lower == "0" || lower == "false" || lower == "off" || lower == "no")
			{
				value = 0.0;
				return true;
			}

			return false;
		}

		bool ParseColor(std::string_view text, VariableColor& color)
		{
			if (!text.empty() && text.front() == '#')
			{
				text.remove_prefix(1);

				if (text.size() != 6 && text.size() != 8)
				{
					return false;
				}

				float channels[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

				for (std::size_t i = 0; i < text.size() / 2; ++i)
				{
					unsigned byte = 0;
					const auto result = std::from_chars(text.data() + i * 2, text.data() + i * 2 + 2, byte, 16);

					if (result.ec != std::errc() || result.ptr != text.data() + i * 2 + 2)
					{
						return false;
					}

					channels[i] = static_cast<float>(byte) / 255.0f;
				}

				color = { channels[0], channels[1], channels[2], channels[3] };
				return true;
			}

			float channels[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			std::size_t count = 0;
			std::size_t start = 0;

			while (start < text.size() && count < 4)
			{
				while (start < text.size() && (std::isspace(static_cast<unsigned char>(text[start])) || text[start] == ','))
				{
					++start;
				}

				std::size_t end = start;

				while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) && text[end] != ',')
				{
					++end;
				}

				if (end == start)
				{
					break;
				}

				double channel = 0.0;

				if (!ParseDouble(text.substr(start, end - start), channel))
				{
					return false;
				}

				channels[count++] = static_cast<float>(channel);
				start = end;
			}

			if (count < 3)
			{
				return false;
			}

			color = { channels[0], channels[1], channels[2], channels[3] };
			return true;
		}

		std::string FormatNumber(double value)
		{
			char buffer[64];
			const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
			return std::string(buffer, result.ptr);
		}

	} // namespace

	ConsoleVariable::ConsoleVariable(VariableDesc descValue) : desc(std::move(descValue))
	{
		if (desc.Type == VariableType::Enum && desc.Choices.empty())
		{
			desc.Choices.push_back("default");
		}

		active.Number = desc.DefaultNumber;
		active.Text = desc.DefaultText;
		active.Color = desc.DefaultColor;
		pending = active;
	}

	std::string_view ConsoleVariable::GetEnumName() const
	{
		const auto index = GetEnum();
		return index < desc.Choices.size() ? std::string_view(desc.Choices[index]) : std::string_view();
	}

	bool ConsoleVariable::SetFromText(std::string_view text, bool force)
	{
		if (HasFlag(desc.Flags, VariableFlags::ReadOnly) && !force)
		{
			return false;
		}

		Value value = restartPending ? pending : active;

		switch (desc.Type)
		{
		case VariableType::Bool:
			if (!ParseBool(text, value.Number))
			{
				return false;
			}

			break;
		case VariableType::Int:
		case VariableType::Float:
			if (!ParseDouble(text, value.Number))
			{
				double flag = 0.0;

				if (desc.Type != VariableType::Int || !ParseBool(text, flag))
				{
					return false;
				}

				value.Number = flag;
			}

			break;
		case VariableType::Enum:
		{
			const std::string lower = Lower(text);
			bool found = false;

			for (std::size_t i = 0; i < desc.Choices.size(); ++i)
			{
				if (Lower(desc.Choices[i]) == lower)
				{
					value.Number = static_cast<double>(i);
					found = true;
					break;
				}
			}

			double index = 0.0;

			if (!found && (!ParseDouble(text, index) || index < 0.0 || index >= static_cast<double>(desc.Choices.size())))
			{
				return false;
			}

			if (!found)
			{
				value.Number = std::floor(index);
			}

			break;
		}
		case VariableType::String:
			value.Text = std::string(text);
			break;
		case VariableType::Color:
			if (!ParseColor(text, value.Color))
			{
				return false;
			}

			break;
		}

		Apply(std::move(value));
		return true;
	}

	bool ConsoleVariable::SetEnum(std::string_view name)
	{
		if (desc.Type != VariableType::Enum)
		{
			return false;
		}

		return SetFromText(name, true);
	}

	void ConsoleVariable::SetNumber(double number)
	{
		Value value = restartPending ? pending : active;
		value.Number = number;
		Apply(std::move(value));
	}

	void ConsoleVariable::SetString(std::string text)
	{
		Value value = restartPending ? pending : active;
		value.Text = std::move(text);
		Apply(std::move(value));
	}

	void ConsoleVariable::SetColor(VariableColor color)
	{
		Value value = restartPending ? pending : active;
		value.Color = color;
		Apply(std::move(value));
	}

	void ConsoleVariable::Reset()
	{
		Value value;
		value.Number = desc.DefaultNumber;
		value.Text = desc.DefaultText;
		value.Color = desc.DefaultColor;
		Apply(std::move(value));
	}

	void ConsoleVariable::Apply(Value value)
	{
		switch (desc.Type)
		{
		case VariableType::Bool:
			value.Number = value.Number != 0.0 ? 1.0 : 0.0;
			break;
		case VariableType::Int:
			value.Number = std::round(value.Number);
			break;
		case VariableType::Enum:
			value.Number = std::clamp(std::floor(value.Number), 0.0, static_cast<double>(desc.Choices.size() - 1));
			break;
		default:
			break;
		}

		if ((desc.Type == VariableType::Int || desc.Type == VariableType::Float) && desc.Min <= desc.Max)
		{
			value.Number = std::clamp(value.Number, desc.Min, desc.Max);
		}

		if (latchRestart && HasFlag(desc.Flags, VariableFlags::RequiresRestart))
		{
			if (value == (restartPending ? pending : active))
			{
				return;
			}

			restartPending = !(value == active);
			pending = std::move(value);
		}
		else
		{
			if (value == active)
			{
				return;
			}

			active = std::move(value);
			pending = active;
		}

		// Copy: a subscriber may unsubscribe itself.
		const auto callbacks = subscribers;

		for (const auto& [id, callback] : callbacks)
		{
			(void)id;
			callback(*this);
		}

		if (registryHook)
		{
			registryHook(*this);
		}
	}

	std::string ConsoleVariable::Format(const Value& value) const
	{
		switch (desc.Type)
		{
		case VariableType::Bool:
			return value.Number != 0.0 ? "1" : "0";
		case VariableType::Int:
			return std::to_string(static_cast<int64_t>(value.Number));
		case VariableType::Float:
			return FormatNumber(value.Number);
		case VariableType::Enum:
		{
			const auto index = static_cast<std::size_t>(value.Number);
			return index < desc.Choices.size() ? desc.Choices[index] : std::to_string(index);
		}
		case VariableType::String:
			return value.Text;
		case VariableType::Color:
		{
			const auto byte = [](float channel)
			{
				return static_cast<unsigned>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
			};
			char buffer[16];
			std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", byte(value.Color.R), byte(value.Color.G), byte(value.Color.B),
				byte(value.Color.A));
			return buffer;
		}
		}

		return {};
	}

	std::string ConsoleVariable::ToString() const
	{
		return Format(restartPending ? pending : active);
	}

	std::string ConsoleVariable::DefaultToString() const
	{
		Value value;
		value.Number = desc.DefaultNumber;
		value.Text = desc.DefaultText;
		value.Color = desc.DefaultColor;
		return Format(value);
	}

	bool ConsoleVariable::IsDefault() const
	{
		return ToString() == DefaultToString();
	}

	ConsoleVariable::SubscriptionId ConsoleVariable::Subscribe(ChangeCallback callback)
	{
		const SubscriptionId id = nextSubscription++;
		subscribers.emplace_back(id, std::move(callback));
		return id;
	}

	void ConsoleVariable::Unsubscribe(SubscriptionId id)
	{
		std::erase_if(subscribers,
			[id](const auto& entry)
			{
				return entry.first == id;
			});
	}

} // namespace Swim::Commands
