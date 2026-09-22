#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"
#include "Engine/Systems/Renderer/RenderGraph/GraphStagingDesc.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::Render::Internal
{
	// Staged buffers are suballocated from executor-owned arenas instead of
	// being pooled transient objects or caller-owned imports.
	enum class GraphStaging : std::uint8_t
	{
		None,
		Upload,
		Readback
	};

	struct GraphResource
	{
		GraphKind Kind = GraphKind::Buffer;
		std::string Name;
		Rhi::BufferDesc Buffer{};
		Rhi::TextureDesc Texture{};
		Rhi::RhiObject* Imported = nullptr;
		Rhi::ResourceState Initial = Rhi::ResourceState::Undefined;
		Rhi::ResourceState Final = Rhi::ResourceState::Undefined;
		bool Exported = false;
		GraphStaging Staging = GraphStaging::None;
		std::uint64_t Alignment = 4;
		GraphUploadWriter Writer;

		// Contents exist before the first pass: defined imports and uploads.
		bool InitiallyDefined() const { return (Imported && Initial != Rhi::ResourceState::Undefined) || Staging == GraphStaging::Upload; }

		// Imports and staged buffers own their physical storage outside the pool.
		bool Poolable() const { return !Imported && Staging == GraphStaging::None; }
	};
} // namespace Swim::Render::Internal
