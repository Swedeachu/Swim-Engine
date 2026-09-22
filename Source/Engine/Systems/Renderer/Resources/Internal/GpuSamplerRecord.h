#pragma once
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"
#include "Engine/Systems/Renderer/Resources/GpuHandle.h"

#include <memory>

namespace Swim::Render::Internal
{
	struct GpuSamplerRecord
	{
		std::unique_ptr<Rhi::Sampler> Sampler;
		Rhi::SamplerDesc Desc; // DebugName cleared; identity excludes names.
		std::uint32_t References = 0;
		BindlessSamplerHandle Bindless;
		Rhi::TimelinePoint LastUse; // Latest last use reported by any releasing holder.
	};
} // namespace Swim::Render::Internal
