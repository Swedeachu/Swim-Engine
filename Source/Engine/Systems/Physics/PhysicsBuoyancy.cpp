#include "PhysicsBuoyancy.h"
#include "PhysicsWorld.h"

#include <algorithm>
#include <cmath>

namespace Engine
{

	namespace
	{

		bool Finite(const glm::vec3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		bool Inside(const FluidVolumeDesc& fluid, const glm::vec3& p)
		{
			return p.x >= fluid.BoundsMin.x && p.y >= fluid.BoundsMin.y && p.z >= fluid.BoundsMin.z && p.x <= fluid.BoundsMax.x &&
				p.y <= fluid.BoundsMax.y && p.z <= fluid.BoundsMax.z;
		}

		FluidSample SampleFluid(const FluidVolumeDesc& fluid, float x, float z, double time)
		{
			if (!fluid.Surface)
			{
				return { fluid.SurfaceHeight, glm::vec3(0.0f) };
			}

			auto sample = fluid.Surface(x, z, time);
			return std::isfinite(sample.SurfaceHeight) && Finite(sample.FlowVelocity) ? sample
																					  : FluidSample{ fluid.SurfaceHeight, glm::vec3(0.0f) };
		}

	} // namespace

	BuoyancyDesc BuoyancyDesc::Box(const glm::vec3& halfExtents)
	{
		BuoyancyDesc desc;
		desc.HalfExtents = halfExtents;
		desc.Volume = 8.0f * halfExtents.x * halfExtents.y * halfExtents.z;
		return desc;
	}

	BuoyancyDesc BuoyancyDesc::Floater(const glm::vec3& halfExtents, float density)
	{
		BuoyancyDesc desc = Box(halfExtents);
		desc.Density = density;
		desc.HeaveDamping = 2.0f;
		desc.NeverSink = true;
		return desc;
	}

	BuoyancyDesc BuoyancyDesc::Sphere(float radius)
	{
		BuoyancyDesc desc;
		// The sample grid spans the cube of the sphere's volume, so depth maps to volume
		// about as it does for the sphere.
		const float half = 0.5f * std::cbrt(4.0f / 3.0f * 3.14159265f) * radius;
		desc.HalfExtents = glm::vec3(half);
		desc.Volume = 4.0f / 3.0f * 3.14159265f * radius * radius * radius;
		return desc;
	}

	BuoyancyDesc BuoyancyDesc::Capsule(float radius, float halfHeight)
	{
		BuoyancyDesc desc;
		desc.Volume = 3.14159265f * radius * radius * (2.0f * halfHeight + 4.0f / 3.0f * radius);
		const float side = std::sqrt(3.14159265f) * 0.5f * radius; // Square of the circle's area.
		desc.HalfExtents = glm::vec3(side, desc.Volume / (8.0f * side * side), side);
		return desc;
	}

	FluidVolumeHandle PhysicsWorld::AddFluidVolume(FluidVolumeDesc desc)
	{
		const FluidVolumeHandle handle{ nextFluid++ };
		fluids.push_back({ handle, std::move(desc) });
		return handle;
	}

	bool PhysicsWorld::UpdateFluidVolume(FluidVolumeHandle volume, FluidVolumeDesc desc)
	{
		for (auto& fluid : fluids)
		{
			if (fluid.Handle == volume)
			{
				fluid.Desc = std::move(desc);
				return true;
			}
		}

		return false;
	}

	void PhysicsWorld::RemoveFluidVolume(FluidVolumeHandle volume)
	{
		std::erase_if(fluids,
			[volume](const FluidVolume& fluid)
			{
				return fluid.Handle == volume;
			});
	}

