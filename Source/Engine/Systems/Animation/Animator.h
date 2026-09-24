#pragma once
#include "Engine/Systems/Animation/AnimationClip.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Animation
{
	enum class AnimatorParameterType : std::uint8_t
	{
		Float,
		Bool,
		Trigger // A bool that a transition consumes when it fires.
	};

	struct AnimatorParameterDesc
	{
		std::string Name;
		AnimatorParameterType Type = AnimatorParameterType::Float;
		float Default = 0.0f; // Bools/triggers: nonzero is true.
	};

	enum class AnimatorConditionOp : std::uint8_t
	{
		Greater,
		Less,
		Equal,
		NotEqual,
		IsTrue, // Bools and triggers.
		IsFalse
	};

	struct AnimatorCondition
	{
		std::string Parameter;
		AnimatorConditionOp Op = AnimatorConditionOp::IsTrue;
		float Value = 0.0f;
	};

	inline constexpr std::uint32_t AnyState = InvalidJoint;

	// A state-machine edge. It fires when every condition holds and, with an exit
	// time, once the source state's normalized play time (loops counted, so 1.5 is
	// halfway through the second loop) reaches ExitTime. The destination fades in
	// linearly over Duration seconds while the source keeps playing.
	struct AnimatorTransitionDesc
	{
		std::uint32_t From = AnyState;
		std::uint32_t To = 0;
		float Duration = 0.2f;
		float ExitTime = -1.0f; // Negative: no exit time.
		std::vector<AnimatorCondition> Conditions;
		bool AllowSelf = false; // Any-state edges may re-enter their own destination.
	};

	struct AnimatorStateDesc
	{
		std::string Name;
		std::shared_ptr<const AnimationClip> Clip; // May be null (holds the layer's input pose).
		float Speed = 1.0f;						   // Negative plays backwards.
		bool Loop = true;
		float StartTime = 0.0f; // Normalized entry time.
	};

	enum class LayerBlendMode : std::uint8_t
	{
		Override, // lerp(result, layer pose, weight x mask).
		Additive  // result += weight x mask x (layer pose - the state's pose at clip time 0).
	};

	struct AnimatorLayerDesc
	{
		std::string Name;
		std::vector<AnimatorStateDesc> States;
		std::vector<AnimatorTransitionDesc> Transitions;
		std::uint32_t DefaultState = 0;
		float Weight = 1.0f;
		LayerBlendMode Blend = LayerBlendMode::Override;
		BoneMask Mask; // Empty: every joint.
	};

	// Root motion is taken from layer 0: the root joint's local motion between
	// updates is reported (loops accumulate) and removed from the pose, which keeps
	// the root at its rest value on the extracted axes.
	struct RootMotionDesc
	{
		bool Enabled = false;
		std::string Joint;						  // Empty: joint 0.
		Vec3 TranslationAxes{ 1.0f, 1.0f, 1.0f }; // 1 = extracted, 0 = left animated.
		bool ExtractRotation = false;
	};

	struct AnimatorDesc
	{
		std::shared_ptr<const Skeleton> SharedSkeleton;
		std::vector<AnimatorLayerDesc> Layers;
		std::vector<AnimatorParameterDesc> Parameters;
		MorphLayout Morphs;
		RootMotionDesc RootMotion;
	};

	struct AnimatorEvent
	{
		std::string Name;
		float Time = 0.0f; // Clip seconds.
		std::uint32_t Layer = 0;
		std::uint32_t State = 0;
		float Weight = 1.0f; // The state's blend weight within its layer when it fired.
	};

	struct RootMotionDelta
	{
		Vec3 Translation{ 0.0f, 0.0f, 0.0f }; // In the root joint's parent space.
		Quat Rotation = IdentityQuat;
	};

	// Evaluates layered state machines into a local pose. Owns no GPU data and
	// touches only its own state, so independent animators may update on job
	// workers concurrently (see AnimationUpdate.h).
	class Animator
	{
	  public:
		// Throws std::invalid_argument for a null skeleton, no layers, out-of-range
		// state indices, unknown condition parameters or masks of the wrong size.
		explicit Animator(AnimatorDesc desc);

		bool SetFloat(std::string_view name, float value);
		bool SetBool(std::string_view name, bool value);
		bool SetTrigger(std::string_view name);
		bool ResetTrigger(std::string_view name);
		std::optional<float> GetParameter(std::string_view name) const;

		// Starts a state directly, fading from the current one over `crossfade`.
		bool Play(std::uint32_t layer, std::string_view state, float crossfade = 0.0f, float normalizedStart = 0.0f);

		void SetSpeed(float speed) { globalSpeed = speed; }

		void SetLayerWeight(std::uint32_t layer, float weight);
		// Manual morph weight (applied after every layer); nullopt returns control to clips.
		void SetMorphWeightOverride(std::uint32_t weight, std::optional<float> value);

		// Advances every layer by dt seconds, fires transitions and events, and
		// evaluates the pose and root motion.
		void Update(float dt);

		const AnimationPose& GetPose() const { return pose; }

		std::span<const AnimatorEvent> GetEvents() const { return events; }

		const RootMotionDelta& GetRootMotion() const { return rootMotion; }

		const Skeleton& GetSkeleton() const { return *desc.SharedSkeleton; }

		const MorphLayout& GetMorphLayout() const { return desc.Morphs; }

		std::uint32_t GetCurrentState(std::uint32_t layer) const { return layers[layer].Current.State; }

		bool IsInTransition(std::uint32_t layer) const { return layers[layer].Source.has_value(); }

		float GetTransitionProgress(std::uint32_t layer) const;
		float GetStateTime(std::uint32_t layer) const; // Clip seconds of the current state.
		float GetNormalizedTime(std::uint32_t layer) const;
		std::uint32_t FindState(std::uint32_t layer, std::string_view name) const;

	  private:
		struct Playing
		{
			std::uint32_t State = 0;
			float Unwrapped = 0.0f;			// Clip seconds, loops not wrapped.
			float PreviousUnwrapped = 0.0f; // Before this update's advance.
			bool Entered = true;			// The next advance includes its start time.
		};

		struct LayerRuntime
		{
			Playing Current;
			std::optional<Playing> Source;
			float Elapsed = 0.0f;
			float Duration = 0.0f;
			std::vector<ClipBinding> Bindings; // Per state.
		};

		struct Parameter
		{
			AnimatorParameterDesc Desc;
			float Value = 0.0f;
		};

		Parameter* FindParameter(std::string_view name);
		const Parameter* FindParameter(std::string_view name) const;
		float Duration(std::uint32_t layer, std::uint32_t state) const;
		float LocalTime(std::uint32_t layer, const Playing& playing) const;
		void Start(std::uint32_t layer, std::uint32_t state, float crossfade, float normalizedStart);
		void Advance(std::uint32_t layer, Playing& playing, float dt, float weight);
		bool Evaluate(const AnimatorTransitionDesc& transition, const LayerRuntime& runtime, std::uint32_t layer) const;
		void SampleLayer(std::uint32_t layer, AnimationPose& out, AnimationPose* reference);
		void SampleState(std::uint32_t layer, std::uint32_t state, float time, AnimationPose& out) const;
		JointPose RootAt(std::uint32_t state, float unwrapped) const;
		void ExtractRootMotion();
		void EvaluatePose();

		AnimatorDesc desc;
		std::vector<LayerRuntime> layers;
		std::vector<Parameter> parameters;
		std::vector<std::optional<float>> morphOverrides;
		AnimationPose rest;
		AnimationPose pose;
		AnimationPose scratch;
		AnimationPose scratchReference;
		AnimationPose sourceScratch;
		AnimationPose sourceReference;
		std::vector<AnimatorEvent> events;
		RootMotionDelta rootMotion;
		std::uint32_t rootJoint = 0;
		float globalSpeed = 1.0f;
	};
} // namespace Swim::Animation
