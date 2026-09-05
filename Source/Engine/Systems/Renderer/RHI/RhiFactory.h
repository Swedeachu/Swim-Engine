#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <array>
#include <cstddef>
#include <memory>

namespace Swim::Rhi
{

	using GraphicsSystemCreateFunction = std::unique_ptr<GraphicsSystem>(*)();

	class GraphicsFactory
	{
	public:
		bool Register(GraphicsApi api, GraphicsSystemCreateFunction createFunction)
		{
			if (api == GraphicsApi::Count || createFunction == nullptr)
			{
				return false;
			}

			auto& slot = createFunctions[ToIndex(api)];
			if (slot != nullptr)
			{
				return false;
			}

			slot = createFunction;
			return true;
		}

		bool Unregister(GraphicsApi api)
		{
			if (api == GraphicsApi::Count)
			{
				return false;
			}

			auto& slot = createFunctions[ToIndex(api)];
			if (slot == nullptr)
			{
				return false;
			}

			slot = nullptr;
			return true;
		}

		bool IsAvailable(GraphicsApi api) const
		{
			return api != GraphicsApi::Count && createFunctions[ToIndex(api)] != nullptr;
		}

		std::unique_ptr<GraphicsSystem> Create(GraphicsApi api) const
		{
			if (!IsAvailable(api))
			{
				return nullptr;
			}
			return createFunctions[ToIndex(api)]();
		}

	private:
		static constexpr std::size_t ToIndex(GraphicsApi api)
		{
			return static_cast<std::size_t>(api);
		}

		std::array<GraphicsSystemCreateFunction, static_cast<std::size_t>(GraphicsApi::Count)> createFunctions{};
	};

} // namespace Swim::Rhi
