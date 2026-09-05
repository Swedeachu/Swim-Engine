#pragma once

#include "Engine/Systems/Physics/Backends/Jolt/Internal/JoltPhysicsUtils.h"
#include "Engine/Systems/Physics/Backends/Jolt/JoltWorldBackend.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Engine
{

	using namespace JoltPhysicsDetail;

	class JoltWorldBackend::BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
	{

	public:

		struct RegisteredLayer
		{
			CollisionLayer Collision{};
			bool IsMoving = false;
		};

		JPH::ObjectLayer Register(MotionType motion, CollisionLayer collision)
		{
			const LayerKey key{ collision.Layer, collision.Mask, motion != MotionType::Static };
			const auto found = registeredLayerLookup.find(key);
			if (found != registeredLayerLookup.end())
			{
				return found->second;
			}

			if (registeredLayers.size() >= static_cast<std::size_t>(JPH::cObjectLayerInvalid))
			{
				return JPH::cObjectLayerInvalid;
			}

			const JPH::ObjectLayer objectLayer = static_cast<JPH::ObjectLayer>(registeredLayers.size());
			registeredLayers.push_back(RegisteredLayer{ collision, key.IsMoving });
			registeredLayerLookup.emplace(key, objectLayer);
			return objectLayer;
		}

		const RegisteredLayer* Get(JPH::ObjectLayer layer) const
		{
			const std::size_t index = static_cast<std::size_t>(layer);
			return index < registeredLayers.size() ? &registeredLayers[index] : nullptr;
		}

		JPH::uint GetNumBroadPhaseLayers() const override
		{
			return BroadPhaseLayers::Count;
		}

		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
		{
			const RegisteredLayer* registeredLayer = Get(layer);
			JPH_ASSERT(registeredLayer != nullptr);
			return registeredLayer && registeredLayer->IsMoving
				? BroadPhaseLayers::Moving
				: BroadPhaseLayers::NonMoving;
		}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
		{
			switch (static_cast<JPH::BroadPhaseLayer::Type>(layer))
			{
				case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::NonMoving):
					return "NON_MOVING";
				case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::Moving):
					return "MOVING";
				default:
					return "INVALID";
			}
		}
#endif

	private:

		struct LayerKey
		{
			std::uint32_t Layer = 0;
			std::uint32_t Mask = 0;
			bool IsMoving = false;

			bool operator==(const LayerKey&) const = default;
		};

		struct LayerKeyHash
		{
			std::size_t operator()(const LayerKey& key) const noexcept
			{
				std::uint64_t packed = (static_cast<std::uint64_t>(key.Layer) << 32u) | key.Mask;
				packed ^= packed >> 33u;
				packed *= 0xff51afd7ed558ccdULL;
				packed ^= packed >> 33u;
				return static_cast<std::size_t>(packed ^ (key.IsMoving ? 0x9e3779b97f4a7c15ULL : 0ULL));
			}
		};

		std::vector<RegisteredLayer> registeredLayers;
		std::unordered_map<LayerKey, JPH::ObjectLayer, LayerKeyHash> registeredLayerLookup;

	};

} // namespace Engine
