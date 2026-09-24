#include "Engine/Systems/Animation/Animator.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::Animation
{
	namespace
	{
		// Bounds the per-update work of pathological steps (huge dt on a tiny clip).
		constexpr int MaxLoopsPerUpdate = 1024;

		float PositiveModulo(float value, float period)
		{
			const float result = std::fmod(value, period);
			return result < 0.0f ? result + period : result;
		}

		void SampleJointTracks(const AnimationClip& clip, const ClipBinding& binding, std::uint32_t joint, float time, JointPose& out)
		{
			std::array<float, 4> value{};
			const auto& tracks = clip.GetTracks();
			for (std::size_t index = 0; index < tracks.size(); ++index)
			{
				if (binding.Targets[index].Joint != joint)
				{
					continue;
				}
				const Assets::AnimationTrack& track = tracks[index];
				SampleTrack(track, time, std::span<float>(value.data(), track.Components));
				if (track.Path == Assets::AnimationPath::Translation)
				{
					out.Translation = { value[0], value[1], value[2] };
				}
				else if (track.Path == Assets::AnimationPath::Rotation)
				{
					out.Rotation = { value[0], value[1], value[2], value[3] };
				}
				else if (track.Path == Assets::AnimationPath::Scale)
				{
					out.Scale = { value[0], value[1], value[2] };
				}
			}
		}
	} // namespace

	Animator::Animator(AnimatorDesc source) : desc(std::move(source))
	{
		if (!desc.SharedSkeleton)
		{
			throw std::invalid_argument("Animator needs a skeleton");
		}
		if (desc.Layers.empty())
		{
			throw std::invalid_argument("Animator needs at least one layer");
		}
		const Skeleton& skeleton = *desc.SharedSkeleton;
		for (const AnimatorParameterDesc& parameter : desc.Parameters)
		{
			if (FindParameter(parameter.Name))
			{
				throw std::invalid_argument("Animator parameter names must be unique: " + parameter.Name);
			}
			parameters.push_back({ parameter, parameter.Default });
		}
		layers.resize(desc.Layers.size());
		for (std::uint32_t index = 0; index < desc.Layers.size(); ++index)
		{
			const AnimatorLayerDesc& layer = desc.Layers[index];
			if (layer.States.empty() || layer.DefaultState >= layer.States.size())
			{
				throw std::invalid_argument("Animator layer needs states and a valid default state");
			}
			if (!layer.Mask.Weights.empty() && layer.Mask.Weights.size() != skeleton.GetJointCount())
			{
				throw std::invalid_argument("Animator layer mask does not match the skeleton");
			}
			for (const AnimatorTransitionDesc& transition : layer.Transitions)
			{
				if (transition.To >= layer.States.size() || (transition.From != AnyState && transition.From >= layer.States.size()))
				{
					throw std::invalid_argument("Animator transition references a state outside its layer");
				}
				for (const AnimatorCondition& condition : transition.Conditions)
				{
					if (!FindParameter(condition.Parameter))
					{
						throw std::invalid_argument("Animator condition references an unknown parameter: " + condition.Parameter);
					}
				}
			}
			for (const AnimatorStateDesc& state : layer.States)
			{
				layers[index].Bindings.push_back(state.Clip ? BindClip(*state.Clip, skeleton, desc.Morphs) : ClipBinding{});
			}
		}
		if (desc.RootMotion.Enabled)
		{
			rootJoint = desc.RootMotion.Joint.empty() ? 0u : skeleton.FindJoint(desc.RootMotion.Joint);
			if (rootJoint == InvalidJoint)
			{
				throw std::invalid_argument("Animator root motion joint is not in the skeleton: " + desc.RootMotion.Joint);
			}
		}
		rest = MakeRestPose(skeleton, desc.Morphs);
		pose = rest;
		morphOverrides.resize(rest.MorphWeights.size());
		for (std::uint32_t index = 0; index < layers.size(); ++index)
		{
			Start(index, desc.Layers[index].DefaultState, 0.0f, desc.Layers[index].States[desc.Layers[index].DefaultState].StartTime);
		}
		// The initial pose; the first Update fires the default states' start events.
		EvaluatePose();
	}

	Animator::Parameter* Animator::FindParameter(std::string_view name)
	{
		for (Parameter& parameter : parameters)
		{
			if (parameter.Desc.Name == name)
			{
				return &parameter;
			}
		}
		return nullptr;
	}

	const Animator::Parameter* Animator::FindParameter(std::string_view name) const
	{
		return const_cast<Animator*>(this)->FindParameter(name);
	}

	bool Animator::SetFloat(std::string_view name, float value)
	{
		Parameter* parameter = FindParameter(name);
		if (!parameter || parameter->Desc.Type != AnimatorParameterType::Float)
		{
			return false;
		}
		parameter->Value = value;
		return true;
	}

	bool Animator::SetBool(std::string_view name, bool value)
	{
		Parameter* parameter = FindParameter(name);
		if (!parameter || parameter->Desc.Type != AnimatorParameterType::Bool)
		{
			return false;
		}
		parameter->Value = value ? 1.0f : 0.0f;
		return true;
	}

	bool Animator::SetTrigger(std::string_view name)
	{
		Parameter* parameter = FindParameter(name);
		if (!parameter || parameter->Desc.Type != AnimatorParameterType::Trigger)
		{
			return false;
		}
		parameter->Value = 1.0f;
		return true;
	}

	bool Animator::ResetTrigger(std::string_view name)
	{
		Parameter* parameter = FindParameter(name);
		if (!parameter || parameter->Desc.Type != AnimatorParameterType::Trigger)
		{
			return false;
		}
		parameter->Value = 0.0f;
		return true;
	}

	std::optional<float> Animator::GetParameter(std::string_view name) const
	{
		const Parameter* parameter = FindParameter(name);
		return parameter ? std::optional<float>(parameter->Value) : std::nullopt;
	}

	std::uint32_t Animator::FindState(std::uint32_t layer, std::string_view name) const
	{
		const auto& states = desc.Layers[layer].States;
		for (std::uint32_t index = 0; index < states.size(); ++index)
		{
			if (states[index].Name == name)
			{
				return index;
			}
		}
		return InvalidJoint;
	}

	bool Animator::Play(std::uint32_t layer, std::string_view state, float crossfade, float normalizedStart)
	{
		if (layer >= layers.size())
		{
			return false;
		}
		const std::uint32_t index = FindState(layer, state);
		if (index == InvalidJoint)
		{
			return false;
		}
		Start(layer, index, crossfade, normalizedStart);
		return true;
	}

	void Animator::SetLayerWeight(std::uint32_t layer, float weight)
	{
		desc.Layers.at(layer).Weight = std::clamp(weight, 0.0f, 1.0f);
	}

	void Animator::SetMorphWeightOverride(std::uint32_t weight, std::optional<float> value)
	{
		morphOverrides.at(weight) = value;
	}

	float Animator::Duration(std::uint32_t layer, std::uint32_t state) const
	{
		const auto& clip = desc.Layers[layer].States[state].Clip;
		return clip ? clip->GetDuration() : 0.0f;
	}

	float Animator::LocalTime(std::uint32_t layer, const Playing& playing) const
	{
		const float duration = Duration(layer, playing.State);
		if (!(duration > 0.0f))
		{
			return 0.0f;
		}
		return desc.Layers[layer].States[playing.State].Loop ? PositiveModulo(playing.Unwrapped, duration)
															 : std::clamp(playing.Unwrapped, 0.0f, duration);
	}

	float Animator::GetStateTime(std::uint32_t layer) const
	{
		return LocalTime(layer, layers[layer].Current);
	}

	float Animator::GetNormalizedTime(std::uint32_t layer) const
	{
		const float duration = Duration(layer, layers[layer].Current.State);
		return duration > 0.0f ? layers[layer].Current.Unwrapped / duration : 1.0f;
	}

	float Animator::GetTransitionProgress(std::uint32_t layer) const
	{
		const LayerRuntime& runtime = layers[layer];
		if (!runtime.Source)
		{
			return 1.0f;
		}
		return runtime.Duration > 0.0f ? std::clamp(runtime.Elapsed / runtime.Duration, 0.0f, 1.0f) : 1.0f;
	}

	void Animator::Start(std::uint32_t layer, std::uint32_t state, float crossfade, float normalizedStart)
	{
		LayerRuntime& runtime = layers[layer];
		Playing next;
		next.State = state;
		next.Unwrapped = std::clamp(normalizedStart, 0.0f, 1.0f) * Duration(layer, state);
		next.PreviousUnwrapped = next.Unwrapped;
		next.Entered = true;
		if (crossfade > 0.0f)
		{
			// A fade started mid-fade continues from the state that was fading in.
			runtime.Source = runtime.Current;
			runtime.Elapsed = 0.0f;
			runtime.Duration = crossfade;
		}
		else
		{
			runtime.Source.reset();
		}
		runtime.Current = next;
	}

	void Animator::Advance(std::uint32_t layer, Playing& playing, float dt, float weight)
	{
		const AnimatorStateDesc& state = desc.Layers[layer].States[playing.State];
		const float duration = Duration(layer, playing.State);
		const float delta = dt * state.Speed * globalSpeed;
		const float previous = playing.Unwrapped;
		float next = previous + delta;
		if (!state.Loop)
		{
			next = std::clamp(next, 0.0f, duration);
		}
		else if (duration > 0.0f && std::abs(next - previous) > duration * MaxLoopsPerUpdate)
		{
			next = previous + std::copysign(duration * MaxLoopsPerUpdate, delta);
		}
		playing.PreviousUnwrapped = previous;
		playing.Unwrapped = next;
		const bool inclusiveStart = playing.Entered;
		playing.Entered = false;

		if (!state.Clip || state.Clip->GetEvents().empty() || !(duration > 0.0f))
		{
			return;
		}
		// Occurrences of each event at e + k x duration (k = 0 only without looping):
		// forward in (previous, next], backward in [next, previous); the start of a
		// freshly entered state is inclusive.
		const bool forward = next >= previous;
		const float low = forward ? previous : next;
		const float high = forward ? next : previous;
		std::vector<std::pair<float, const Assets::AnimationEvent*>> fired;
		for (const Assets::AnimationEvent& event : state.Clip->GetEvents())
		{
			int first = 0, last = 0;
			if (state.Loop)
			{
				first = static_cast<int>(std::ceil((low - event.Time) / duration)) - 1;
				last = static_cast<int>(std::floor((high - event.Time) / duration)) + 1;
			}
			for (int k = first; k <= last; ++k)
			{
				const float at = event.Time + static_cast<float>(k) * duration;
				const bool inside = forward ? ((at > low || (inclusiveStart && at == low)) && at <= high)
											: (at >= low && (at < high || (inclusiveStart && at == high)));
				if (inside)
				{
					fired.push_back({ at, &event });
				}
			}
		}
		std::stable_sort(fired.begin(), fired.end(),
			[forward](const auto& a, const auto& b)
			{
				return forward ? a.first < b.first : a.first > b.first;
			});
		for (const auto& [at, event] : fired)
		{
			events.push_back({ event->Name, event->Time, layer, playing.State, weight });
		}
	}

	bool Animator::Evaluate(const AnimatorTransitionDesc& transition, const LayerRuntime& runtime, std::uint32_t layer) const
	{
		if (transition.From != AnyState && transition.From != runtime.Current.State)
		{
			return false;
		}
		if (transition.From == AnyState && transition.To == runtime.Current.State && !transition.AllowSelf)
		{
			return false;
		}
		if (transition.ExitTime >= 0.0f)
		{
			const float duration = Duration(layer, runtime.Current.State);
			if (duration > 0.0f && runtime.Current.Unwrapped / duration < transition.ExitTime)
			{
				return false;
			}
		}
		for (const AnimatorCondition& condition : transition.Conditions)
		{
			const float value = FindParameter(condition.Parameter)->Value;
			bool holds = false;
			switch (condition.Op)
			{
			case AnimatorConditionOp::Greater:
				holds = value > condition.Value;
				break;
			case AnimatorConditionOp::Less:
				holds = value < condition.Value;
				break;
			case AnimatorConditionOp::Equal:
				holds = value == condition.Value;
				break;
			case AnimatorConditionOp::NotEqual:
				holds = value != condition.Value;
				break;
			case AnimatorConditionOp::IsTrue:
				holds = value != 0.0f;
				break;
			case AnimatorConditionOp::IsFalse:
				holds = value == 0.0f;
				break;
			}
			if (!holds)
			{
				return false;
			}
		}
		return true;
	}

	void Animator::SampleState(std::uint32_t layer, std::uint32_t state, float time, AnimationPose& out) const
	{
		const auto& clip = desc.Layers[layer].States[state].Clip;
		if (clip)
		{
			SampleClip(*clip, layers[layer].Bindings[state], time, out);
		}
	}

	void Animator::SampleLayer(std::uint32_t layer, AnimationPose& out, AnimationPose* reference)
	{
		const LayerRuntime& runtime = layers[layer];
		const bool additive = desc.Layers[layer].Blend == LayerBlendMode::Additive;
		// Override layers start from the pose below them, so joints their clips do not
		// animate keep it; additive layers start from rest (untouched joints add nothing).
		const AnimationPose& base = additive ? rest : pose;
		out = base;
		SampleState(layer, runtime.Current.State, LocalTime(layer, runtime.Current), out);
		if (reference)
		{
			*reference = base;
			SampleState(layer, runtime.Current.State, 0.0f, *reference);
		}
		if (!runtime.Source)
		{
			return;
		}
		const float progress = GetTransitionProgress(layer);
		sourceScratch = base;
		SampleState(layer, runtime.Source->State, LocalTime(layer, *runtime.Source), sourceScratch);
		BlendPose(sourceScratch, out, progress);
		out = sourceScratch;
		if (reference)
		{
			sourceReference = base;
			SampleState(layer, runtime.Source->State, 0.0f, sourceReference);
			BlendPose(sourceReference, *reference, progress);
			*reference = sourceReference;
		}
	}

	JointPose Animator::RootAt(std::uint32_t state, float unwrapped) const
	{
		JointPose result = rest.Joints[rootJoint];
		const AnimatorStateDesc& desc0 = desc.Layers[0].States[state];
		if (!desc0.Clip)
		{
			return result;
		}
		const ClipBinding& binding = layers[0].Bindings[state];
		const float duration = desc0.Clip->GetDuration();
		if (!desc0.Loop || !(duration > 0.0f))
		{
			SampleJointTracks(*desc0.Clip, binding, rootJoint, std::clamp(unwrapped, 0.0f, duration), result);
			return result;
		}
		// Each completed loop adds the clip's net root motion (end relative to start).
		const float cycles = std::floor(unwrapped / duration);
		JointPose start = rest.Joints[rootJoint];
		JointPose end = start;
		SampleJointTracks(*desc0.Clip, binding, rootJoint, 0.0f, start);
		SampleJointTracks(*desc0.Clip, binding, rootJoint, duration, end);
		SampleJointTracks(*desc0.Clip, binding, rootJoint, unwrapped - cycles * duration, result);
		result.Translation = Add(result.Translation, Scale(Sub(end.Translation, start.Translation), cycles));
		const Quat cycle = Normalize(Multiply(end.Rotation, Inverse(start.Rotation)));
		const Quat step = cycles >= 0.0f ? cycle : Inverse(cycle);
		const int count = std::min(static_cast<int>(std::abs(cycles)), MaxLoopsPerUpdate * 64);
		Quat accumulated = IdentityQuat;
		for (int index = 0; index < count; ++index)
		{
			accumulated = Normalize(Multiply(step, accumulated));
		}
		result.Rotation = Normalize(Multiply(accumulated, result.Rotation));
		return result;
	}

	void Animator::ExtractRootMotion()
	{
		const LayerRuntime& runtime = layers[0];
		const float progress = GetTransitionProgress(0);
		const auto deltaOf = [&](const Playing& playing, Vec3& translation, Quat& rotation)
		{
			const JointPose before = RootAt(playing.State, playing.PreviousUnwrapped);
			const JointPose after = RootAt(playing.State, playing.Unwrapped);
			translation = Sub(after.Translation, before.Translation);
			rotation = Normalize(Multiply(after.Rotation, Inverse(before.Rotation)));
		};
		Vec3 translation{};
		Quat rotation = IdentityQuat;
		deltaOf(runtime.Current, translation, rotation);
		if (runtime.Source)
		{
			Vec3 sourceTranslation{};
			Quat sourceRotation = IdentityQuat;
			deltaOf(*runtime.Source, sourceTranslation, sourceRotation);
			translation = Lerp(sourceTranslation, translation, progress);
			rotation = Nlerp(sourceRotation, rotation, progress);
		}
		const Vec3& axes = desc.RootMotion.TranslationAxes;
		rootMotion.Translation = Mul(translation, axes);
		rootMotion.Rotation = desc.RootMotion.ExtractRotation ? rotation : IdentityQuat;

		JointPose& root = pose.Joints[rootJoint];
		for (int axis = 0; axis < 3; ++axis)
		{
			root.Translation[axis] += (rest.Joints[rootJoint].Translation[axis] - root.Translation[axis]) * axes[axis];
		}
		if (desc.RootMotion.ExtractRotation)
		{
			root.Rotation = rest.Joints[rootJoint].Rotation;
		}
	}

	void Animator::Update(float dt)
	{
		events.clear();
		rootMotion = {};
		for (std::uint32_t index = 0; index < layers.size(); ++index)
		{
			LayerRuntime& runtime = layers[index];
			const float progress = GetTransitionProgress(index);
			Advance(index, runtime.Current, dt, progress);
			if (runtime.Source)
			{
				Advance(index, *runtime.Source, dt, 1.0f - progress);
				runtime.Elapsed += std::abs(dt);
			}
		}

		// Transitions fire only outside a fade (Play may still interrupt one).
		for (std::uint32_t index = 0; index < layers.size(); ++index)
		{
			LayerRuntime& runtime = layers[index];
			if (runtime.Source && runtime.Elapsed < runtime.Duration)
			{
				continue;
			}
			const auto& transitions = desc.Layers[index].Transitions;
			const AnimatorTransitionDesc* chosen = nullptr;
			for (int pass = 0; pass < 2 && !chosen; ++pass)
			{
				for (const AnimatorTransitionDesc& transition : transitions)
				{
					if ((transition.From == AnyState) == (pass == 0) && Evaluate(transition, runtime, index))
					{
						chosen = &transition;
						break;
					}
				}
			}
			if (!chosen)
			{
				continue;
			}
			for (const AnimatorCondition& condition : chosen->Conditions)
			{
				Parameter* parameter = FindParameter(condition.Parameter);
				if (parameter->Desc.Type == AnimatorParameterType::Trigger)
				{
					parameter->Value = 0.0f;
				}
			}
			// The finished fade (if any) is dropped before the new one starts.
			runtime.Source.reset();
			Start(index, chosen->To, chosen->Duration, desc.Layers[index].States[chosen->To].StartTime);
		}

		EvaluatePose();
	}

	void Animator::EvaluatePose()
	{
		pose = rest;
		for (std::uint32_t index = 0; index < layers.size(); ++index)
		{
			const AnimatorLayerDesc& layer = desc.Layers[index];
			if (layer.Weight <= 0.0f)
			{
				continue;
			}
			const BoneMask* mask = layer.Mask.Weights.empty() ? nullptr : &layer.Mask;
			if (layer.Blend == LayerBlendMode::Additive)
			{
				SampleLayer(index, scratch, &scratchReference);
				AddPose(pose, MakeAdditivePose(scratch, scratchReference), layer.Weight, mask);
			}
			else
			{
				SampleLayer(index, scratch, nullptr);
				if (layer.Weight >= 1.0f && !mask)
				{
					pose = scratch;
				}
				else
				{
					BlendPose(pose, scratch, layer.Weight, mask);
				}
			}
		}
		if (desc.RootMotion.Enabled)
		{
			ExtractRootMotion();
		}
		for (std::size_t index = 0; index < morphOverrides.size(); ++index)
		{
			if (morphOverrides[index])
			{
				pose.MorphWeights[index] = *morphOverrides[index];
			}
		}
		// Finished fades end here, after their last blended pose.
		for (LayerRuntime& runtime : layers)
		{
			if (runtime.Source && runtime.Elapsed >= runtime.Duration)
			{
				runtime.Source.reset();
			}
		}
	}
} // namespace Swim::Animation
