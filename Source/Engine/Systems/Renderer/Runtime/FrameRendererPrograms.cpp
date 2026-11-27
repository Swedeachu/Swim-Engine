#include "Engine/Systems/Renderer/Runtime/FrameRendererImpl.h"

namespace Engine
{

	using namespace FrameRendererDetail;

	void FrameRenderer::Impl::CollectPrograms()
	{
		std::erase_if(retiredPrograms,
			[](const RetiredPrograms& retired)
			{
				return !retired.After.Semaphore || retired.After.Semaphore->GetCompletedValue() >= retired.After.Value;
			});
	}

	RenderFeatureGeometryPipeline FrameRenderer::Impl::GetGeometryPipeline(
		std::string_view name, RenderGeometryPass pass, bool prepassed, const RenderGeometryState& state, S::SampleCount samples)
	{
		const bool shadow = pass == RenderGeometryPass::Shadow;
		auto& program = geometryPrograms[{ std::string(name), shadow }];

		if (!program)
		{
			const auto& spaces = shadow ? geometryShadowSpaces : geometryForwardSpaces;
			program = std::make_unique<RuntimeGraphicsProgram>(shaders.LoadGraphics(name, spaces));
		}

		prepassed = prepassed && pass == RenderGeometryPass::Opaque;
		// Shadow tiles always have one sample; alpha to coverage only where depth and coverage are drawn.
		const auto sampleCount = shadow ? S::SampleCount::X1 : samples;
		const bool coverage = sampleCount != S::SampleCount::X1 && state.AlphaToCoverage &&
			(pass == RenderGeometryPass::DepthPrepass || pass == RenderGeometryPass::Opaque);
		// Shadow tiles keep their own bias (the shadow programs' normal offset); non-finite biases are none.
		const auto bias = [&](float value)
		{
			return !shadow && std::isfinite(value) ? std::clamp(value, -1.0e6f, 1.0e6f) : 0.0f;
		};
		const float depthBias = bias(state.DepthBias);
		const float slopeBias = bias(state.SlopeDepthBias);
		auto& pipeline = geometryPipelines[{ std::string(name), static_cast<std::uint32_t>(pass), prepassed,
			static_cast<std::uint32_t>(state.Cull), static_cast<std::uint32_t>(sampleCount), coverage, depthBias, slopeBias }];

		if (!pipeline)
		{
			pipeline = CreateGeometryPipeline(name, *program, pass, prepassed, state.Cull, sampleCount, coverage, depthBias, slopeBias);
		}

		return { pipeline.get(), program.get() };
	}

	std::unique_ptr<S::GraphicsPipeline> FrameRenderer::Impl::CreateGeometryPipeline(std::string_view name,
		const RuntimeGraphicsProgram& program, RenderGeometryPass pass, bool prepassed, S::CullMode cull, S::SampleCount samples,
		bool alphaToCoverage, float depthBias, float slopeBias)
	{
		auto& p = *program.Program;
		auto& l = *program.Layout;
		S::GraphicsPipelineDesc desc;

		switch (pass)
		{
		case RenderGeometryPass::DepthPrepass:
			desc = R::ForwardPlusRenderer::DepthPrepassPipelineDesc(p, l);
			break;
		case RenderGeometryPass::Opaque:
			desc = prepassed ? R::ForwardPlusRenderer::PrepassedPipelineDesc(p, l)
							 : R::ForwardPlusRenderer::PipelineDesc(R::ForwardPlusBin::Opaque, p, l);
			break;
		case RenderGeometryPass::Transparent:
			desc = R::ForwardPlusRenderer::PipelineDesc(R::ForwardPlusBin::Transparent, p, l);
			break;
		default:
			desc = R::ShadowRenderer::PipelineDesc(p, l);
			break;
		}

		desc.Raster.Cull = cull;
		desc.Samples = samples;
		desc.AlphaToCoverage = alphaToCoverage;
		desc.Raster.DepthBiasConstant = depthBias;
		desc.Raster.DepthBiasSlope = slopeBias;
		const std::string label = "Feature " + std::string(name);
		desc.DebugName = label;
		auto pipeline = device.CreateGraphicsPipeline(desc);

		if (!pipeline)
		{
			throw std::runtime_error("FrameRenderer: cannot create the pipeline of feature program " + std::string(name));
		}

		return pipeline;
	}

