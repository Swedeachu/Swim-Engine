#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::Render::Internal
{
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
	};
} // namespace Swim::Render::Internal
