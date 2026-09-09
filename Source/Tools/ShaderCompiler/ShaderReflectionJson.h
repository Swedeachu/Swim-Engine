#pragma once

#include <simdjson.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace Swim::ShaderCompiler::Detail
{

	inline std::optional<simdjson::dom::element> FindField(
		simdjson::dom::object object,
		std::string_view name)
	{
		auto result = object[name];
		if (result.error())
		{
			return std::nullopt;
		}
		return result.value_unsafe();
	}

	inline std::string ReadString(simdjson::dom::object object, std::string_view name)
	{
		const auto field = FindField(object, name);
		if (!field)
		{
			return {};
		}

		std::string_view value;
		if (field->get_string().get(value))
		{
			return {};
		}
		return std::string(value);
	}

	inline bool ReadU32(simdjson::dom::object object, std::string_view name, std::uint32_t& outValue)
	{
		const auto field = FindField(object, name);
		if (!field)
		{
			return false;
		}

		std::uint64_t value = 0;
		if (field->get_uint64().get(value))
		{
			return false;
		}
		if (value > std::numeric_limits<std::uint32_t>::max())
		{
			return false;
		}

		outValue = static_cast<std::uint32_t>(value);
		return true;
	}

} // namespace Swim::ShaderCompiler::Detail