	// Before recording a frame; after is the last submission (the old programs' last use).
	void FrameRenderer::Impl::ApplyPrograms(S::TimelinePoint after)
	{
		auto pending = std::move(pendingPrograms);
		pendingPrograms.clear();

		for (auto& item : pending)
		{
			const std::string name = item.Desc.Name;
			std::string error;
			const bool compute = featurePrograms.contains(name) && featurePrograms.at(name);
			bool graphics = false;

			for (const auto& [key, program] : geometryPrograms)
			{
				graphics = graphics || (key.first == name && program);
			}

			if (!compute && !graphics && shaders.IsLoaded(name))
			{
				error = "it is used by an engine pass; the change applies after a restart";
			}
			else
			{
				const auto previous = shaders.Find(name);

				try
				{
					shaders.Replace(item.Desc);
					std::unique_ptr<RuntimeComputeProgram> freshCompute;
					std::map<bool, std::unique_ptr<RuntimeGraphicsProgram>> freshGraphics;
					decltype(geometryPipelines) freshPipelines;

					if (compute)
					{
						freshCompute = std::make_unique<RuntimeComputeProgram>(shaders.LoadCompute(name));
					}

					for (const auto& [key, program] : geometryPrograms)
					{
						if (key.first == name && program)
						{
							const auto& spaces = key.second ? geometryShadowSpaces : geometryForwardSpaces;
							freshGraphics[key.second] = std::make_unique<RuntimeGraphicsProgram>(shaders.LoadGraphics(name, spaces));
						}
					}

					for (const auto& [key, pipeline] : geometryPipelines)
					{
						const auto pass = static_cast<RenderGeometryPass>(std::get<1>(key));
						const auto program = freshGraphics.find(pass == RenderGeometryPass::Shadow);

						if (std::get<0>(key) == name && pipeline && program != freshGraphics.end())
						{
							freshPipelines[key] = CreateGeometryPipeline(name, *program->second, pass, std::get<2>(key),
								static_cast<S::CullMode>(std::get<3>(key)), static_cast<S::SampleCount>(std::get<4>(key)), std::get<5>(key),
								std::get<6>(key), std::get<7>(key));
						}
					}

					RetiredPrograms retired;
					retired.After = after;

					if (freshCompute)
					{
						retired.Compute.push_back(std::exchange(featurePrograms[name], std::move(freshCompute)));
					}

					for (auto& [shadow, program] : freshGraphics)
					{
						retired.Graphics.push_back(std::exchange(geometryPrograms[{ name, shadow }], std::move(program)));
					}

					for (auto& [key, pipeline] : freshPipelines)
					{
						retired.Pipelines.push_back(std::exchange(geometryPipelines[key], std::move(pipeline)));
					}

					retiredPrograms.push_back(std::move(retired));

					environmentValid = false;
				}
				catch (const std::exception& failure)
				{
					error = failure.what();

					if (previous)
					{
						shaders.Replace(*previous);
					}
				}
			}

			if (item.Done)
			{
				item.Done(error);
			}
		}
	}

	RuntimeGraphicsProgram FrameRenderer::Impl::LoadDrawProgram(std::string_view name, const S::DescriptorSchemaDesc* bindlessSpace)
	{
		if (bindlessSpace)
		{
			return shaders.LoadGraphics(name, { bindlessSpace, 1 });
		}

		return shaders.LoadGraphics(name);
	}

	void FrameRenderer::ReplaceProgram(RuntimeShaderDesc desc, std::function<void(std::string_view error)> done)
	{
		impl->pendingPrograms.push_back({ std::move(desc), std::move(done) });
	}

	void FrameRenderer::SetConstantsOverride(
		std::function<void(std::string_view, const RuntimeComputeProgram&, std::span<std::byte>)> callback)
	{
		impl->overrideConstants = std::move(callback);
	}

} // namespace Engine