	bool PhysicsWorld::SetBuoyancy(BodyHandle body, const BuoyancyDesc& desc)
	{
		if (!IsBodyValid(body) || !(desc.Volume > 0.0f) || !std::isfinite(desc.Volume) || !Finite(desc.Center) ||
			!Finite(desc.HalfExtents) || desc.HalfExtents.x <= 0.0f || desc.HalfExtents.y <= 0.0f || desc.HalfExtents.z <= 0.0f ||
			desc.Samples < 1 || desc.Samples > 4 || !(desc.DragScale >= 0.0f) || !(desc.Density >= 0.0f) || !std::isfinite(desc.Density) ||
			!(desc.HeaveDamping >= 0.0f) || !std::isfinite(desc.HeaveDamping) || !std::isfinite(desc.TopMargin))
		{
			return false;
		}

		for (auto& entry : buoyant)
		{
			if (entry.Body == body)
			{
				entry.Desc = desc;
				return true;
			}
		}

		buoyant.push_back({ body, desc, {} });
		return true;
	}

	void PhysicsWorld::ClearBuoyancy(BodyHandle body)
	{
		std::erase_if(buoyant,
			[body](const BuoyantBody& entry)
			{
				return entry.Body == body;
			});
	}

	const BuoyancyState* PhysicsWorld::GetBuoyancyState(BodyHandle body) const
	{
		for (const auto& entry : buoyant)
		{
			if (entry.Body == body)
			{
				return &entry.State;
			}
		}

		return nullptr;
	}

	void PhysicsWorld::ApplyBuoyancy(float dt)
	{
		if (buoyant.empty() || fluids.empty() || !(dt > 0.0f))
		{
			for (auto& entry : buoyant)
			{
				entry.State = {};
			}

			return;
		}

		std::erase_if(buoyant,
			[this](const BuoyantBody& entry)
			{
				return !IsBodyValid(entry.Body);
			});

		for (auto& entry : buoyant)
		{
			entry.State = {};
			PhysicsPose pose;

			if (!GetBodyPose(entry.Body, pose))
			{
				continue;
			}

			const auto& desc = entry.Desc;
			const glm::mat3 rotation = glm::mat3_cast(pose.Rotation);
			const glm::vec3 centre = pose.Position + rotation * desc.Center;
			const FluidVolumeDesc* fluid = nullptr;

			for (std::size_t i = 0; i < fluids.size() && !fluid; ++i)
			{
				if (Inside(fluids[i].Desc, centre))
				{
					fluid = &fluids[i].Desc;
					entry.State.Volume = static_cast<std::int32_t>(i);
				}
			}

			if (!fluid)
			{
				continue;
			}

			const float gravity = glm::length(fluid->Gravity);
			const glm::vec3 up = gravity > 0.0f ? -fluid->Gravity / gravity : glm::vec3(0.0f, 1.0f, 0.0f);
			const std::uint32_t n = desc.Samples;
			const glm::vec3 cell = desc.HalfExtents / float(n);
			// Half the vertical extent of one cell as the body is turned: each point stands for a
			// slab that goes from dry to wet over that height.
			float halfHeight = 0.0f;

			for (int axis = 0; axis < 3; ++axis)
			{
				halfHeight += std::abs(glm::dot(rotation[axis], up)) * cell[axis];
			}

			halfHeight = std::max(halfHeight, 1.0e-4f);
			float mass = 0.0f;
			const bool hasMass = GetMass(entry.Body, mass) && mass > 0.0f;
			// A set density stands in for the body's own: lift scaled so mass / lift volume matches it.
			const float liftScale = desc.Density > 0.0f && hasMass ? mass / (desc.Density * desc.Volume) : 1.0f;
			const float pointVolume = desc.Volume / float(n * n * n);
			const float pointLift = fluid->Density * gravity * pointVolume * liftScale;
			float submerged = 0.0f;
			glm::vec3 flow(0.0f);
			glm::vec3 total(0.0f);

			for (std::uint32_t z = 0; z < n; ++z)
			{
				for (std::uint32_t y = 0; y < n; ++y)
				{
					for (std::uint32_t x = 0; x < n; ++x)
					{
						const glm::vec3 local =
							desc.Center - desc.HalfExtents + cell * glm::vec3(2.0f * x + 1.0f, 2.0f * y + 1.0f, 2.0f * z + 1.0f);
						const glm::vec3 point = pose.Position + rotation * local;

						if (!Inside(*fluid, point))
						{
							continue;
						}

						const auto sample = SampleFluid(*fluid, point.x, point.z, simulationTime);
						const float depth = sample.SurfaceHeight - glm::dot(point, up);
						const float wet = std::clamp(depth / (2.0f * halfHeight) + 0.5f, 0.0f, 1.0f);

						if (wet <= 0.0f)
						{
							continue;
						}

						const glm::vec3 force = up * (pointLift * wet);
						AddForceAtPosition(entry.Body, force, point, ForceMode::Force, true);
						total += force;
						submerged += wet;
						flow += sample.FlowVelocity * wet;
					}
				}
			}

			if (submerged <= 0.0f)
			{
				continue;
			}

			flow /= submerged;
			const float fraction = submerged / float(n * n * n);
			entry.State.Submerged = fraction;
			entry.State.Force = total;
			glm::vec3 velocity(0.0f);
			glm::vec3 spin(0.0f);
			// Displaced fluid mass over the body's (effective) mass.
			const float displaced = hasMass ? fluid->Density * desc.Volume * fraction * liftScale / mass : fraction;

			if (GetLinearVelocity(entry.Body, velocity))
			{
				const float keep = std::exp(-fluid->LinearDrag * desc.DragScale * displaced * dt);
				glm::vec3 change = (flow - velocity) * (1.0f - keep);

				if (desc.HeaveDamping > 0.0f)
				{
					const float heave = glm::dot(flow - velocity - change, up);
					change += up * (heave * (1.0f - std::exp(-desc.HeaveDamping * displaced * dt)));
				}

				AddForce(entry.Body, change, ForceMode::VelocityChange, true);
			}

			if (GetAngularVelocity(entry.Body, spin))
			{
				SetAngularVelocity(entry.Body, spin * std::exp(-fluid->AngularDrag * desc.DragScale * displaced * dt), true);
			}
		}
	}

