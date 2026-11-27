#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"

#include "Engine/Assets/AssetSystem.h"
#include "Engine/Math/ColorMath.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
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

	} // namespace

	Swim::Assets::TextureAsset MakeTextureAsset(
		std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba8, bool srgb, bool mips)
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

		// The whole chain is sized up front; mip 0 is the input bytes as they are.
		std::uint64_t total = 0;

		for (std::uint32_t w = width, h = height;; w = std::max(w / 2, 1u), h = std::max(h / 2, 1u))
		{
			TextureMipDesc mip;
			mip.Width = w;
			mip.Height = h;
			mip.OffsetBytes = total;
			mip.SizeBytes = std::uint64_t(w) * h * 4;
			mip.UncompressedSizeBytes = mip.SizeBytes;
			payload.Mips.push_back(mip);
			total += mip.SizeBytes;

			if (!mips || (w == 1 && h == 1))
			{
				break;
			}
		}

		payload.Bytes.resize(total);
		std::memcpy(payload.Bytes.data(), rgba8.data(), rgba8.size());

		if (payload.Mips.size() == 1)
		{
			texture.Payloads.push_back(std::move(payload));
			return texture;
		}

		// Levels are filtered in linear float (colour premultiplied by alpha for sRGB) and
		// encoded through tables; no pow per texel.
		const auto& decode = Srgb8ToLinearTable();
		const auto encodePlain = [](float value)
		{
			return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
		};

		// Mip 1 reads the input bytes through the decode table; later levels read floats.
		std::vector<float> level;
		std::vector<float> next;
		const auto fetch = [&](std::size_t texel, float out[4])
		{
			if (level.empty())
			{
				const std::uint8_t* p = &rgba8[texel * 4];

				for (int c = 0; c < 3; ++c)
				{
					out[c] = srgb ? decode[p[c]] : float(p[c]) * (1.0f / 255.0f);
				}

				out[3] = float(p[3]) * (1.0f / 255.0f);
			}
			else
			{
				std::memcpy(out, &level[texel * 4], sizeof(float) * 4);
			}
		};
		std::uint32_t w = width;
		std::uint32_t h = height;

		for (std::size_t m = 1; m < payload.Mips.size(); ++m)
		{
			const std::uint32_t nw = payload.Mips[m].Width;
			const std::uint32_t nh = payload.Mips[m].Height;
			next.assign(std::size_t(nw) * nh * 4, 0.0f);
			auto* bytes = reinterpret_cast<std::uint8_t*>(payload.Bytes.data() + payload.Mips[m].OffsetBytes);

			for (std::uint32_t y = 0; y < nh; ++y)
			{
				const std::uint32_t y0 = std::min(y * 2, h - 1);
				const std::uint32_t y1 = std::min(y * 2 + 1, h - 1);

				for (std::uint32_t x = 0; x < nw; ++x)
				{
					const std::uint32_t x0 = std::min(x * 2, w - 1);
					const std::uint32_t x1 = std::min(x * 2 + 1, w - 1);
					float texels[4][4];
					fetch(std::size_t(y0) * w + x0, texels[0]);
					fetch(std::size_t(y0) * w + x1, texels[1]);
					fetch(std::size_t(y1) * w + x0, texels[2]);
					fetch(std::size_t(y1) * w + x1, texels[3]);
					// sRGB colour is averaged by alpha, so clear texels (often white or black) never
					// bleed into cut-out edges at a distance; data and alpha take the plain average.
					float colour[3] = {};
					float plain[3] = {};
					float alpha = 0.0f;

					for (const auto& texel : texels)
					{
						for (int c = 0; c < 3; ++c)
						{
							colour[c] += texel[c] * texel[3];
							plain[c] += texel[c];
						}

						alpha += texel[3];
					}

					const std::size_t at = (std::size_t(y) * nw + x) * 4;
					float* out = &next[at];

					for (int c = 0; c < 3; ++c)
					{
						out[c] = srgb && alpha > 0.0f ? colour[c] / alpha : plain[c] * 0.25f;
						bytes[at + c] = srgb ? LinearToSrgb8(out[c]) : encodePlain(out[c]);
					}

					out[3] = alpha * 0.25f;
					bytes[at + 3] = encodePlain(out[3]);
				}
			}

			std::swap(level, next);
			w = nw;
			h = nh;
		}

		texture.Payloads.push_back(std::move(payload));
		return texture;
	}

	void MeshLibrary::MakeUrgent(MeshHandle mesh)
	{
		residency.MakeUrgent(Resolve(mesh));
	}

	void MeshLibrary::MakeUrgent(TextureHandle texture)
	{
		residency.MakeUrgent(Resolve(texture));
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
		++meshesVersion;
		auto& slot = meshes[path];

		// A resident asset is never re-uploaded, so every registration publishes its own asset
		// and a replaced one is retired once the GPU is done with it.
		if (slot.IsValid())
		{
			released.push_back(slot);
			replaced[slot.GetId().Value] = path;
			++meshReplacements;
			++replacements;
		}

		const std::string assetPath = path + "#" + std::to_string(revision++);
		auto handle = assets.Declare<Swim::Assets::MeshAsset>(assetPath);

		if (!assets.Publish(handle, ProceduralMeshes::ToMeshAsset(mesh)))
		{
			throw std::runtime_error("MeshLibrary: cannot publish " + assetPath);
		}

		residency.RequestMesh(handle);
		slot = handle;
		names[handle.GetId().Value] = path;

		if (sourceObserver.Mesh)
		{
			sourceObserver.Mesh(handle, path, mesh);
		}

		return handle;
	}

	void MeshLibrary::Unregister(std::string_view name)
	{
		const auto found = meshes.find(ProceduralPath(name));

		if (found != meshes.end())
		{
			released.push_back(found->second);
			meshes.erase(found);
			++meshesVersion;
		}
	}

	void MeshLibrary::FlushReleases(Swim::Rhi::TimelinePoint lastUse)
	{
		for (const auto& handle : released)
		{
			names.erase(handle.GetId().Value);
			residency.ReleaseMesh(handle, lastUse);
			assets.Forget(handle);
		}

		released.clear();

		for (const auto& handle : releasedTextures)
		{
			names.erase(handle.GetId().Value);
			residency.ReleaseTexture(handle, lastUse);
			assets.Forget(handle);
		}

		releasedTextures.clear();
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
		++meshesVersion;
		names[handle.GetId().Value] = std::string(logicalPath);
		return handle;
	}

	MeshLibrary::TextureHandle MeshLibrary::RegisterTexture(
		std::string_view name, std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba8, bool srgb, bool mips)
	{
		const std::string path = ProceduralPath(name);
		auto& slot = textures[path];

		// As with meshes, a replacement is a new asset; materials pointing at the old one keep
		// it until they are updated and the GPU is done with it.
		if (slot.IsValid())
		{
			releasedTextures.push_back(slot);
			replaced[slot.GetId().Value] = path;
			++replacements;
		}

		const std::string assetPath = path + "#" + std::to_string(revision++);
		auto handle = assets.Declare<Swim::Assets::TextureAsset>(assetPath);

		if (!assets.Publish(handle, MakeTextureAsset(width, height, rgba8, srgb, mips)))
		{
			throw std::runtime_error("MeshLibrary: cannot publish texture " + assetPath);
		}

		residency.RequestTexture(handle);
		slot = handle;
		names[handle.GetId().Value] = path;

		if (sourceObserver.Texture)
		{
			sourceObserver.Texture(handle, path, width, height, rgba8, srgb, mips);
		}

		return handle;
	}

	std::string_view MeshLibrary::FindName(MeshHandle mesh) const
	{
		const auto found = names.find(mesh.GetId().Value);
		return found == names.end() ? std::string_view{} : std::string_view(found->second);
	}

	std::string_view MeshLibrary::FindTextureName(TextureHandle texture) const
	{
		const auto found = names.find(texture.GetId().Value);
		return found == names.end() ? std::string_view{} : std::string_view(found->second);
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
		return residency.GetState(Resolve(mesh)) == Swim::Render::AssetResidencyState::Resident;
	}

	bool MeshLibrary::IsResident(TextureHandle texture) const
	{
		return residency.GetState(Resolve(texture)) == Swim::Render::AssetResidencyState::Resident;
	}

	std::uint32_t MeshLibrary::GetBindlessIndex(TextureHandle texture) const
	{
		return residency.GetBindlessIndex(Resolve(texture));
	}

	void MeshLibrary::UnregisterTexture(std::string_view name)
	{
		const auto found = textures.find(ProceduralPath(name));

		if (found != textures.end())
		{
			releasedTextures.push_back(found->second);
			textures.erase(found);
		}
	}

	MeshLibrary::MeshHandle MeshLibrary::Resolve(MeshHandle mesh) const
	{
		if (replaced.empty() || !mesh.IsValid())
		{
			return mesh;
		}

		const auto path = replaced.find(mesh.GetId().Value);

		if (path == replaced.end())
		{
			return mesh;
		}

		const auto current = meshes.find(path->second);
		return current == meshes.end() ? mesh : current->second;
	}

	MeshLibrary::TextureHandle MeshLibrary::Resolve(TextureHandle texture) const
	{
		if (replaced.empty() || !texture.IsValid())
		{
			return texture;
		}

		const auto path = replaced.find(texture.GetId().Value);

		if (path == replaced.end())
		{
			return texture;
		}

		const auto current = textures.find(path->second);
		return current == textures.end() ? texture : current->second;
	}

	MeshLibrary::Stats MeshLibrary::GetStats() const
	{
		Stats stats;
		stats.Meshes = static_cast<std::uint32_t>(meshes.size());
		stats.Textures = static_cast<std::uint32_t>(textures.size());
		stats.ResidentMeshes = GetResidentMeshCount();
		stats.ResidentTextures = GetResidentTextureCount();
		stats.PendingReleases = static_cast<std::uint32_t>(released.size() + releasedTextures.size());
		stats.Replacements = replacements;
		return stats;
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
			++meshesVersion;
		}
	}

	void MeshLibrary::UntrackGpuMesh(Swim::Render::GpuMeshHandle mesh)
	{
		extraMeshes.erase(std::remove(extraMeshes.begin(), extraMeshes.end(), mesh), extraMeshes.end());
		++meshesVersion;
	}

	const std::vector<MeshLibrary::PageSlot>& MeshLibrary::CollectPageSlots(bool* conflict) const
	{
		// Builtins are fixed; meshes map to GPU meshes only through heap creation/destruction.
		if (pageSlotsKey[0] == meshesVersion && pageSlotsKey[1] == heap.GetGeneration())
		{
			if (conflict)
			{
				*conflict = pageSlotsConflict;
			}

			return pageSlots;
		}

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
		// Deterministic order: builtins first, then the rest by path (sorted again only when
		// the meshes change, not on every heap change).
		if (sortedMeshesVersion != meshesVersion)
		{
			std::vector<std::pair<const std::string*, MeshHandle>> byPath;
			byPath.reserve(meshes.size());

			for (const auto& [path, handle] : meshes)
			{
				byPath.emplace_back(&path, handle);
			}

			std::sort(byPath.begin(), byPath.end(),
				[](const auto& a, const auto& b)
				{
					return *a.first < *b.first;
				});
			sortedMeshes.clear();

			for (const auto& entry : byPath)
			{
				sortedMeshes.push_back(entry.second);
			}

			sortedMeshesVersion = meshesVersion;
		}

		for (const auto& handle : builtins)
		{
			add(residency.GetGpuMesh(handle));
		}

		for (const auto& handle : sortedMeshes)
		{
			add(residency.GetGpuMesh(handle));
		}

		for (const auto mesh : extraMeshes)
		{
			add(mesh);
		}

		if (conflict)
		{
			*conflict = clash;
		}

		pageSlotsKey[0] = meshesVersion;
		pageSlotsKey[1] = heap.GetGeneration();
		pageSlots = std::move(slots);
		pageSlotsConflict = clash;
		return pageSlots;
	}

} // namespace Engine
