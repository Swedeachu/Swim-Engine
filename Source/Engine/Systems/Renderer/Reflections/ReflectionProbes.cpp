#include "Engine/Systems/Renderer/Reflections/ReflectionProbes.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::Render::ReflectionProbes
{

	namespace
	{

		float Dot(const Float3& a, const Float3& b)
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		}

		Float3 Sub(const Float3& a, const Float3& b)
		{
			return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
		}

		Float3 Normalize(const Float3& v)
		{
			const float length = std::sqrt(Dot(v, v));
			return length > 0.0f ? Float3{ v[0] / length, v[1] / length, v[2] / length } : Float3{ 0, 0, 0 };
		}

		float Distance(const Float3& a, const Float3& b)
		{
			const auto d = Sub(a, b);
			return std::sqrt(Dot(d, d));
		}

	} // namespace

	FaceBasis CubeFace(std::uint32_t face)
	{
		switch (face)
		{
		case 0: return { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } };
		case 1: return { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } };
		case 2: return { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } };
		case 3: return { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } };
		case 4: return { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } };
		case 5: return { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } };
		default: throw std::invalid_argument("ReflectionProbes::CubeFace: face must be 0..5");
		}
	}

	bool FaceSees(std::uint32_t face, const Float3& offset, float radius)
	{
		const auto basis = CubeFace(face);
		const float forward = Dot(offset, basis.Forward);
		// The frustum's side planes are |side| = forward; a sphere touches it when it is within
		// radius * sqrt(2) of them (the planes are at 45 degrees).
		const float slack = radius * 1.41421356f;
		return forward > -radius && std::abs(Dot(offset, basis.Right)) <= forward + slack &&
			   std::abs(Dot(offset, basis.Up)) <= forward + slack;
	}

	std::array<float, 16> CubeFaceView(std::uint32_t face, const Float3& position)
	{
		const auto basis = CubeFace(face);
		// Right-handed rows: x = -Right (mirrored), y = Up, z = -Forward.
		const Float3 x{ -basis.Right[0], -basis.Right[1], -basis.Right[2] };
		const Float3 z{ -basis.Forward[0], -basis.Forward[1], -basis.Forward[2] };
		const auto& y = basis.Up;
		return { x[0], x[1], x[2], -Dot(x, position), y[0], y[1], y[2], -Dot(y, position), z[0], z[1], z[2], -Dot(z, position), 0, 0, 0,
			1 };
	}

	std::array<float, 16> CubeFaceProjection(float nearPlane)
	{
		return PerspectiveReverseZRowMajor(3.14159265358979f * 0.5f, 1.0f, nearPlane);
	}

	float CaptureDistance(float depth, float nearPlane, float ndcX, float ndcY)
	{
		if (!(depth > 0.0f))
		{
			return DistanceSky;
		}

		return std::min(nearPlane / depth * std::sqrt(1.0f + ndcX * ndcX + ndcY * ndcY), DistanceSky);
	}

	Selection Select(std::span<const GpuReflectionProbeRecord> records, const Float3& position, float pixelObjectId)
	{
		Selection selection;
		float firstWeight = 0.0f;
		float secondWeight = 0.0f;
		float firstShare = 0.0f;
		float secondShare = 0.0f;

		for (std::size_t i = 0; i < records.size(); ++i)
		{
			const auto& record = records[i];
			const float owner = record.Params[3];

			if (owner != 0.0f)
			{
				if (owner == pixelObjectId)
				{
					// The pixel's own object probe, exclusively.
					return { static_cast<std::int32_t>(i), -1, 1.0f, 0.0f, 1.0f };
				}

				continue;
			}

			const Float3 centre{ record.PositionRadius[0], record.PositionRadius[1], record.PositionRadius[2] };
			const float radius = record.PositionRadius[3];
			const float d = Distance(position, centre);
			const float weight = std::clamp((radius - d) / std::max(record.Params[0], 1.0e-3f), 0.0f, 1.0f);

			if (!(weight > 0.0f))
			{
				continue;
			}

			const float share = weight / (d / std::max(radius, 1.0e-3f) + 0.05f);

			if (weight > firstWeight || (weight == firstWeight && share > firstShare))
			{
				selection.Second = selection.First;
				secondWeight = firstWeight;
				secondShare = firstShare;
				selection.First = static_cast<std::int32_t>(i);
				firstWeight = weight;
				firstShare = share;
			}
			else if (weight > secondWeight)
			{
				selection.Second = static_cast<std::int32_t>(i);
				secondWeight = weight;
				secondShare = share;
			}
		}

		if (selection.First < 0)
		{
			return selection;
		}

		const float total = firstShare + (selection.Second >= 0 ? secondShare : 0.0f);
		selection.FirstShare = total > 0.0f ? firstShare / total : 1.0f;
		selection.SecondShare = selection.Second >= 0 && total > 0.0f ? secondShare / total : 0.0f;
		selection.Coverage = firstWeight;
		return selection;
	}

	Float3 ParallaxDirection(const Float3& position, const Float3& direction, const Float3& probe,
		const std::function<float(const Float3&)>& distance, float texelAngle)
	{
		const Float3 offset = Sub(position, probe);
		const auto at = [&](float t)
		{
			return Float3{ offset[0] + direction[0] * t, offset[1] + direction[1] * t, offset[2] + direction[2] * t };
		};
		// How far beyond the captured surface in its own direction (>= 0: on or beyond).
		const auto excess = [&](float t, float& length)
		{
			const Float3 x = at(t);
			length = std::sqrt(Dot(x, x));
			return length > 0.0f ? length - distance(Normalize(x)) : -1.0f;
		};
		float t = 0.0f;
		bool wasBeyond = false;
		bool hasHidden = false;
		Float3 hidden = direction;

		for (std::uint32_t i = 0; i < ParallaxSteps; ++i)
		{
			const Float3 x = at(t);
			const float length = std::sqrt(Dot(x, x));
			const Float3 radial = length > 1.0e-5f ? Float3{ x[0] / length, x[1] / length, x[2] / length } : direction;
			const float along = Dot(direction, radial);
			const Float3 acrossVector{ direction[0] - radial[0] * along, direction[1] - radial[1] * along, direction[2] - radial[2] * along };
			const float across = std::sqrt(Dot(acrossVector, acrossVector));
			const float dt =
				std::clamp(std::max(ParallaxTexels * texelAngle, ParallaxMinAngle) * length / std::max(across, 0.02f), ParallaxMinStep, ParallaxReach * 0.125f);
			const float next = t + dt;

			if (next > ParallaxReach)
			{
				break;
			}

			float nextLength = 0.0f;
			const bool beyond = excess(next, nextLength) >= 0.0f;

			if (!beyond || wasBeyond)
			{
				// Only a transition from in front to beyond can be a crossing.
				wasBeyond = beyond;
				t = next;
				continue;
			}

			float lo = t;
			float hi = next;

			for (std::uint32_t k = 0; k < ParallaxRefineSteps; ++k)
			{
				const float mid = 0.5f * (lo + hi);
				float midLength = 0.0f;
				(excess(mid, midLength) >= 0.0f ? hi : lo) = mid;
			}

			float hitLength = 0.0f;
			const float over = excess(hi, hitLength);

			if (over <= ParallaxThickness(hitLength, hi - lo))
			{
				const auto hit = Normalize(at(hi));
				return Dot(hit, hit) > 0.0f ? hit : direction;
			}

			// The ray went behind an occluder (the captured distance jumps at its silhouette):
			// not a crossing; keep marching until it comes out and crosses something real.
			// Close behind it, the occluder is kept in case nothing real follows.
			if (!hasHidden && over <= ParallaxHidden(hitLength))
			{
				hidden = Normalize(at(hi));
				hasHidden = Dot(hidden, hidden) > 0.0f;
			}

			wasBeyond = true;
			t = next;
		}

		// Nothing crossed within reach: a side of the occluder the probe does not see, or else
		// the sky, along the ray.
		return hasHidden ? hidden : direction;
	}

	void Scheduler::Reset()
	{
		slots.clear();
	}

	std::uint32_t Scheduler::GetUsedSlots() const
	{
		return static_cast<std::uint32_t>(std::count_if(slots.begin(), slots.end(),
			[](const Slot& slot)
			{
				return slot.Used;
			}));
	}

	Plan Scheduler::Update(std::span<const ReflectionProbeDesc> probes, const Float3& camera, std::uint64_t frame, double time,
		const ReflectionProbeSettings& settings, std::span<const ReflectionProbeMover> movers)
	{
		Plan plan;
		const auto slotCount = std::clamp(settings.MaxProbes, 1u, MaxReflectionProbes);

		if (slots.size() != slotCount)
		{
			slots.assign(slotCount, {});
		}

		if (!settings.Enabled)
		{
			for (auto& slot : slots)
			{
				slot = {};
			}

			return plan;
		}

		// Rank the probes: priority / (1 + distance to the camera); the best MaxProbes keep slots.
		std::vector<std::pair<float, std::uint32_t>> ranked;

		for (std::uint32_t i = 0; i < probes.size(); ++i)
		{
			const float rank = std::max(probes[i].Priority, 1.0e-3f) / (1.0f + Distance(probes[i].Position, camera));
			ranked.push_back({ rank, i });
		}

		std::stable_sort(ranked.begin(), ranked.end(),
			[](const auto& a, const auto& b)
			{
				return a.first > b.first;
			});

		if (ranked.size() > slotCount)
		{
			ranked.resize(slotCount);
		}

		// Keep the slots of probes still present; release the others; then place new probes.
		std::vector<std::int32_t> slotOf(probes.size(), -1);
		std::vector<bool> kept(slots.size(), false);

		for (const auto& [rank, index] : ranked)
		{
			for (std::uint32_t s = 0; s < slots.size(); ++s)
			{
				if (slots[s].Used && slots[s].Key == probes[index].Key)
				{
					slotOf[index] = static_cast<std::int32_t>(s);
					kept[s] = true;
					break;
				}
			}
		}

		for (std::uint32_t s = 0; s < slots.size(); ++s)
		{
			if (!kept[s])
			{
				slots[s] = {};
			}
		}

		for (const auto& [rank, index] : ranked)
		{
			if (slotOf[index] >= 0)
			{
				continue;
			}

			for (std::uint32_t s = 0; s < slots.size(); ++s)
			{
				if (!slots[s].Used)
				{
					slots[s] = {};
					slots[s].Used = true;
					slots[s].Key = probes[index].Key;
					slotOf[index] = static_cast<std::int32_t>(s);
					break;
				}
			}
		}

		// Face urgencies.
		struct Candidate
		{
			float Urgency;
			FaceCapture Capture;
			bool SeesMover;
			bool Changed;
		};
		std::vector<Candidate> candidates;
		std::vector<std::size_t> chosen;
		// Changed faces wait in proportion to how large their probe is on screen (its rank
		// against the best one, 0.5 .. 1): the balls up close refresh every frame, the far
		// ones at no less than about half that rate.
		const float bestRank = ranked.empty() ? 1.0f : std::max(ranked.front().first, 1.0e-6f);

		for (const auto& [rank, index] : ranked)
		{
			const auto s = static_cast<std::uint32_t>(slotOf[index]);
			auto& slot = slots[s];
			slot.LastSeen = frame;
			const auto& probe = probes[index];
			const float screenWeight = 0.5f + 0.5f * std::min(rank / bestRank, 1.0f);
			const bool incomplete = std::any_of(slot.FaceFrame.begin(), slot.FaceFrame.end(),
				[](std::uint64_t f)
				{
					return f == 0;
				});

			for (std::uint32_t face = 0; face < 6; ++face)
			{
				float urgency = 0.0f;
				// Faces whose content changed (never captured, or a mover in view or just left)
				// are ranked by how long they waited only, not by the probe's rank: they go round
				// robin, so with FacesPerFrame at least half of them, every one is refreshed at
				// least every second frame (a far probe is never starved by a near one).
				bool changed = false;
				const auto seesMover = [&]
				{
					for (const auto& mover : movers)
					{
						const Float3 offset = Sub(mover.Center, probe.Position);
						const float distance = std::sqrt(Dot(offset, offset));
						// A mover at the probe itself is its owner (an object probe does not see it).
						if (distance > mover.Radius && distance - mover.Radius < settings.MoverRange &&
							FaceSees(face, offset, mover.Radius))
						{
							return true;
						}
					}

					return false;
				};

				const bool moving = probe.Dynamic && seesMover();

				if (slot.FaceFrame[face] == 0)
				{
					urgency = 1.0e6f;
					changed = true;
				}
				else if (probe.Dynamic && slot.FaceFrame[face] != frame && (slot.FaceSawMover[face] || moving))
				{
					// A mover in view, or one that was at the last capture (it left: one more
					// capture removes it). The longer a face waited, the sooner it goes.
					urgency = 1.0e4f + 100.0f * float(frame - slot.FaceFrame[face]) * screenWeight;
					changed = true;
				}
				else if (Distance(slot.FacePosition[face], probe.Position) > settings.MoveThreshold)
				{
					// The probe moved (an orbiting chrome ball's object probe): changed content too.
					urgency = 1.0e4f + 100.0f * float(frame - slot.FaceFrame[face]) * screenWeight;
					changed = true;
				}
				else if (probe.Dynamic && frame - slot.FaceFrame[face] >= settings.IdleRefreshFrames)
				{
					urgency = float(frame - slot.FaceFrame[face]);
				}
				else if (incomplete)
				{
					urgency = 0.0f;
				}

				if (urgency > 0.0f)
				{
					candidates.push_back({ changed ? urgency : urgency * rank, { s, face, index }, moving, changed });
				}
			}
		}

		std::stable_sort(candidates.begin(), candidates.end(),
			[](const Candidate& a, const Candidate& b)
			{
				return a.Urgency > b.Urgency;
			});
		const auto budget = std::min<std::size_t>(std::min(settings.FacesPerFrame, MaxFacesPerFrame), candidates.size());
		// An eighth of the budget (at least one face from four a frame) is kept for faces whose content did
		// not change (the idle refresh: animated materials, lighting, and faces whose first
		// capture came before the scene was there), so a busy scene - movers in view of many
		// probes - can never starve them: the probes' still faces once kept their first, empty
		// capture for good, which showed as plain sky and ground in the lower half of chrome
		// spheres. Faces never captured go before everything.
		std::size_t idleReserve = std::min<std::size_t>(std::max<std::size_t>(budget / 8u, budget >= 4u ? 1u : 0u),
			static_cast<std::size_t>(std::count_if(candidates.begin(), candidates.end(),
				[](const Candidate& c)
				{
					return !c.Changed;
				})));
		std::size_t changedLeft = budget - idleReserve;
		const std::size_t changedBudget = changedLeft;
		chosen.clear();
		std::vector<bool> taken(candidates.size(), false);

		for (std::size_t i = 0; i < candidates.size() && chosen.size() < budget; ++i)
		{
			if (taken[i])
			{
				continue;
			}

			const auto& c = candidates[i];
			const bool fresh = slots[c.Capture.Slot].FaceFrame[c.Capture.Face] == 0;

			if (c.Changed && !fresh && changedLeft == 0)
			{
				continue;
			}

			if (c.Changed && !fresh)
			{
				// A probe's changed faces are captured together, in one frame. Faces captured
				// on different frames showed a moving object at two times: across a cube seam
				// the orbiting block appeared twice, or cut in sections, in the chrome spheres.
				// A probe whose changed faces do not fit what is left waits for a frame they do
				// (the round robin brings it to the front). Only a group larger than the whole
				// budget for changed faces is split (oldest faces first): the budget stays a bound.
				std::vector<std::size_t> group;

				for (std::size_t j = i; j < candidates.size(); ++j)
				{
					const auto& other = candidates[j];

					if (!taken[j] && other.Changed && other.Capture.Slot == c.Capture.Slot &&
						slots[other.Capture.Slot].FaceFrame[other.Capture.Face] != 0)
					{
						group.push_back(j);
						taken[j] = true; // Chosen below, or waiting as a whole.
					}
				}

				if (group.size() > changedLeft && group.size() <= changedBudget && !chosen.empty())
				{
					continue;
				}

				for (const auto j : group)
				{
					if (changedLeft == 0)
					{
						break;
					}

					chosen.push_back(j);
					--changedLeft;
				}

				continue;
			}

			if (fresh || c.Changed)
			{
				changedLeft -= changedLeft > 0 ? 1u : 0u;
				idleReserve -= changedLeft == 0 && fresh && idleReserve > 0 ? 1u : 0u;
			}
			else if (idleReserve > 0)
			{
				--idleReserve;
			}
			else if (changedLeft > 0)
			{
				--changedLeft; // An idle face past its reserve takes a changed face's turn.
			}
			else
			{
				continue;
			}

			chosen.push_back(i);
		}

		for (const auto i : chosen)
		{
			const auto& capture = candidates[i].Capture;
			plan.Captures.push_back(capture);
			auto& slot = slots[capture.Slot];
			slot.FaceFrame[capture.Face] = frame;
			slot.FaceTime[capture.Face] = time;
			slot.FacePosition[capture.Face] = probes[capture.Probe].Position;
			slot.FaceSawMover[capture.Face] = candidates[i].SeesMover;

			if (std::find(plan.Filter.begin(), plan.Filter.end(), capture.Slot) == plan.Filter.end())
			{
				plan.Filter.push_back(capture.Slot);
			}
		}

		std::sort(plan.Filter.begin(), plan.Filter.end());

		for (const auto& [rank, index] : ranked)
		{
			const auto s = static_cast<std::uint32_t>(slotOf[index]);
			const auto& slot = slots[s];

			if (std::any_of(slot.FaceFrame.begin(), slot.FaceFrame.end(),
					[](std::uint64_t f)
					{
						return f == 0;
					}))
			{
				continue; // Not every face captured yet.
			}

			const double oldest = *std::min_element(slot.FaceTime.begin(), slot.FaceTime.end());
			plan.Active.push_back({ s, index, static_cast<float>(std::max(time - oldest, 0.0)) });
		}

		std::sort(plan.Active.begin(), plan.Active.end(),
			[](const ActiveProbe& a, const ActiveProbe& b)
			{
				return a.Slot < b.Slot;
			});
		return plan;
	}

} // namespace Swim::Render::ReflectionProbes
