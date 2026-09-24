#pragma once
#include "Engine/Assets/AnimationClipAsset.h"
#include "Engine/Systems/Animation/Skeleton.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Swim::Animation
{
	// Morph weight slots of an animator: each channel names the mesh node a
	// MorphWeights track targets and owns Count consecutive pose weights.
	struct MorphChannel
	{
		std::string Target;
		std::uint32_t Count = 0;
		std::vector<float> DefaultWeights; // Count entries, or empty for zeros.
	};

	struct MorphLayout
	{
		std::vector<MorphChannel> Channels;

		std::uint32_t GetWeightCount() const;
		std::uint32_t GetOffset(std::uint32_t channel) const;
		std::uint32_t FindChannel(std::string_view target) const; // InvalidJoint when absent.
		std::vector<float> GetDefaultWeights() const;
	};

	// A validated clip ready for sampling (shared, read-only).
	class AnimationClip
	{
	  public:
		// Throws std::invalid_argument for tracks the .sasset reader would reject.
		explicit AnimationClip(Assets::AnimationClipAsset asset);

		const std::string& GetName() const { return asset.Name; }

		float GetDuration() const { return asset.Duration; }

		const std::vector<Assets::AnimationTrack>& GetTracks() const { return asset.Tracks; }

		const std::vector<Assets::AnimationEvent>& GetEvents() const { return asset.Events; }

	  private:
		Assets::AnimationClipAsset asset;
	};

	// Where each track of a clip writes for one skeleton + morph layout. Tracks
	// whose target is neither a joint nor a morph channel stay unbound (ignored).
	struct ClipBinding
	{
		struct Target
		{
			std::uint32_t Joint = InvalidJoint;		  // Translation/Rotation/Scale tracks.
			std::uint32_t MorphOffset = InvalidJoint; // MorphWeights tracks: first pose weight.
			std::uint32_t MorphCount = 0;			  // Weights written (min of track and channel).
		};

		std::vector<Target> Targets; // Parallel to the clip's tracks.
		std::uint32_t BoundTracks = 0;
	};

	ClipBinding BindClip(const AnimationClip& clip, const Skeleton& skeleton, const MorphLayout& morphs = {});

	// Writes every bound track's value at `time` (seconds, clamped to the keys)
	// into `pose`; joints and weights without tracks keep their current values.
	void SampleClip(const AnimationClip& clip, const ClipBinding& binding, float time, AnimationPose& pose);

	// Samples one track: `out` receives Components floats (rotations normalized).
	void SampleTrack(const Assets::AnimationTrack& track, float time, std::span<float> out);

	// The rest pose with default morph weights.
	AnimationPose MakeRestPose(const Skeleton& skeleton, const MorphLayout& morphs = {});

	// a = lerp(a, b, weight x mask) per joint (rotations by shortest-path nlerp) and per weight.
	void BlendPose(AnimationPose& a, const AnimationPose& b, float weight, const BoneMask* mask = nullptr);

	// The additive delta of `pose` relative to `reference`: translation and morph
	// differences, rotation reference^-1 * pose, scale ratio.
	AnimationPose MakeAdditivePose(const AnimationPose& pose, const AnimationPose& reference);

	// base += weight x mask x delta (rotation: base * slerp(identity, delta, w)).
	void AddPose(AnimationPose& base, const AnimationPose& delta, float weight, const BoneMask* mask = nullptr);
} // namespace Swim::Animation
