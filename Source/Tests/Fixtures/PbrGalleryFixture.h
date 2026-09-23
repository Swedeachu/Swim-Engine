#pragma once
// The PBR image-regression gallery (critical-path item 62): a grid of analytic
// sphere impostors spanning metallic/dielectric materials and the full roughness
// range, lit by one directional light and image-based lighting. The CPU renderer
// here is the golden reference; Shaders/Slang/RhiSmoke/PbrGallery.slang renders the
// same image on the GPU. Pixel centers and normals are computed identically on
// both sides, so every covered pixel compares directly.
#include "Engine/Systems/Renderer/Environment/EnvironmentReference.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace Swim::Testing::PbrGallery
{
	namespace Env = Render::Environment;
	namespace Pbr = Render::StandardPbr;
	using Float4 = std::array<float, 4>;

	struct Sphere
	{
		float CenterX = 0.0f; // Pixels, x right.
		float CenterY = 0.0f; // Pixels, y down.
		float Radius = 0.0f;
		Pbr::Float3 BaseColor{ 1, 1, 1 };
		float Metallic = 0.0f;
		float Roughness = 0.0f;
		float Occlusion = 1.0f;
	};

	struct Layout
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t Columns = 0;
		std::uint32_t Rows = 0;
		std::vector<Sphere> Spheres; // Row-major.
	};

	// Rows (materials) x columns (roughness 0 .. 1).
	struct Material
	{
		const char* Name;
		Pbr::Float3 BaseColor;
		float Metallic;
		float Occlusion;
	};

	inline const std::array<Material, 4>& Materials()
	{
		static const std::array<Material, 4> materials{ {
			{ "gold", { 1.0f, 0.766f, 0.336f }, 1.0f, 1.0f },
			{ "red plastic", { 0.8f, 0.05f, 0.05f }, 0.0f, 1.0f },
			{ "white dielectric", { 1.0f, 1.0f, 1.0f }, 0.0f, 1.0f },
			{ "half-metal copper, occluded", { 0.955f, 0.638f, 0.538f }, 0.5f, 0.6f },
		} };
		return materials;
	}

	inline constexpr std::uint32_t WhiteDielectricRow = 2;

	inline Layout MakeLayout(std::uint32_t cell, std::uint32_t columns = 6)
	{
		Layout layout;
		layout.Columns = columns;
		layout.Rows = static_cast<std::uint32_t>(Materials().size());
		layout.Width = cell * columns;
		layout.Height = cell * layout.Rows;
		for (std::uint32_t row = 0; row < layout.Rows; ++row)
		{
			const auto& material = Materials()[row];
			for (std::uint32_t column = 0; column < columns; ++column)
			{
				Sphere sphere;
				sphere.CenterX = (float(column) + 0.5f) * float(cell);
				sphere.CenterY = (float(row) + 0.5f) * float(cell);
				sphere.Radius = 0.42f * float(cell);
				sphere.BaseColor = material.BaseColor;
				sphere.Metallic = material.Metallic;
				sphere.Roughness = columns > 1 ? float(column) / float(columns - 1) : 0.5f;
				sphere.Occlusion = material.Occlusion;
				layout.Spheres.push_back(sphere);
			}
		}
		return layout;
	}

	struct Frame
	{
		Pbr::Float3 LightDirection{ 0, 0, 1 }; // Toward the light.
		Pbr::Float3 LightRadiance{ 0, 0, 0 };
		Env::EnvironmentLighting Environment;
	};

	struct Coverage
	{
		std::uint32_t Sphere = 0;
		float OffsetX = 0.0f; // Unit-disk coordinates, y up.
		float OffsetY = 0.0f;
		float RadiusSquared = 0.0f;
		float EdgeDistance = 0.0f; // Pixels from the silhouette (the fragment test is r^2 < 1).
	};

	// The sphere whose impostor covers pixel (x, y)'s center, if any (spheres do not overlap).
	inline std::optional<Coverage> Cover(const Layout& layout, std::uint32_t x, std::uint32_t y)
	{
		const float px = float(x) + 0.5f;
		const float py = float(y) + 0.5f;
		for (std::uint32_t index = 0; index < layout.Spheres.size(); ++index)
		{
			const auto& sphere = layout.Spheres[index];
			const float ox = (px - sphere.CenterX) / sphere.Radius;
			const float oy = (sphere.CenterY - py) / sphere.Radius;
			const float r2 = ox * ox + oy * oy;
			if (r2 < 1.0f)
			{
				return Coverage{ index, ox, oy, r2, (1.0f - std::sqrt(r2)) * sphere.Radius };
			}
		}
		return std::nullopt;
	}

	inline Pbr::ResolvedSurface Surface(const Sphere& sphere, const Coverage& coverage)
	{
		Pbr::ResolvedSurface surface;
		surface.Normal = Pbr::Normalize({ coverage.OffsetX, coverage.OffsetY, std::sqrt(1.0f - coverage.RadiusSquared) });
		surface.BaseColor = sphere.BaseColor;
		surface.Metallic = sphere.Metallic;
		surface.PerceptualRoughness = sphere.Roughness;
		surface.Occlusion = sphere.Occlusion;
		surface.Emissive = { 0, 0, 0 };
		surface.Alpha = 1.0f;
		return surface;
	}

	inline Float4 ShadePixel(const Layout& layout, const Env::EnvironmentProbe& probe, const Frame& frame, const Coverage& coverage)
	{
		const auto surface = Surface(layout.Spheres[coverage.Sphere], coverage);
		Pbr::Lighting lighting;
		lighting.View = { 0, 0, 1 };
		lighting.LightDirection = Pbr::Normalize(frame.LightDirection);
		lighting.LightRadiance = frame.LightRadiance;
		lighting.Ambient = { 0, 0, 0 };
		const auto environment = probe.Lookup(surface, lighting.View, frame.Environment);
		return Pbr::ShadeResolved(surface, lighting, &environment);
	}

	// The whole reference image (RGBA, rows top to bottom); uncovered pixels are 0.
	inline std::vector<Float4> Render(const Layout& layout, const Env::EnvironmentProbe& probe, const Frame& frame)
	{
		std::vector<Float4> image(std::size_t(layout.Width) * layout.Height, Float4{ 0, 0, 0, 0 });
		for (std::uint32_t y = 0; y < layout.Height; ++y)
		{
			for (std::uint32_t x = 0; x < layout.Width; ++x)
			{
				if (const auto coverage = Cover(layout, x, y))
				{
					image[std::size_t(y) * layout.Width + x] = ShadePixel(layout, probe, frame, *coverage);
				}
			}
		}
		return image;
	}

	inline float Luminance(const Float4& color)
	{
		return 0.2126f * color[0] + 0.7152f * color[1] + 0.0722f * color[2];
	}

	// Optional image dumps for inspection: a linear .pfm and a tone-mapped (Reinhard,
	// sRGB) .bmp per image. Returns false when a file cannot be written.
	inline bool WriteImages(const std::string& stem, const std::vector<Float4>& image, std::uint32_t width, std::uint32_t height)
	{
		std::ofstream pfm(stem + ".pfm", std::ios::binary | std::ios::trunc);
		const std::string header = "PF\n" + std::to_string(width) + " " + std::to_string(height) + "\n-1.0\n";
		pfm.write(header.data(), std::streamsize(header.size()));
		for (std::uint32_t row = height; row-- > 0;) // PFM stores rows bottom to top.
		{
			for (std::uint32_t x = 0; x < width; ++x)
			{
				pfm.write(reinterpret_cast<const char*>(image[std::size_t(row) * width + x].data()), 3 * sizeof(float));
			}
		}
		pfm.close();

		std::ofstream bmp(stem + ".bmp", std::ios::binary | std::ios::trunc);
		const std::uint32_t rowBytes = (width * 3 + 3) & ~3u;
		const std::uint32_t dataBytes = rowBytes * height;
		const auto put16 = [&](std::uint16_t value)
		{
			bmp.write(reinterpret_cast<const char*>(&value), 2);
		};
		const auto put32 = [&](std::uint32_t value)
		{
			bmp.write(reinterpret_cast<const char*>(&value), 4);
		};
		put16(0x4D42);
		put32(54 + dataBytes);
		put32(0);
		put32(54);
		put32(40);
		put32(width);
		put32(height);
		put16(1);
		put16(24);
		put32(0);
		put32(dataBytes);
		put32(2835);
		put32(2835);
		put32(0);
		put32(0);
		std::vector<char> row(rowBytes, 0);
		for (std::uint32_t y = height; y-- > 0;) // BMP stores rows bottom to top.
		{
			for (std::uint32_t x = 0; x < width; ++x)
			{
				const auto& p = image[std::size_t(y) * width + x];
				for (int c = 0; c < 3; ++c)
				{
					const float mapped = std::max(p[c], 0.0f) / (1.0f + std::max(p[c], 0.0f));
					row[x * 3 + std::size_t(2 - c)] =
						static_cast<char>(static_cast<std::uint8_t>(std::lround(Pbr::LinearToSrgb(mapped) * 255.0f)));
				}
			}
			bmp.write(row.data(), std::streamsize(row.size()));
		}
		bmp.close();
		return pfm.good() && bmp.good();
	}
} // namespace Swim::Testing::PbrGallery
