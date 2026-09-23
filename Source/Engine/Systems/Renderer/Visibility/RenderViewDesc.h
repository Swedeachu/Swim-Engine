#pragma once
#include "Engine/Systems/Renderer/Visibility/GpuViewRecord.h"

#include <array>

namespace Swim::Render
{
	// Inputs for BuildGpuViewRecord. ViewProjection is row-major (clip = M * p) and
	// maps visible depth to [0, 1], which holds for both conventional and reverse-Z
	// projections; an infinite far plane yields an always-passing far plane.
	struct RenderViewDesc
	{
		std::array<float, 16> ViewProjection{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		std::array<float, 3> CameraPosition{};
		float LodScale = 1.0f;
		float LodPixelError = 1.0f;
		float LodHysteresis = 0.25f;
		std::uint32_t Flags = 0; // GpuViewFlags.
	};

	GpuViewRecord BuildGpuViewRecord(const RenderViewDesc& desc);

	// Row-major multiply helpers for building view-projection matrices on the CPU.
	std::array<float, 16> MultiplyRowMajor(const std::array<float, 16>& a, const std::array<float, 16>& b);
	// Orthographic projection (right-handed view looking down -Z) to clip depth [0, 1].
	std::array<float, 16> OrthographicRowMajor(float left, float right, float bottom, float top, float nearPlane, float farPlane);
	// Perspective projection (right-handed, looking down -Z) to clip depth [0, 1].
	std::array<float, 16> PerspectiveRowMajor(float verticalFov, float aspect, float nearPlane, float farPlane);
} // namespace Swim::Render
