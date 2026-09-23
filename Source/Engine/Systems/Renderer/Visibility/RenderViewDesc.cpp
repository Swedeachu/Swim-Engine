#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"

#include <cmath>

namespace Swim::Render
{
	GpuViewRecord BuildGpuViewRecord(const RenderViewDesc& desc)
	{
		GpuViewRecord view;
		const auto& m = desc.ViewProjection;
		for (int i = 0; i < 16; ++i)
		{
			view.ViewProjection[i] = m[i];
		}
		const auto row = [&](int r, int c)
		{
			return m[r * 4 + c];
		};
		// Gribb-Hartmann planes for clip x, y in [-w, w] and z in [0, w].
		const float planes[6][4] = {
			{ row(3, 0) + row(0, 0), row(3, 1) + row(0, 1), row(3, 2) + row(0, 2), row(3, 3) + row(0, 3) }, // Left
			{ row(3, 0) - row(0, 0), row(3, 1) - row(0, 1), row(3, 2) - row(0, 2), row(3, 3) - row(0, 3) }, // Right
			{ row(3, 0) + row(1, 0), row(3, 1) + row(1, 1), row(3, 2) + row(1, 2), row(3, 3) + row(1, 3) }, // Bottom
			{ row(3, 0) - row(1, 0), row(3, 1) - row(1, 1), row(3, 2) - row(1, 2), row(3, 3) - row(1, 3) }, // Top
			{ row(2, 0), row(2, 1), row(2, 2), row(2, 3) },													// z >= 0
			{ row(3, 0) - row(2, 0), row(3, 1) - row(2, 1), row(3, 2) - row(2, 2), row(3, 3) - row(2, 3) }, // z <= w
		};
		for (int p = 0; p < 6; ++p)
		{
			const float length = std::sqrt(planes[p][0] * planes[p][0] + planes[p][1] * planes[p][1] + planes[p][2] * planes[p][2]);
			float* out = view.FrustumPlanes + p * 4;
			if (length < 1.0e-12f)
			{
				// Degenerate (for example the far plane of an infinite projection): never culls.
				out[0] = out[1] = out[2] = 0.0f;
				out[3] = 1.0f;
				continue;
			}
			for (int c = 0; c < 4; ++c)
			{
				out[c] = planes[p][c] / length;
			}
		}
		for (int i = 0; i < 3; ++i)
		{
			view.CameraPosition[i] = desc.CameraPosition[i];
		}
		view.LodScale = desc.LodScale;
		view.LodPixelError = desc.LodPixelError;
		view.LodHysteresis = desc.LodHysteresis;
		view.Flags = desc.Flags;
		return view;
	}

	std::array<float, 16> MultiplyRowMajor(const std::array<float, 16>& a, const std::array<float, 16>& b)
	{
		std::array<float, 16> result{};
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				float sum = 0.0f;
				for (int k = 0; k < 4; ++k)
				{
					sum += a[r * 4 + k] * b[k * 4 + c];
				}
				result[r * 4 + c] = sum;
			}
		}
		return result;
	}

	std::array<float, 16> OrthographicRowMajor(float left, float right, float bottom, float top, float nearPlane, float farPlane)
	{
		std::array<float, 16> m{};
		m[0] = 2.0f / (right - left);
		m[3] = -(right + left) / (right - left);
		m[5] = 2.0f / (top - bottom);
		m[7] = -(top + bottom) / (top - bottom);
		// View space looks down -Z: z_view = -near -> 0, -far -> 1.
		m[10] = -1.0f / (farPlane - nearPlane);
		m[11] = -nearPlane / (farPlane - nearPlane);
		m[15] = 1.0f;
		return m;
	}

	std::array<float, 16> PerspectiveRowMajor(float verticalFov, float aspect, float nearPlane, float farPlane)
	{
		std::array<float, 16> m{};
		const float f = 1.0f / std::tan(verticalFov * 0.5f);
		m[0] = f / aspect;
		m[5] = f;
		m[10] = farPlane / (nearPlane - farPlane);
		m[11] = nearPlane * farPlane / (nearPlane - farPlane);
		m[14] = -1.0f;
		return m;
	}
} // namespace Swim::Render