	void PhysicsWorld::KeepFloatersUp()
	{
		for (auto& entry : buoyant)
		{
			const auto& desc = entry.Desc;
			PhysicsPose pose;

			if (!desc.NeverSink || entry.State.Volume < 0 || std::size_t(entry.State.Volume) >= fluids.size() ||
				!GetBodyPose(entry.Body, pose))
			{
				continue;
			}

			const FluidVolumeDesc& fluid = fluids[std::size_t(entry.State.Volume)].Desc;
			const glm::mat3 rotation = glm::mat3_cast(pose.Rotation);
			const glm::vec3 centre = pose.Position + rotation * desc.Center;

			if (!Inside(fluid, centre))
			{
				continue;
			}

			const float gravity = glm::length(fluid.Gravity);
			const glm::vec3 up = gravity > 0.0f ? -fluid.Gravity / gravity : glm::vec3(0.0f, 1.0f, 0.0f);
			float reach = 0.0f;

			for (int axis = 0; axis < 3; ++axis)
			{
				reach += std::abs(glm::dot(rotation[axis], up)) * desc.HalfExtents[axis];
			}

			const auto sample = SampleFluid(fluid, centre.x, centre.z, simulationTime);
			const float below = sample.SurfaceHeight + desc.TopMargin - (glm::dot(centre, up) + reach);

			if (below <= 0.0f)
			{
				continue;
			}

			pose.Position += up * below;
			SetBodyPose(entry.Body, pose, true);
			glm::vec3 velocity(0.0f);

			if (GetLinearVelocity(entry.Body, velocity))
			{
				const float sinking = glm::dot(sample.FlowVelocity - velocity, up);

				if (sinking > 0.0f)
				{
					SetLinearVelocity(entry.Body, velocity + up * sinking, true);
				}
			}
		}
	}

} // namespace Engine
