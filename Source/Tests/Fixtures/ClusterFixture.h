#pragma once
// Shared scenes for the clustered lighting tests (items 64-65, 68): a camera
// (row-major look-at view + reverse-Z infinite perspective) and random point/spot
// lights around it, encoded into a GpuLightBuffer-style row array.
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterGrid.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"

#include <array>
#include <cmath>
#include <random>
#include <vector>

namespace Swim::Testing::ClusterScene
{
	using Float3 = std::array<float, 3>;

	// Right-handed look-at, row-major world -> view (the camera looks down -Z).
	inline std::array<float, 16> LookAt(const Float3& eye, const Float3& target, const Float3& up)
	{
		const auto f = Render::StandardPbr::Normalize({ target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] });
		const Float3 sRaw{ f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
		const auto s = Render::StandardPbr::Normalize(sRaw);
		const Float3 u{ s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
		const auto dot = [](const Float3& a, const Float3& b)
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		};
		return { s[0], s[1], s[2], -dot(s, eye), u[0], u[1], u[2], -dot(u, eye), -f[0], -f[1], -f[2], dot(f, eye), 0, 0, 0, 1 };
	}

	inline Render::ClusterView Camera(float aspect, float verticalFov = 1.0f, float nearPlane = 0.1f)
	{
		Render::ClusterView view;
		view.View = LookAt({ 0, 4, 18 }, { 0, 2, 0 }, { 0, 1, 0 });
		view.Projection = Render::PerspectiveReverseZRowMajor(verticalFov, aspect, nearPlane);
		return view;
	}

	struct Scene
	{
		std::vector<Render::GpuLightRecord> Rows;
		Render::GpuLightHeader Header;
		std::vector<Render::LightDesc> Descs; // Local lights in row order, then directional.
	};

	// `directional` directional lights and `local` point/spot lights scattered in a
	// 40 x 12 x 55 m box around the origin (some behind the camera at z = 18).
	inline Scene RandomScene(
		std::uint32_t directional, std::uint32_t local, std::uint32_t seed, float minRange = 1.0f, float maxRange = 6.0f)
	{
		std::mt19937 random(seed);
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		std::normal_distribution<float> normal(0.0f, 1.0f);
		Scene scene;
		scene.Header.DirectionalCount = directional;
		scene.Header.LocalCount = local;
		scene.Header.FirstLocalRow = std::max(directional, 1u);
		scene.Header.LocalCapacity = local;
		scene.Rows.resize(scene.Header.FirstLocalRow + local);
		for (std::uint32_t i = 0; i < directional; ++i)
		{
			Render::LightDesc desc;
			desc.Type = Render::LightType::Directional;
			desc.Direction = { normal(random), -1.0f - unit(random), normal(random) };
			desc.Intensity = 0.5f + unit(random);
			scene.Rows[i] = Render::Lights::EncodeLight(desc);
		}
		for (std::uint32_t i = 0; i < local; ++i)
		{
			Render::LightDesc desc;
			desc.Type = unit(random) < 0.5f ? Render::LightType::Point : Render::LightType::Spot;
			desc.Position = { 40 * unit(random) - 20, 12 * unit(random) - 2, 55 * unit(random) - 25 };
			desc.Direction = { normal(random), normal(random), normal(random) };
			desc.Color = { 0.3f + unit(random), 0.3f + unit(random), 0.3f + unit(random) };
			desc.Intensity = 1.0f + 10.0f * unit(random);
			desc.Range = minRange + (maxRange - minRange) * unit(random);
			desc.OuterConeAngle = 0.2f + 1.3f * unit(random);
			desc.InnerConeAngle = desc.OuterConeAngle * 0.8f * unit(random);
			scene.Rows[scene.Header.FirstLocalRow + i] = Render::Lights::EncodeLight(desc);
			scene.Descs.push_back(desc);
		}
		return scene;
	}

	// A view-space point at a pixel center and view depth, and its world position.
	inline Float3 ViewPoint(const Render::ClusterGridRecord& grid, float pixelX, float pixelY, float depth)
	{
		const float ndcX = pixelX / float(grid.Limits[0]) * 2.0f - 1.0f;
		const float ndcY = 1.0f - pixelY / float(grid.Limits[1]) * 2.0f;
		return { depth * (ndcX + grid.Projection[2]) / grid.Projection[0], depth * (ndcY + grid.Projection[3]) / grid.Projection[1],
			-depth };
	}

	// Inverse of an affine row-major view (rotation + translation).
	inline Float3 ViewToWorld(const Render::ClusterGridRecord& grid, const Float3& v)
	{
		Float3 t{ v[0] - grid.ViewRows[0][3], v[1] - grid.ViewRows[1][3], v[2] - grid.ViewRows[2][3] };
		Float3 world{ 0, 0, 0 };
		for (int column = 0; column < 3; ++column)
		{
			for (int row = 0; row < 3; ++row)
			{
				world[column] += grid.ViewRows[row][column] * t[row];
			}
		}
		return world;
	}
} // namespace Swim::Testing::ClusterScene
