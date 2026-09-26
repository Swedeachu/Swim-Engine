#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"

#include "Engine/Assets/AssetSystem.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyService.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Engine
{
	namespace
	{
		constexpr std::array<std::string_view, static_cast<std::size_t>(BuiltinMesh::Count)> BuiltinNames{ "Cube", "Sphere", "Plane",
			"Cylinder", "Cone", "Torus", "Capsule" };

		ProceduralMeshes::MeshData MakeBuiltin(BuiltinMesh mesh)
		{
			switch (mesh)
			{
			case BuiltinMesh::Cube:
				return ProceduralMeshes::MakeBox();
			case BuiltinMesh::Sphere:
				return ProceduralMeshes::MakeSphere(0.5f, 48, 24);
			case BuiltinMesh::Plane:
				return ProceduralMeshes::MakePlane(1.0f, 1, 1.0f);
			case BuiltinMesh::Cylinder:
				return ProceduralMeshes::MakeCylinder(0.5f, 1.0f, 40);
			case BuiltinMesh::Cone:
				return ProceduralMeshes::MakeCone(0.5f, 1.0f, 40);
			case BuiltinMesh::Torus:
				return ProceduralMeshes::MakeTorus(0.4f, 0.15f, 48, 24);
			case BuiltinMesh::Capsule:
				return ProceduralMeshes::MakeCapsule(0.25f, 0.5f, 32, 10);
			case BuiltinMesh::Count:
				break;
			}
			throw std::invalid_argument("Unknown built-in mesh");
		}

		std::string ProceduralPath(std::string_view name)
		{
			return "Procedural/" + std::string(name);
		}

		float SrgbToLinear(float value)
		{
			return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
		}

		float LinearToSrgb(float value)
		{
			return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
		}
	} // namespace

	Swim::Assets::TextureAsset MakeTextureAsset(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba8, bool srgb)
	{
		using namespace Swim::Assets;
		if (!width || !height || rgba8.size() != std::size_t(width) * height * 4)
		{
			throw std::invalid_argument("MakeTextureAsset: pixel data does not match the size");
		}
		TextureAsset texture;
		texture.Dimension = TextureDimension::Texture2D;
		texture.ColorSpace = srgb ? TextureColorSpace::SRgb : TextureColorSpace::Linear;
		texture.Semantic = srgb ? TextureSemantic::Color : TextureSemantic::Data;
		texture.Width = width;
		texture.Height = height;
		TexturePayloadVariant payload;
		payload.Container = TextureContainerFormat::NativeMipData;
		payload.Format = srgb ? TexturePayloadFormat::RGBA8SRgb : TexturePayloadFormat::RGBA8UNorm;

		std::vector<float> level(rgba8.size());
		for (std::size_t i = 0; i < rgba8.size(); ++i)
		{
			const float value = float(rgba8[i]) / 255.0f;
			level[i] = srgb && i % 4 != 3 ? SrgbToLinear(value) : value;
		}
		std::uint32_t w = width;
		std::uint32_t h = height;
		while (true)
		{
			TextureMipDesc mip;
			mip.Width = w;
			mip.Height = h;
			mip.OffsetBytes = payload.Bytes.size();
			mip.SizeBytes = std::uint64_t(w) * h * 4;
			mip.UncompressedSizeBytes = mip.SizeBytes;
			for (std::size_t i = 0; i < level.size(); ++i)
			{
				const float value = srgb && i % 4 != 3 ? LinearToSrgb(level[i]) : level[i];
				payload.Bytes.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f)));
			}
			payload.Mips.push_back(mip);
			if (w == 1 && h == 1)
			{
				break;
			}
			const std::uint32_t nw = std::max(w / 2, 1u);
			const std::uint32_t nh = std::max(h / 2, 1u);
			std::vector<float> next(std::size_t(nw) * nh * 4, 0.0f);
			for (std::uint32_t y = 0; y < nh; ++y)
			{
				for (std::uint32_t x = 0; x < nw; ++x)
				{
					for (std::uint32_t c = 0; c < 4; ++c)
					{
						float sum = 0.0f;
						int count = 0;
						for (std::uint32_t dy = 0; dy < 2; ++dy)
						{
							for (std::uint32_t dx = 0; dx < 2; ++dx)
							{
								const std::uint32_t sx = std::min(x * 2 + dx, w - 1);
								const std::uint32_t sy = std::min(y * 2 + dy, h - 1);
								sum += level[(std::size_t(sy) * w + sx) * 4 + c];
								++count;
							}
						}
						next[(std::size_t(y) * nw + x) * 4 + c] = sum / float(count);
					}
				}
			}
			level = std::move(next);
			w = nw;
			h = nh;
		}
		texture.Payloads.push_back(std::move(payload));
		return texture;
	}

	MeshLibrary::MeshLibrary(Swim::Assets::AssetSystem& assetSystem, Swim::Render::AssetResidencyService& residencyService,
		Swim::Render::GeometryHeap& geometryHeap)
		: assets(assetSystem), residency(residencyService), heap(geometryHeap)
	{
		for (std::size_t i = 0; i < builtins.size(); ++i)
		{
			builtins[i] = Register(BuiltinNames[i], MakeBuiltin(static_cast<BuiltinMesh>(i)));
		}
	}

	MeshLibrary::MeshHandle MeshLibrary::Register(std::string_view name, const ProceduralMeshes::MeshData& mesh)
	{
		const std::string path = ProceduralPath(name);
		auto handle = assets.Declare<Swim::Assets::MeshAsset>(path);
		if (!assets.Publish(handle, ProceduralMeshes::ToMeshAsset(mesh)))
		{
			throw std::runtime_error("MeshLibrary: cannot publish " + path);
		}
		residency.RequestMesh(handle);
		meshes[path] = handle;
		return handle;
	}

	MeshLibrary::MeshHandle MeshLibrary::Find(std::string_view name) const
	{
		const auto found = meshes.find(ProceduralPath(name));
		if (found != meshes.end())
		{
			return found->second;
		}
		const auto cooked = meshes.find(std::string(name));
		return cooked == meshes.end() ? MeshHandle{} : cooked->second;
	}

	MeshLibrary::MeshHandle MeshLibrary::Load(std::string_view logicalPath)
	{
		auto handle = assets.Find<Swim::Assets::MeshAsset>(logicalPath);
		if (!handle.IsValid())
		{
			return {};
		}
		residency.RequestMesh(handle);
		meshes[std::string(logicalPath)] = handle;
		return handle;
	}

	MeshLibrary::TextureHandle MeshLibrary::RegisterTexture(
		std::string_view name, std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba8, bool srgb)
	{
		const std::string path = ProceduralPath(name);
		auto handle = assets.Declare<Swim::Assets::TextureAsset>(path);
		if (!assets.Publish(handle, MakeTextureAsset(width, height, rgba8, srgb)))
		{
			throw std::runtime_error("MeshLibrary: cannot publish texture " + path);
		}
		residency.RequestTexture(handle);
		textures[path] = handle;
		return handle;
	}

	MeshLibrary::TextureHandle MeshLibrary::FindTexture(std::string_view name) const
	{
		const auto found = textures.find(ProceduralPath(name));
		return found == textures.end() ? TextureHandle{} : found->second;
	}

	MeshLibrary::TextureHandle MeshLibrary::RegisterChecker(
		std::string_view name, std::uint32_t size, std::uint32_t cells, std::array<std::uint8_t, 4> a, std::array<std::uint8_t, 4> b)
	{
		size = std::max(size, 2u);
		cells = std::clamp(cells, 1u, size);
		std::vector<std::uint8_t> pixels(std::size_t(size) * size * 4);
		for (std::uint32_t y = 0; y < size; ++y)
		{
			for (std::uint32_t x = 0; x < size; ++x)
			{
				const bool odd = ((x * cells / size) + (y * cells / size)) % 2 != 0;
				const auto& color = odd ? b : a;
				std::copy(color.begin(), color.end(), pixels.begin() + (std::size_t(y) * size + x) * 4);
			}
		}
		return RegisterTexture(name, size, size, pixels, true);
	}

	bool MeshLibrary::IsResident(MeshHandle mesh) const
	{
		return residency.GetState(mesh) == Swim::Render::AssetResidencyState::Resident;
	}

	std::uint32_t MeshLibrary::GetResidentMeshCount() const
	{
		std::uint32_t count = 0;
		for (const auto& [path, handle] : meshes)
		{
			(void)path;
			count += IsResident(handle) ? 1u : 0u;
		}
		return count;
	}

	std::uint32_t MeshLibrary::GetResidentTextureCount() const
	{
		std::uint32_t count = 0;
		for (const auto& [path, handle] : textures)
		{
			(void)path;
			count += residency.GetState(handle) == Swim::Render::AssetResidencyState::Resident ? 1u : 0u;
		}
		return count;
	}

	void MeshLibrary::TrackGpuMesh(Swim::Render::GpuMeshHandle mesh)
	{
		if (mesh && std::find(extraMeshes.begin(), extraMeshes.end(), mesh) == extraMeshes.end())
		{
			extraMeshes.push_back(mesh);
		}
	}

	void MeshLibrary::UntrackGpuMesh(Swim::Render::GpuMeshHandle mesh)
	{
		extraMeshes.erase(std::remove(extraMeshes.begin(), extraMeshes.end(), mesh), extraMeshes.end());
	}

	std::vector<MeshLibrary::PageSlot> MeshLibrary::CollectPageSlots(bool* conflict) const
	{
		std::vector<PageSlot> slots;
		bool clash = false;
		const auto add = [&](Swim::Render::GpuMeshHandle mesh)
		{
			const auto* metadata = mesh ? heap.GetMetadata(mesh) : nullptr;
			if (!metadata || metadata->IndexPage == Swim::Render::GpuMeshMetadata::InvalidPage ||
				metadata->VertexPage == Swim::Render::GpuMeshMetadata::InvalidPage)
			{
				return;
			}
			for (const auto& slot : slots)
			{
				if (slot.IndexPage == metadata->IndexPage)
				{
					clash = clash || slot.VertexPage != metadata->VertexPage;
					return;
				}
			}
			slots.push_back({ metadata->IndexPage, metadata->VertexPage });
		};
		// Deterministic order: builtins first, then the rest by path.
		std::vector<std::string> paths;
		for (const auto& [path, handle] : meshes)
		{
			(void)handle;
			paths.push_back(path);
		}
		std::sort(paths.begin(), paths.end());
		for (const auto& handle : builtins)
		{
			add(residency.GetGpuMesh(handle));
		}
		for (const auto& path : paths)
		{
			add(residency.GetGpuMesh(meshes.at(path)));
		}
		for (const auto mesh : extraMeshes)
		{
			add(mesh);
		}
		if (conflict)
		{
			*conflict = clash;
		}
		return slots;
	}
} // namespace Engine
