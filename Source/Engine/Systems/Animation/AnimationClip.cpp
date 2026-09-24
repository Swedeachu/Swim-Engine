#include "Engine/Systems/Animation/AnimationClip.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace Swim::Animation
{
	namespace
	{
		void ValidateTrack(const Assets::AnimationTrack& track)
		{
			const std::uint32_t expected = track.Path == Assets::AnimationPath::Rotation ? 4u
				: track.Path == Assets::AnimationPath::MorphWeights						 ? track.Components
																						 : 3u;
			if (track.Components == 0 || track.Components != expected || track.Times.empty())
			{
				throw std::invalid_argument("animation track has an invalid component count or no keys");
			}
			const std::size_t perKey =
				std::size_t(track.Components) * (track.Interpolation == Assets::AnimationInterpolation::CubicSpline ? 3u : 1u);
			if (track.Values.size() != track.Times.size() * perKey)
			{
				throw std::invalid_argument("animation track values do not match its keys");
			}
			for (std::size_t key = 0; key < track.Times.size(); ++key)
			{
				if (!std::isfinite(track.Times[key]) || (key > 0 && !(track.Times[key] > track.Times[key - 1])))
				{
					throw std::invalid_argument("animation track times must be finite and strictly increasing");
				}
			}
		}

		float Weight(const BoneMask* mask, std::uint32_t joint)
		{
			return mask ? mask->Get(joint) : 1.0f;
		}
	} // namespace

	std::uint32_t MorphLayout::GetWeightCount() const
	{
		std::uint32_t count = 0;
		for (const MorphChannel& channel : Channels)
		{
			count += channel.Count;
		}
		return count;
	}

	std::uint32_t MorphLayout::GetOffset(std::uint32_t channel) const
	{
		std::uint32_t offset = 0;
		for (std::uint32_t index = 0; index < channel; ++index)
		{
			offset += Channels[index].Count;
		}
		return offset;
	}

	std::uint32_t MorphLayout::FindChannel(std::string_view target) const
	{
		for (std::uint32_t index = 0; index < Channels.size(); ++index)
		{
			if (Channels[index].Target == target)
			{
				return index;
			}
		}
		return InvalidJoint;
	}

	std::vector<float> MorphLayout::GetDefaultWeights() const
	{
		std::vector<float> weights;
		weights.reserve(GetWeightCount());
		for (const MorphChannel& channel : Channels)
		{
			for (std::uint32_t index = 0; index < channel.Count; ++index)
			{
				weights.push_back(index < channel.DefaultWeights.size() ? channel.DefaultWeights[index] : 0.0f);
			}
		}
		return weights;
	}

	AnimationClip::AnimationClip(Assets::AnimationClipAsset source) : asset(std::move(source))
	{
		if (!std::isfinite(asset.Duration) || asset.Duration < 0.0f)
		{
			throw std::invalid_argument("animation clip duration must be finite and non-negative");
		}
		for (const Assets::AnimationTrack& track : asset.Tracks)
		{
			ValidateTrack(track);
			asset.Duration = std::max(asset.Duration, track.Times.back());
		}
		std::stable_sort(asset.Events.begin(), asset.Events.end(),
			[](const auto& a, const auto& b)
			{
				return a.Time < b.Time;
			});
	}

	ClipBinding BindClip(const AnimationClip& clip, const Skeleton& skeleton, const MorphLayout& morphs)
	{
		ClipBinding binding;
		binding.Targets.resize(clip.GetTracks().size());
		for (std::size_t index = 0; index < clip.GetTracks().size(); ++index)
		{
			const Assets::AnimationTrack& track = clip.GetTracks()[index];
			ClipBinding::Target& target = binding.Targets[index];
			if (track.Path == Assets::AnimationPath::MorphWeights)
			{
				const std::uint32_t channel = morphs.FindChannel(track.Target);
				if (channel != InvalidJoint)
				{
					target.MorphOffset = morphs.GetOffset(channel);
					target.MorphCount = std::min(track.Components, morphs.Channels[channel].Count);
					++binding.BoundTracks;
				}
				continue;
			}
			target.Joint = skeleton.FindJoint(track.Target);
			if (target.Joint != InvalidJoint)
			{
				++binding.BoundTracks;
			}
		}
		return binding;
	}

	void SampleTrack(const Assets::AnimationTrack& track, float time, std::span<float> out)
	{
		const std::uint32_t n = track.Components;
		const bool cubic = track.Interpolation == Assets::AnimationInterpolation::CubicSpline;
		const std::size_t stride = std::size_t(n) * (cubic ? 3u : 1u);
		const std::size_t valueOffset = cubic ? n : 0u; // Skip the in-tangent.
		const auto value = [&](std::size_t key, std::uint32_t c)
		{
			return track.Values[key * stride + valueOffset + c];
		};
		const std::size_t keys = track.Times.size();

		std::size_t k0 = 0;
		float t = 0.0f;
		if (keys == 1 || time <= track.Times.front())
		{
			k0 = 0;
		}
		else if (time >= track.Times.back())
		{
			k0 = keys - 1;
		}
		else
		{
			const auto upper = std::upper_bound(track.Times.begin(), track.Times.end(), time);
			k0 = static_cast<std::size_t>(upper - track.Times.begin()) - 1;
			t = (time - track.Times[k0]) / (track.Times[k0 + 1] - track.Times[k0]);
		}
		const bool between = k0 + 1 < keys && t > 0.0f;

		if (!between || track.Interpolation == Assets::AnimationInterpolation::Step)
		{
			for (std::uint32_t c = 0; c < n; ++c)
			{
				out[c] = value(k0, c);
			}
		}
		else if (cubic)
		{
			// glTF cubic Hermite: tangents are scaled by the key interval.
			const float dt = track.Times[k0 + 1] - track.Times[k0];
			const float t2 = t * t, t3 = t2 * t;
			const float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
			for (std::uint32_t c = 0; c < n; ++c)
			{
				const float outTangent = track.Values[k0 * stride + 2 * n + c];
				const float inTangent = track.Values[(k0 + 1) * stride + c];
				out[c] = h00 * value(k0, c) + h10 * dt * outTangent + h01 * value(k0 + 1, c) + h11 * dt * inTangent;
			}
		}
		else if (track.Path == Assets::AnimationPath::Rotation)
		{
			const Quat q = Slerp({ value(k0, 0), value(k0, 1), value(k0, 2), value(k0, 3) },
				{ value(k0 + 1, 0), value(k0 + 1, 1), value(k0 + 1, 2), value(k0 + 1, 3) }, t);
			std::copy(q.begin(), q.end(), out.begin());
			return;
		}
		else
		{
			for (std::uint32_t c = 0; c < n; ++c)
			{
				out[c] = value(k0, c) + (value(k0 + 1, c) - value(k0, c)) * t;
			}
		}
		if (track.Path == Assets::AnimationPath::Rotation)
		{
			const Quat q = Normalize({ out[0], out[1], out[2], out[3] });
			std::copy(q.begin(), q.end(), out.begin());
		}
	}

	void SampleClip(const AnimationClip& clip, const ClipBinding& binding, float time, AnimationPose& pose)
	{
		std::array<float, 4> scratch{};
		std::vector<float> weights;
		const auto& tracks = clip.GetTracks();
		for (std::size_t index = 0; index < tracks.size() && index < binding.Targets.size(); ++index)
		{
			const Assets::AnimationTrack& track = tracks[index];
			const ClipBinding::Target& target = binding.Targets[index];
			if (track.Path == Assets::AnimationPath::MorphWeights)
			{
				if (target.MorphOffset == InvalidJoint)
				{
					continue;
				}
				weights.resize(track.Components);
				SampleTrack(track, time, weights);
				for (std::uint32_t c = 0; c < target.MorphCount && target.MorphOffset + c < pose.MorphWeights.size(); ++c)
				{
					pose.MorphWeights[target.MorphOffset + c] = weights[c];
				}
				continue;
			}
			if (target.Joint == InvalidJoint || target.Joint >= pose.Joints.size())
			{
				continue;
			}
			SampleTrack(track, time, std::span<float>(scratch.data(), track.Components));
			JointPose& joint = pose.Joints[target.Joint];
			switch (track.Path)
			{
			case Assets::AnimationPath::Translation:
				joint.Translation = { scratch[0], scratch[1], scratch[2] };
				break;
			case Assets::AnimationPath::Rotation:
				joint.Rotation = { scratch[0], scratch[1], scratch[2], scratch[3] };
				break;
			case Assets::AnimationPath::Scale:
				joint.Scale = { scratch[0], scratch[1], scratch[2] };
				break;
			default:
				break;
			}
		}
	}

	AnimationPose MakeRestPose(const Skeleton& skeleton, const MorphLayout& morphs)
	{
		AnimationPose pose;
		pose.Joints = skeleton.GetRestPose();
		pose.MorphWeights = morphs.GetDefaultWeights();
		return pose;
	}

	void BlendPose(AnimationPose& a, const AnimationPose& b, float weight, const BoneMask* mask)
	{
		const std::size_t joints = std::min(a.Joints.size(), b.Joints.size());
		for (std::uint32_t joint = 0; joint < joints; ++joint)
		{
			const float w = weight * Weight(mask, joint);
			if (w <= 0.0f)
			{
				continue;
			}
			JointPose& target = a.Joints[joint];
			const JointPose& source = b.Joints[joint];
			target.Translation = Lerp(target.Translation, source.Translation, w);
			target.Rotation = Nlerp(target.Rotation, source.Rotation, w);
			target.Scale = Lerp(target.Scale, source.Scale, w);
		}
		const std::size_t morphs = std::min(a.MorphWeights.size(), b.MorphWeights.size());
		for (std::size_t index = 0; index < morphs; ++index)
		{
			a.MorphWeights[index] += (b.MorphWeights[index] - a.MorphWeights[index]) * weight;
		}
	}

	AnimationPose MakeAdditivePose(const AnimationPose& pose, const AnimationPose& reference)
	{
		AnimationPose delta;
		const std::size_t joints = std::min(pose.Joints.size(), reference.Joints.size());
		delta.Joints.resize(joints);
		for (std::size_t joint = 0; joint < joints; ++joint)
		{
			const JointPose& p = pose.Joints[joint];
			const JointPose& r = reference.Joints[joint];
			JointPose& d = delta.Joints[joint];
			d.Translation = Sub(p.Translation, r.Translation);
			d.Rotation = Normalize(Multiply(Inverse(r.Rotation), p.Rotation));
			for (int axis = 0; axis < 3; ++axis)
			{
				d.Scale[axis] = std::abs(r.Scale[axis]) > 1e-20f ? p.Scale[axis] / r.Scale[axis] : 1.0f;
			}
		}
		const std::size_t morphs = std::min(pose.MorphWeights.size(), reference.MorphWeights.size());
		delta.MorphWeights.resize(morphs);
		for (std::size_t index = 0; index < morphs; ++index)
		{
			delta.MorphWeights[index] = pose.MorphWeights[index] - reference.MorphWeights[index];
		}
		return delta;
	}

	void AddPose(AnimationPose& base, const AnimationPose& delta, float weight, const BoneMask* mask)
	{
		const std::size_t joints = std::min(base.Joints.size(), delta.Joints.size());
		for (std::uint32_t joint = 0; joint < joints; ++joint)
		{
			const float w = weight * Weight(mask, joint);
			if (w == 0.0f)
			{
				continue;
			}
			JointPose& target = base.Joints[joint];
			const JointPose& d = delta.Joints[joint];
			target.Translation = Add(target.Translation, Scale(d.Translation, w));
			target.Rotation = Normalize(Multiply(target.Rotation, Slerp(IdentityQuat, d.Rotation, w)));
			for (int axis = 0; axis < 3; ++axis)
			{
				target.Scale[axis] *= 1.0f + (d.Scale[axis] - 1.0f) * w;
			}
		}
		const std::size_t morphs = std::min(base.MorphWeights.size(), delta.MorphWeights.size());
		for (std::size_t index = 0; index < morphs; ++index)
		{
			base.MorphWeights[index] += delta.MorphWeights[index] * weight;
		}
	}
} // namespace Swim::Animation
