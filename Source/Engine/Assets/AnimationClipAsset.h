#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Swim::Assets
{

	enum class AnimationPath : std::uint8_t
	{
		Translation, // 3 components per key.
		Rotation,	 // 4 components per key (x, y, z, w quaternion).
		Scale,		 // 3 components per key.
		MorphWeights // Components = the target mesh's morph target count.
	};

	enum class AnimationInterpolation : std::uint8_t
	{
		Step,
		Linear,		// Rotations: shortest-path slerp.
		CubicSpline // Values hold (in-tangent, value, out-tangent) per key.
	};

	// One animated property. Target names the joint (Translation/Rotation/Scale)
	// or the mesh node (MorphWeights) it drives; runtime binding resolves names
	// against a skeleton, so a clip can drive any skeleton with matching joint
	// names. Times are strictly increasing seconds.
	struct AnimationTrack
	{
		std::string Target;
		AnimationPath Path = AnimationPath::Translation;
		AnimationInterpolation Interpolation = AnimationInterpolation::Linear;
		std::uint32_t Components = 3;
		std::vector<float> Times;
		std::vector<float> Values; // Keys x Components (x 3 for CubicSpline).
	};

	struct AnimationEvent
	{
		float Time = 0.0f;
		std::string Name;
	};

	struct AnimationClipAsset
	{
		std::string Name;
		float Duration = 0.0f; // Seconds; the last key time unless authored longer.
		std::vector<AnimationTrack> Tracks;
		std::vector<AnimationEvent> Events; // Sorted by time.
	};

} // namespace Swim::Assets
