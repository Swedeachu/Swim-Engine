#include "Engine/Systems/Renderer/Skinning/SkinningReference.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace Swim::Render::Skinning
{
	namespace
	{
		using Float3 = std::array<float, 3>;

		bool Finite(const Float3& v)
		{
			return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
		}

		float Weight(std::span<const float> weights, std::uint32_t index)
		{
			return index < weights.size() ? weights[index] : 0.0f;
		}

		// The blended 3x4 of a vertex's influences (zero weights skipped), in slot order.
		SkinMatrix Blend(const GpuSkinVertex& skin, std::span<const SkinMatrix> palette)
		{
			SkinMatrix blended{};
			for (std::uint32_t slot = 0; slot < 4; ++slot)
			{
				const float w = skin.Weights[slot];
				if (w == 0.0f)
				{
					continue;
				}
				const std::uint16_t joint = Joint(skin, slot);
				if (joint >= palette.size())
				{
					throw std::out_of_range("skinned vertex joint is outside the palette");
				}
				for (std::size_t k = 0; k < 12; ++k)
				{
					blended[k] += w * palette[joint][k];
				}
			}
			return blended;
		}

		Float3 Point(const SkinMatrix& m, const Float3& p)
		{
			return { m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3], m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
				m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11] };
		}

		Float3 Direction(const SkinMatrix& m, const Float3& v)
		{
			return { m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[4] * v[0] + m[5] * v[1] + m[6] * v[2],
				m[8] * v[0] + m[9] * v[1] + m[10] * v[2] };
		}

		Float3 Cross(const Float3& a, const Float3& b)
		{
			return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
		}

		float Dot(const Float3& a, const Float3& b)
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		}

		Float3 Normalized(const Float3& v, const Float3& fallback)
		{
			const float lengthSquared = Dot(v, v);
			if (!(lengthSquared > 1e-30f))
			{
				return fallback;
			}
			const float inverse = 1.0f / std::sqrt(lengthSquared);
			return { v[0] * inverse, v[1] * inverse, v[2] * inverse };
		}

		void Morph(const StandardVertex& source, std::span<const GpuMorphDelta> deltas, std::span<const float> weights, Float3& position,
			Float3& normal, Float3& tangent)
		{
			position = source.Position;
			normal = source.Normal;
			tangent = { source.Tangent[0], source.Tangent[1], source.Tangent[2] };
			for (const GpuMorphDelta& delta : deltas)
			{
				const float w = Weight(weights, delta.Target);
				for (int c = 0; c < 3; ++c)
				{
					position[c] += w * delta.Position[c];
					normal[c] += w * delta.Normal[c];
					tangent[c] += w * delta.Tangent[c];
				}
			}
		}
	} // namespace

	SkinnedSource BuildSource(std::span<const StandardVertex> vertices, std::span<const SkinInfluence> influences,
		std::span<const SkinnedMorphTarget> targets, std::uint32_t jointCount)
	{
		if (vertices.empty() || influences.size() != vertices.size())
		{
			throw std::invalid_argument("skinned mesh needs one influence per vertex");
		}
		if (jointCount == 0 || jointCount > 65536)
		{
			throw std::invalid_argument("skinned mesh needs 1 .. 65536 joints");
		}
		for (const SkinnedMorphTarget& target : targets)
		{
			for (const auto* stream : { &target.Positions, &target.Normals, &target.Tangents })
			{
				if (!stream->empty() && stream->size() != vertices.size())
				{
					throw std::invalid_argument("morph target deltas must be empty or one per vertex");
				}
			}
		}
		SkinnedSource source;
		source.SkinVertices.resize(vertices.size());
		SkinnedBoundsData& bounds = source.Bounds;
		bounds.JointCount = jointCount;
		bounds.MorphTargetCount = static_cast<std::uint32_t>(targets.size());
		bounds.JointMin.assign(
			jointCount, { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() });
		bounds.JointMax.assign(
			jointCount, { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() });
		bounds.MorphReach.assign(std::size_t(jointCount) * targets.size(), 0.0f);
		const Float3 zero{ 0, 0, 0 };
		for (std::size_t v = 0; v < vertices.size(); ++v)
		{
			if (!Finite(vertices[v].Position))
			{
				throw std::invalid_argument("skinned mesh vertex position is not finite");
			}
			const SkinInfluence& influence = influences[v];
			GpuSkinVertex& skin = source.SkinVertices[v];
			for (std::uint32_t slot = 0; slot < 4; ++slot)
			{
				const float w = influence.Weights[slot];
				if (!std::isfinite(w) || w < 0.0f)
				{
					throw std::invalid_argument("skin weights must be finite and non-negative");
				}
				if (w > 0.0f && influence.Joints[slot] >= jointCount)
				{
					throw std::invalid_argument(
						"skin influence joint " + std::to_string(influence.Joints[slot]) + " is outside the skeleton");
				}
				skin.Weights[slot] = w;
				skin.Joints[slot / 2] |= std::uint32_t(w > 0.0f ? influence.Joints[slot] : 0u) << ((slot % 2) * 16);
			}
			skin.MorphFirst = static_cast<std::uint32_t>(source.MorphDeltas.size());
			for (std::uint32_t t = 0; t < targets.size(); ++t)
			{
				GpuMorphDelta delta;
				delta.Target = t;
				const Float3 p = targets[t].Positions.empty() ? zero : targets[t].Positions[v];
				const Float3 n = targets[t].Normals.empty() ? zero : targets[t].Normals[v];
				const Float3 tan = targets[t].Tangents.empty() ? zero : targets[t].Tangents[v];
				if (!Finite(p) || !Finite(n) || !Finite(tan))
				{
					throw std::invalid_argument("morph target delta is not finite");
				}
				if (p == zero && n == zero && tan == zero)
				{
					continue;
				}
				std::copy(p.begin(), p.end(), delta.Position);
				std::copy(n.begin(), n.end(), delta.Normal);
				std::copy(tan.begin(), tan.end(), delta.Tangent);
				source.MorphDeltas.push_back(delta);
				const float reach = std::sqrt(Dot(p, p));
				for (std::uint32_t slot = 0; slot < 4; ++slot)
				{
					if (skin.Weights[slot] > 0.0f)
					{
						float& entry = bounds.MorphReach[std::size_t(t) * jointCount + Joint(skin, slot)];
						entry = std::max(entry, reach);
					}
				}
			}
			skin.MorphCount = static_cast<std::uint32_t>(source.MorphDeltas.size()) - skin.MorphFirst;
			for (std::uint32_t slot = 0; slot < 4; ++slot)
			{
				if (skin.Weights[slot] > 0.0f)
				{
					const std::uint16_t joint = Joint(skin, slot);
					for (int axis = 0; axis < 3; ++axis)
					{
						bounds.JointMin[joint][axis] = std::min(bounds.JointMin[joint][axis], vertices[v].Position[axis]);
						bounds.JointMax[joint][axis] = std::max(bounds.JointMax[joint][axis], vertices[v].Position[axis]);
					}
				}
			}
		}
		return source;
	}

	StandardVertex SkinVertex(const StandardVertex& source, const GpuSkinVertex& skin, std::span<const GpuMorphDelta> deltas,
		std::span<const SkinMatrix> palette, std::span<const float> morphWeights)
	{
		Float3 position, normal, tangent;
		Morph(source, deltas, morphWeights, position, normal, tangent);
		const SkinMatrix m = Blend(skin, palette);
		StandardVertex result = source;
		result.Position = Point(m, position);
		const Float3 r0{ m[0], m[1], m[2] }, r1{ m[4], m[5], m[6] }, r2{ m[8], m[9], m[10] };
		const Float3 cofactor{ Dot(Cross(r1, r2), normal), Dot(Cross(r2, r0), normal), Dot(Cross(r0, r1), normal) };
		result.Normal = Normalized(cofactor, source.Normal);
		const Float3 t = Normalized(Direction(m, tangent), { source.Tangent[0], source.Tangent[1], source.Tangent[2] });
		result.Tangent = { t[0], t[1], t[2], source.Tangent[3] };
		return result;
	}

	std::array<float, 3> SkinPosition(const StandardVertex& source, const GpuSkinVertex& skin, std::span<const GpuMorphDelta> deltas,
		std::span<const SkinMatrix> palette, std::span<const float> morphWeights)
	{
		Float3 position, normal, tangent;
		Morph(source, deltas, morphWeights, position, normal, tangent);
		return Point(Blend(skin, palette), position);
	}

	RenderBounds ComputeBounds(const SkinnedBoundsData& data, std::span<const SkinMatrix> palette, std::span<const float> morphWeights)
	{
		if (palette.size() < data.JointCount)
		{
			throw std::invalid_argument("skinned bounds need one matrix per joint");
		}
		Float3 min{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
		Float3 max{ -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
		for (std::uint32_t joint = 0; joint < data.JointCount; ++joint)
		{
			if (!(data.JointMin[joint][0] <= data.JointMax[joint][0]))
			{
				continue; // Moves nothing.
			}
			float reach = 0.0f;
			for (std::uint32_t t = 0; t < data.MorphTargetCount; ++t)
			{
				reach += std::abs(Weight(morphWeights, t)) * data.MorphReach[std::size_t(t) * data.JointCount + joint];
			}
			for (int corner = 0; corner < 8; ++corner)
			{
				Float3 p{};
				for (int axis = 0; axis < 3; ++axis)
				{
					p[axis] = (corner >> axis) & 1 ? data.JointMax[joint][axis] + reach : data.JointMin[joint][axis] - reach;
				}
				const Float3 q = Point(palette[joint], p);
				for (int axis = 0; axis < 3; ++axis)
				{
					min[axis] = std::min(min[axis], q[axis]);
					max[axis] = std::max(max[axis], q[axis]);
				}
			}
		}
		return RenderBounds::FromMinMax(min, max);
	}
} // namespace Swim::Render::Skinning
