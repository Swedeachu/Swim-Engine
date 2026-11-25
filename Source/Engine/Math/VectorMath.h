#pragma once

#include <glm/glm.hpp>

#include <array>

// Small glm companions. Renderer feature and reference APIs pass vectors as
// std::array<float, N> (their headers stay glm-free); these convert at that boundary so
// the math in between is plain glm.
namespace Engine
{

	inline glm::vec2 ToGlm(const std::array<float, 2>& v)
	{
		return { v[0], v[1] };
	}

	inline glm::vec3 ToGlm(const std::array<float, 3>& v)
	{
		return { v[0], v[1], v[2] };
	}

	inline glm::vec4 ToGlm(const std::array<float, 4>& v)
	{
		return { v[0], v[1], v[2], v[3] };
	}

	inline std::array<float, 2> ToArray(const glm::vec2& v)
	{
		return { v.x, v.y };
	}

	inline std::array<float, 3> ToArray(const glm::vec3& v)
	{
		return { v.x, v.y, v.z };
	}

	inline std::array<float, 4> ToArray(const glm::vec4& v)
	{
		return { v.x, v.y, v.z, v.w };
	}

	// glm::normalize (v times 1 / |v|, bit for bit), or `fallback` when |v| <= minLength,
	// where glm::normalize would divide by zero.
	inline glm::vec3 NormalizeOr(const glm::vec3& v, const glm::vec3& fallback, float minLength = 0.0f)
	{
		const float length = glm::length(v);
		return length > minLength ? v * (1.0f / length) : fallback;
	}

} // namespace Engine
