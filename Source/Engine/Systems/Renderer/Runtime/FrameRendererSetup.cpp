#include "Engine/Systems/Renderer/Runtime/FrameRendererImpl.h"

namespace Engine
{

	using namespace FrameRendererDetail;

	FrameRenderer::Impl::Impl(RenderDevice& renderDeviceValue, Swim::Assets::AssetSystem& assets, Swim::IO::AsyncIoService& io,
		Swim::Jobs::JobSystem* jobs, const FrameRendererDesc& descValue)
		: device(renderDeviceValue.GetDevice()), renderDevice(renderDeviceValue),
		  shaders(renderDeviceValue.GetDevice(), descValue.ShaderRoot),
		  drawPath(R::SelectVisibilityDrawPath(renderDeviceValue.GetCapabilities())), desc(descValue)
	{
		// SwiftShader advertises drawIndirectCount but implements vkCmdDrawIndexedIndirectCount
		// as an unsupported stub (nothing is drawn): use the zero-filled fallback there, and
		// wherever SWIM_FORCE_INDIRECT_FALLBACK=1 asks for it.
		constexpr std::uint32_t SwiftShaderVendor = 0x1AE0u;
		const char* forced = std::getenv("SWIM_FORCE_INDIRECT_FALLBACK");

		if (desc.ForceIndirectFallback || renderDeviceValue.GetAdapterInfo().VendorId == SwiftShaderVendor ||
			(forced && std::string_view(forced) == "1"))
		{
			drawPath = R::VisibilityDrawPath::ZeroFilledIndirect;
		}

		for (const auto name : ShaderLibrary::RequiredPrograms())
		{
			if (!shaders.Contains(name))
			{
				throw std::runtime_error(
					"FrameRenderer: shader program '" + std::string(name) + "' is missing from " + shaders.GetRoot().string());
			}
		}

		LoadComputePrograms();
		CreateDrawPipelines();
		CreateSharedResources();
		CreateResidency(assets, io, jobs);
		CreateScenePasses();
		CreateEffects();
	}

	void FrameRenderer::Impl::LoadComputePrograms()
	{
		// --- Programs --------------------------------------------------------------
		visibilityProgram = shaders.LoadCompute("GpuVisibility");
		clusterCull = shaders.LoadCompute("ClusterLightCull");
		clusterBounds = shaders.LoadCompute("ClusterBounds");
		clusterAssign = shaders.LoadCompute("ClusterAssign");
		clusterScan = shaders.LoadCompute("ClusterScan");
		sortProgram = shaders.LoadCompute("ForwardTransparentSort");
		environmentSky = shaders.LoadCompute("EnvironmentSky");
		environmentDownsample = shaders.LoadCompute("EnvironmentDownsample");
		environmentPrefilter = shaders.LoadCompute("EnvironmentPrefilter");
		environmentIrradiance = shaders.LoadCompute("EnvironmentIrradiance");
		environmentLut = shaders.LoadCompute("EnvironmentBrdfLut");
		postHistogram = shaders.LoadCompute("PostHistogram");
		postExposure = shaders.LoadCompute("PostExposure");
		postBloomDown = shaders.LoadCompute("PostBloomDownsample");
		postBloomUp = shaders.LoadCompute("PostBloomUpsample");
		postComposite = shaders.LoadCompute("PostComposite");
		postCompositeHdr = shaders.LoadCompute("PostCompositeHdr");
		temporalResolve = shaders.LoadCompute("TemporalResolve");
		ssAo = shaders.LoadCompute("ScreenSpaceAo");
		ssBlur = shaders.LoadCompute("ScreenSpaceBlur");
		ssComposite = shaders.LoadCompute("ScreenSpaceComposite");
		ssReflection = shaders.LoadCompute("ScreenSpaceReflection");

		if (shaders.Contains("ScreenSpaceCompositeHalf"))
		{
			ssCompositeHalf = shaders.LoadCompute("ScreenSpaceCompositeHalf");
		}

		if (shaders.Contains("ScreenSpaceLinearDepth") && renderDevice.GetCapabilities().StorageImageExtendedFormats)
		{
			ssLinearDepth = shaders.LoadCompute("ScreenSpaceLinearDepth");
		}

		if (shaders.Contains("ScreenSpaceReflectionTemporal"))
		{
			ssReflectionTemporal = shaders.LoadCompute("ScreenSpaceReflectionTemporal");
		}

		particleSimulate = shaders.LoadCompute("ParticleSimulate");
		particleEmit = shaders.LoadCompute("ParticleEmit");
		particleCompact = shaders.LoadCompute("ParticleCompact");
		particleFinalize = shaders.LoadCompute("ParticleFinalize");
		skinningProgram = shaders.LoadCompute("Skinning");
	}

	void FrameRenderer::Impl::CreateDrawPipelines()
	{
		// One bindless space (samplers + textures) shared by Forward+, masked shadows,
		// particles and UI; every layout defines it identically.
		const auto bindlessSpace = R::ForwardPlusBindlessSpace(desc.BindlessTextures, desc.BindlessSamplers);
		const auto shadowSpace = R::ShadowBindlessSpace(desc.BindlessTextures, desc.BindlessSamplers);
		forwardOpaque = LoadDrawProgram("ForwardOpaque", &bindlessSpace);
		forwardTransparent = LoadDrawProgram("ForwardTransparent", &bindlessSpace);
		forwardDepth = LoadDrawProgram("ForwardDepth", &bindlessSpace);
		forwardPrepassed = LoadDrawProgram("ForwardOpaquePrepassed", &bindlessSpace);
		shadowDepth = LoadDrawProgram("ShadowDepth", nullptr);
		shadowMasked = LoadDrawProgram("ShadowMasked", &shadowSpace);
		particleRender = LoadDrawProgram("ParticleRender", &bindlessSpace);
		uiQuad = LoadDrawProgram("UiQuad", &bindlessSpace);
		sceneViewport = LoadDrawProgram("SceneViewport", &bindlessSpace);
		skyBackground = LoadDrawProgram("SkyBackground", nullptr);
		present = LoadDrawProgram("Present", nullptr);
		forwardOpaquePipeline = Require(device.CreateGraphicsPipeline(R::ForwardPlusRenderer::PipelineDesc(
											R::ForwardPlusBin::Opaque, *forwardOpaque.Program, *forwardOpaque.Layout)),
			"Forward+ opaque");
		forwardTransparentPipeline = Require(device.CreateGraphicsPipeline(R::ForwardPlusRenderer::PipelineDesc(
												 R::ForwardPlusBin::Transparent, *forwardTransparent.Program, *forwardTransparent.Layout)),
			"Forward+ transparent");
		forwardDepthPipeline = Require(
			device.CreateGraphicsPipeline(R::ForwardPlusRenderer::DepthPrepassPipelineDesc(*forwardDepth.Program, *forwardDepth.Layout)),
			"Forward+ depth prepass");
		forwardPrepassedPipeline = Require(device.CreateGraphicsPipeline(R::ForwardPlusRenderer::PrepassedPipelineDesc(
											   *forwardPrepassed.Program, *forwardPrepassed.Layout)),
			"Forward+ prepassed opaque");
		shadowDepthPipeline = Require(
			device.CreateGraphicsPipeline(R::ShadowRenderer::PipelineDesc(*shadowDepth.Program, *shadowDepth.Layout)), "shadow depth");
		shadowMaskedPipeline = Require(
			device.CreateGraphicsPipeline(R::ShadowRenderer::PipelineDesc(*shadowMasked.Program, *shadowMasked.Layout)), "masked shadow");
		particleAdditive = Require(device.CreateGraphicsPipeline(R::ParticleSystem::PipelineDesc(
									   R::ParticleBlendMode::Additive, *particleRender.Program, *particleRender.Layout)),
			"additive particle");
		particleAlpha = Require(device.CreateGraphicsPipeline(R::ParticleSystem::PipelineDesc(
									R::ParticleBlendMode::AlphaBlend, *particleRender.Program, *particleRender.Layout)),
			"alpha particle");
		uiPipeline = Require(
			device.CreateGraphicsPipeline(R::UiRenderer::PipelineDesc(S::Format::RGBA8Unorm, *uiQuad.Program, *uiQuad.Layout)), "UI");
		uiDepthPipeline = Require(device.CreateGraphicsPipeline(R::UiRenderer::PipelineDesc(
									  S::Format::RGBA8Unorm, *uiQuad.Program, *uiQuad.Layout, S::Format::D32Float)),
			"world UI");
		uiInvertPipeline = Require(device.CreateGraphicsPipeline(R::UiRenderer::PipelineDesc(S::Format::RGBA8Unorm, *uiQuad.Program,
									   *uiQuad.Layout, S::Format::Undefined, Swim::UI::UiBlend::Invert)),
			"UI invert");
		uiInvertDepthPipeline = Require(device.CreateGraphicsPipeline(R::UiRenderer::PipelineDesc(S::Format::RGBA8Unorm, *uiQuad.Program,
											*uiQuad.Layout, S::Format::D32Float, Swim::UI::UiBlend::Invert)),
			"world UI invert");
		sceneViewportPipeline = Require(
			device.CreateGraphicsPipeline(SceneViewports::PipelineDesc(*sceneViewport.Program, *sceneViewport.Layout)), "scene viewport");
		sceneViewportTranslucentPipeline =
			Require(device.CreateGraphicsPipeline(SceneViewports::TranslucentPipelineDesc(*sceneViewport.Program, *sceneViewport.Layout)),
				"translucent scene viewport");
		{
			// Feature geometry programs share the frame tables of the engine passes they draw in.
			const auto frameSpace = [](std::initializer_list<const S::PipelineLayout*> layouts)
			{
				S::DescriptorSchemaDesc space{ 0, {} };

				for (const auto* layout : layouts)
				{
					for (const auto& schema : layout->GetInterface().DescriptorSchemas)
					{
						for (const auto& binding : schema.Bindings)
						{
							if (schema.Space == 0 &&
								std::none_of(space.Bindings.begin(), space.Bindings.end(),
									[&](const auto& known)
									{
										return known.Binding == binding.Binding;
									}))
							{
								space.Bindings.push_back(binding);
							}
						}
					}
				}

				for (auto& binding : space.Bindings)
				{
					binding.Stages = S::ShaderStageMask::Vertex | S::ShaderStageMask::Fragment;
				}

				return space;
			};
			geometryForwardSpaces = { frameSpace({ forwardOpaque.Layout.get(), forwardTransparent.Layout.get() }), bindlessSpace };
			geometryShadowSpaces = { frameSpace({ shadowDepth.Layout.get(), shadowMasked.Layout.get() }), shadowSpace };
		}
		{
			// The sky pass writes the colour target and clears depth (the opaque pass clears the
			// other Forward+ targets: ForwardPlusTargets::ClearAuxiliary).
			static const std::array<S::Format, 1> formats{ R::ForwardPlusRenderer::ColorFormat };
			S::GraphicsPipelineDesc skyDesc{};
			skyDesc.Program = skyBackground.Program.get();
			skyDesc.Layout = skyBackground.Layout.get();
			skyDesc.ColorFormats = formats;
			skyDesc.DepthStencilFormat = R::CanonicalDepthFormat;
			skyDesc.DepthStencil.DepthTest = false;
			skyDesc.DepthStencil.DepthWrite = false;
			skyDesc.Raster.Cull = S::CullMode::None;
			skyDesc.DebugName = "Sky background";
			skyPipeline = Require(device.CreateGraphicsPipeline(skyDesc), "sky background");
		}
	}

	void FrameRenderer::Impl::CreateSharedResources()
	{
		// --- Shared resources ------------------------------------------------------
		{
			S::TextureDesc white{};
			white.Extent = { 1, 1, 1 };
			white.PixelFormat = S::Format::RGBA8Unorm;
			white.Usage = S::TextureUsage::Sampled | S::TextureUsage::TransferDestination;
			white.DebugName = "Bindless fallback";
			whiteTexture = Require(device.CreateTexture(white), "fallback texture");
			S::TextureViewDesc view{};
			view.PixelFormat = white.PixelFormat;
			whiteView = Require(device.CreateTextureView(*whiteTexture, view), "fallback view");
		}
		{
			S::SamplerDesc repeat{};
			repeat.EnableAnisotropy = true;
			repeat.MaxAnisotropy = 8.0f;
			repeat.DebugName = "Material sampler";
			linearRepeat = device.CreateSampler(repeat);

			if (!linearRepeat)
			{
				repeat.EnableAnisotropy = false;
				linearRepeat = Require(device.CreateSampler(repeat), "material sampler");
			}

			S::SamplerDesc point{};
			point.MinFilter = point.MagFilter = S::Filter::Nearest;
			point.MaxLod = 4.0f; // Pixel-art tiles stop blending into their atlas neighbours.
			point.DebugName = "Point material sampler";
			pointRepeat = Require(device.CreateSampler(point), "point material sampler");

			S::SamplerDesc clamp{};
			clamp.AddressU = clamp.AddressV = clamp.AddressW = S::SamplerAddressMode::ClampToEdge;
			clamp.DebugName = "Linear clamp";
			linearClamp = Require(device.CreateSampler(clamp), "linear clamp sampler");
			S::SamplerDesc presentDesc = clamp;
			presentDesc.MinFilter = presentDesc.MagFilter = S::Filter::Nearest;
			presentDesc.MipFilter = S::Filter::Nearest;
			presentDesc.DebugName = "Present sampler";
			presentSampler = Require(device.CreateSampler(presentDesc), "present sampler");
		}
		R::BindlessTableDesc bindlessDesc;
		bindlessDesc.Layout = forwardOpaque.Layout.get();
		bindlessDesc.Space = R::ForwardPlusDrawBindings::BindlessSpace;
		bindlessDesc.FallbackTexture = whiteView.get();
		bindlessDesc.FallbackSampler = linearRepeat.get();
		bindlessDesc.DebugName = "Runtime bindless";
		bindless = std::make_unique<R::BindlessResourceTable>(device, bindlessDesc);
		materialSampler = bindless->GetIndex(bindless->RegisterSampler(*linearRepeat));
		pointMaterialSampler = bindless->GetIndex(bindless->RegisterSampler(*pointRepeat));
	}

	void FrameRenderer::Impl::CreateResidency(Swim::Assets::AssetSystem& assets, Swim::IO::AsyncIoService& io, Swim::Jobs::JobSystem* jobs)
	{
		// --- Residency -------------------------------------------------------------
		R::GeometryHeapDesc heapDesc;
		heapDesc.VertexPageSize = desc.VertexPageSize;
		heapDesc.IndexPageSize = desc.IndexPageSize;
		heapDesc.MeshletPageSize = 4ull << 20;
		heapDesc.MaxMeshes = 4096;
		heapDesc.MaxSubmeshes = 16384;
		heapDesc.MaxPages = 64;
		heapDesc.DebugName = "Runtime geometry";
		geometry = std::make_unique<R::GeometryHeap>(device, heapDesc);
		textures = std::make_unique<R::TextureResidency>(device, R::TextureResidencyDesc{ desc.BindlessTextures, "Runtime textures" });
		R::AssetResidencyDesc residencyDesc;
		residencyDesc.Bindless = bindless.get();
		residencyDesc.RetainCpuAssets = false;
		residency = std::make_unique<R::AssetResidencyService>(assets, io, jobs, *geometry, *textures, residencyDesc);

		materialTemplate = R::CreateStandardMaterialTemplate();
		materialTable = std::make_unique<R::GpuMaterialTable>(
			device, R::GpuMaterialTableDesc{ materialTemplate, desc.MaxMaterials, "Runtime materials" });
		materials = std::make_unique<MaterialLibrary>(*materialTable, materialTemplate, *residency, materialSampler, pointMaterialSampler);
		meshes = std::make_unique<MeshLibrary>(assets, *residency, *geometry);
		materials->SetTextureResolver(
			[library = meshes.get()](MeshLibrary::TextureHandle texture)
			{
				return library->Resolve(texture);
			});

		scene = std::make_unique<R::GpuScene>(device, R::GpuSceneDesc{ desc.MaxObjects, "Runtime scene" });
		lights = std::make_unique<R::GpuLightBuffer>(
			device, R::GpuLightBufferDesc{ desc.MaxDirectionalLights, desc.MaxLocalLights, "Runtime lights" });

		R::ParticleSystemDesc particleDesc;
		particleDesc.Capacity = desc.MaxParticles;
		particleDesc.MaxEmitters = desc.MaxEmitters;
		particleDesc.Simulate = { particleSimulate.Pipeline.get(), particleSimulate.Layout.get() };
		particleDesc.Emit = { particleEmit.Pipeline.get(), particleEmit.Layout.get() };
		particleDesc.Compact = { particleCompact.Pipeline.get(), particleCompact.Layout.get() };
		particleDesc.Finalize = { particleFinalize.Pipeline.get(), particleFinalize.Layout.get() };
		particles = std::make_unique<R::ParticleSystem>(device, particleDesc);

		R::SkinningSystemDesc skinDesc;
		skinDesc.Pipeline = skinningProgram.Pipeline.get();
		skinDesc.Layout = skinningProgram.Layout.get();
		skinDesc.MaxSourceVertices = 1u << 18;
		skinDesc.MaxMorphDeltas = 1u << 16;
		skinDesc.MaxMeshes = 64;
		skinDesc.MaxInstances = 256;
		skinning = std::make_unique<R::SkinningSystem>(device, *geometry, skinDesc);
	}

	// Forward+, shadows, clustered lights and the environment builder.
	void FrameRenderer::Impl::CreateScenePasses()
	{
		const auto bindlessSpace = R::ForwardPlusBindlessSpace(desc.BindlessTextures, desc.BindlessSamplers);
		// --- Frame subsystems ------------------------------------------------------
		R::ForwardPlusRendererDesc forwardDesc;
		forwardDesc.Opaque = { forwardOpaquePipeline.get(), forwardOpaque.Layout.get() };
		forwardDesc.Transparent = { forwardTransparentPipeline.get(), forwardTransparent.Layout.get() };
		forwardDesc.DepthPrepass = { forwardDepthPipeline.get(), forwardDepth.Layout.get() };
		forwardDesc.OpaquePrepassed = { forwardPrepassedPipeline.get(), forwardPrepassed.Layout.get() };

		if (shaders.Contains("ForwardBackDepth"))
		{
			forwardBackDepth = LoadDrawProgram("ForwardBackDepth", &bindlessSpace);
			forwardBackDepthPipeline = Require(device.CreateGraphicsPipeline(R::ForwardPlusRenderer::DepthPrepassPipelineDesc(
												   *forwardBackDepth.Program, *forwardBackDepth.Layout)),
				"Forward+ back depth");
			forwardDesc.BackDepth = { forwardBackDepthPipeline.get(), forwardBackDepth.Layout.get() };
		}

		if (shaders.Contains("ForwardOpaqueDeferred") && shaders.Contains("ForwardLocalLights"))
		{
			forwardDeferred = LoadDrawProgram("ForwardOpaqueDeferred", &bindlessSpace);
			forwardDeferredPipeline = Require(device.CreateGraphicsPipeline(R::ForwardPlusRenderer::DeferredPipelineDesc(
												  *forwardDeferred.Program, *forwardDeferred.Layout)),
				"Forward+ deferred opaque");
			forwardLocalLights = shaders.LoadCompute("ForwardLocalLights");
			forwardDesc.OpaqueDeferred = { forwardDeferredPipeline.get(), forwardDeferred.Layout.get() };
			forwardDesc.LocalLightsPipeline = forwardLocalLights.Pipeline.get();
			forwardDesc.LocalLightsLayout = forwardLocalLights.Layout.get();
		}

		forwardDesc.SortPipeline = sortProgram.Pipeline.get();
		forwardDesc.SortLayout = sortProgram.Layout.get();
		forwardDesc.DrawPath = drawPath;
		forward = std::make_unique<R::ForwardPlusRenderer>(device, forwardDesc);

		R::ShadowRendererDesc shadowDesc;
		shadowDesc.Opaque = { shadowDepthPipeline.get(), shadowDepth.Layout.get() };
		shadowDesc.Masked = { shadowMaskedPipeline.get(), shadowMasked.Layout.get() };

		if (shaders.Contains("ShadowClear"))
		{
			shadowClear = LoadDrawProgram("ShadowClear", nullptr);
			shadowClearPipeline =
				Require(device.CreateGraphicsPipeline(R::ShadowRenderer::ClearPipelineDesc(*shadowClear.Program, *shadowClear.Layout)),
					"shadow clear");
			shadowDesc.Clear = { shadowClearPipeline.get(), shadowClear.Layout.get() };
		}

		shadowDesc.DrawPath = drawPath;
		shadows = std::make_unique<R::ShadowRenderer>(shadowDesc);

		R::ClusteredLightAssignerDesc clusterDesc;
		clusterDesc.Cull = { clusterCull.Pipeline.get(), clusterCull.Layout.get(), clusterCull.Space };
		clusterDesc.Bounds = { clusterBounds.Pipeline.get(), clusterBounds.Layout.get(), clusterBounds.Space };
		clusterDesc.Assign = { clusterAssign.Pipeline.get(), clusterAssign.Layout.get(), clusterAssign.Space };
		clusterDesc.Scan = { clusterScan.Pipeline.get(), clusterScan.Layout.get(), clusterScan.Space };
		clusters = std::make_unique<R::ClusteredLightAssigner>(clusterDesc);

		R::EnvironmentBuilderDesc environmentDesc;
		environmentDesc.Sky = { environmentSky.Pipeline.get(), environmentSky.Layout.get(), environmentSky.Space };
		environmentDesc.Downsample = { environmentDownsample.Pipeline.get(), environmentDownsample.Layout.get(),
			environmentDownsample.Space };
		environmentDesc.Prefilter = { environmentPrefilter.Pipeline.get(), environmentPrefilter.Layout.get(), environmentPrefilter.Space };
		environmentDesc.Irradiance = { environmentIrradiance.Pipeline.get(), environmentIrradiance.Layout.get(),
			environmentIrradiance.Space };
		environmentDesc.BrdfLut = { environmentLut.Pipeline.get(), environmentLut.Layout.get(), environmentLut.Space };
		environmentDesc.Sampler = linearClamp.get();

		if (shaders.Contains("EnvironmentOverlay"))
		{
			environmentOverlay = shaders.LoadCompute("EnvironmentOverlay");
			environmentDesc.Overlay = { environmentOverlay.Pipeline.get(), environmentOverlay.Layout.get(), environmentOverlay.Space };
		}

		environmentBuilder = std::make_unique<R::EnvironmentBuilder>(environmentDesc);
	}

	// Screen-space effects, reflections, TAA, post, UI and the material router.
	void FrameRenderer::Impl::CreateEffects()
	{
		R::ScreenSpaceEffectsDesc ssDesc;
		ssDesc.AmbientOcclusion = { ssAo.Pipeline.get(), ssAo.Layout.get(), ssAo.Space };
		ssDesc.Blur = { ssBlur.Pipeline.get(), ssBlur.Layout.get(), ssBlur.Space };
		ssDesc.Composite = { ssComposite.Pipeline.get(), ssComposite.Layout.get(), ssComposite.Space };

		if (ssCompositeHalf.Pipeline)
		{
			ssDesc.CompositeHalf = { ssCompositeHalf.Pipeline.get(), ssCompositeHalf.Layout.get(), ssCompositeHalf.Space };
		}

		if (ssLinearDepth.Pipeline)
		{
			ssDesc.LinearDepth = { ssLinearDepth.Pipeline.get(), ssLinearDepth.Layout.get(), ssLinearDepth.Space };
		}

		if (ssReflectionTemporal.Pipeline)
		{
			ssDesc.ReflectionTemporal = { ssReflectionTemporal.Pipeline.get(), ssReflectionTemporal.Layout.get(),
				ssReflectionTemporal.Space };
		}

		ssDesc.Reflection = { ssReflection.Pipeline.get(), ssReflection.Layout.get(), ssReflection.Space };
		ssDesc.ProbeSampler = linearClamp.get();
		screenSpace = std::make_unique<R::ScreenSpaceEffects>(ssDesc);

		if (shaders.Contains("ReflectionProbeResolve") && shaders.Contains("ReflectionProbePrefilter"))
		{
			probeResolve = shaders.LoadCompute("ReflectionProbeResolve");
			probePrefilter = shaders.LoadCompute("ReflectionProbePrefilter");
			R::ReflectionProbeRendererDesc probeDesc;
			probeDesc.Resolve = { probeResolve.Pipeline.get(), probeResolve.Layout.get(), probeResolve.Space };
			probeDesc.Downsample = { environmentDownsample.Pipeline.get(), environmentDownsample.Layout.get(),
				environmentDownsample.Space };
			probeDesc.Prefilter = { probePrefilter.Pipeline.get(), probePrefilter.Layout.get(), probePrefilter.Space };
			probeDesc.Sampler = linearClamp.get();
			probeRenderer = std::make_unique<R::ReflectionProbeRenderer>(device, probeDesc);
		}

		if (shaders.Contains("PlanarReflectionResolve"))
		{
			planarResolve = shaders.LoadCompute("PlanarReflectionResolve");
			R::PlanarReflectionRendererDesc planarDesc;
			planarDesc.Resolve = { planarResolve.Pipeline.get(), planarResolve.Layout.get(), planarResolve.Space };
			planarDesc.Sampler = linearClamp.get();
			planarRenderer = std::make_unique<R::PlanarReflectionRenderer>(device, planarDesc);
		}

		temporal = std::make_unique<R::TemporalAntiAliasing>(device,
			R::TemporalAntiAliasingDesc{ { temporalResolve.Pipeline.get(), temporalResolve.Layout.get(), temporalResolve.Space }, "TAA" });

		R::PostProcessorDesc postDesc;
		postDesc.Histogram = { postHistogram.Pipeline.get(), postHistogram.Layout.get(), postHistogram.Space };
		postDesc.Exposure = { postExposure.Pipeline.get(), postExposure.Layout.get(), postExposure.Space };
		postDesc.BloomDownsample = { postBloomDown.Pipeline.get(), postBloomDown.Layout.get(), postBloomDown.Space };
		postDesc.BloomUpsample = { postBloomUp.Pipeline.get(), postBloomUp.Layout.get(), postBloomUp.Space };
		postDesc.Composite = { postComposite.Pipeline.get(), postComposite.Layout.get(), postComposite.Space };
		postDesc.CompositeHdr = { postCompositeHdr.Pipeline.get(), postCompositeHdr.Layout.get(), postCompositeHdr.Space };
		post = std::make_unique<R::PostProcessor>(device, postDesc);

		ui = std::make_unique<R::UiRenderer>();
		atlasTextures = std::make_unique<R::UiAtlasTextures>(device, *bindless);
		viewports = std::make_unique<SceneViewports>(device, *bindless);
		viewports->SetAssetResolver({ [library = meshes.get()](MeshLibrary::MeshHandle mesh)
			{
				return library->Resolve(mesh);
			},
			[library = meshes.get()](MeshLibrary::TextureHandle texture)
			{
				return library->Resolve(texture);
			} });

		materials->SetRouter(
			[this](std::uint32_t set, const R::StandardPbr::Parameters& parameters)
			{
				Route(set, parameters);
			});
	}

	FrameRenderer::Impl::~Impl()
	{
		// Every GPU object must be idle before its owner goes.
		try
		{
			renderDevice.WaitIdle();
		}
		catch (...)
		{
		}

		if (atlasTextures)
		{
			atlasTextures->Drain();
		}

		if (viewports)
		{
			viewports->Drain();
		}
	}

	FrameRenderer::Impl::Held& FrameRenderer::Impl::HeldFrame(std::uint32_t slot, std::uint32_t width, std::uint32_t height)
	{
		if (heldFrames.size() <= slot)
		{
			heldFrames.resize(slot + 1);
		}

		auto& held = heldFrames[slot];

		if (!held.Texture || held.Texture->GetDesc().Extent.Width != width || held.Texture->GetDesc().Extent.Height != height)
		{
			if (held.Texture)
			{
				renderDevice.WaitIdle(); // A resize: the old copy may still be read.
			}

			S::TextureDesc desc;
			desc.Extent = { width, height, 1 };
			desc.PixelFormat = S::Format::RGBA8Unorm;
			desc.Usage = S::TextureUsage::Sampled | S::TextureUsage::ColorAttachment;
			desc.DebugName = "Held frame";
			held.Texture = device.CreateTexture(desc);
			held.State = ResourceState::Undefined;

			if (!held.Texture)
			{
				throw std::runtime_error("FrameRenderer: cannot create the held frame");
			}
		}

		return held;
	}

	S::GraphicsPipeline& FrameRenderer::Impl::GetPresentPipeline(S::Format format)
	{
		auto& pipeline = presentPipelines[format];

		if (!pipeline)
		{
			S::GraphicsPipelineDesc presentDesc{};
			presentDesc.Program = present.Program.get();
			presentDesc.Layout = present.Layout.get();
			presentDesc.ColorFormats = { &format, 1 };
			presentDesc.DepthStencil.DepthTest = false;
			presentDesc.DepthStencil.DepthWrite = false;
			presentDesc.Raster.Cull = S::CullMode::None;
			presentDesc.DebugName = "Present";
			pipeline = device.CreateGraphicsPipeline(presentDesc);

			if (!pipeline)
			{
				throw std::runtime_error("FrameRenderer: cannot create the present pipeline");
			}
		}

		return *pipeline;
	}

	void FrameRenderer::Impl::Route(std::uint32_t set, const R::StandardPbr::Parameters& parameters)
	{
		routes[set] = parameters;

		if (visibility)
		{
			R::ForwardPlusRenderer::RouteMaterial(*visibility, set, parameters);
		}

		if (shadowVisibility)
		{
			R::ShadowRenderer::RouteMaterial(*shadowVisibility, set, parameters);
		}

		if (probeVisibility)
		{
			R::ForwardPlusRenderer::RouteMaterial(*probeVisibility, set, parameters);
		}

		if (planarVisibility)
		{
			R::ForwardPlusRenderer::RouteMaterial(*planarVisibility, set, parameters);
		}
	}

	void FrameRenderer::Impl::EnsureVisibility(std::uint32_t slots)
	{
		if (visibility && visibilitySlots == slots)
		{
			return;
		}

		R::GpuVisibilityDesc visibilityDesc;
		visibilityDesc.CullPipeline = visibilityProgram.Pipeline.get();
		visibilityDesc.Layout = visibilityProgram.Layout.get();
		visibilityDesc.Space = visibilityProgram.Space;
		visibilityDesc.MaxObjects = desc.MaxObjects;
		visibilityDesc.MaxMaterialSets = desc.MaxMaterials;
		visibilityDesc.MaterialBinCapacities = R::ForwardPlusRenderer::VisibilityBinCapacities(desc.MaxObjects, 4096);
		visibilityDesc.IndexPageSlots = slots;
		visibilityDesc.DebugName = "Main visibility";
		visibility = std::make_unique<R::GpuVisibility>(device, visibilityDesc);
		visibilityDesc.DebugName = "Probe visibility";
		probeVisibility = std::make_unique<R::GpuVisibility>(device, visibilityDesc);
		visibilityDesc.DebugName = "Planar visibility";
		planarVisibility = std::make_unique<R::GpuVisibility>(device, visibilityDesc);
		visibilityDesc.MaterialBinCapacities = R::ShadowRenderer::VisibilityBinCapacities(desc.MaxObjects, 4096);
		visibilityDesc.DebugName = "Shadow visibility";
		shadowVisibility = std::make_unique<R::GpuVisibility>(device, visibilityDesc);
		visibilitySlots = slots;

		for (const auto& [set, parameters] : routes)
		{
			R::ForwardPlusRenderer::RouteMaterial(*visibility, set, parameters);
			R::ForwardPlusRenderer::RouteMaterial(*probeVisibility, set, parameters);
			R::ForwardPlusRenderer::RouteMaterial(*planarVisibility, set, parameters);
			R::ShadowRenderer::RouteMaterial(*shadowVisibility, set, parameters);
		}
	}

	void FrameRenderer::Impl::EnsureEnvironmentTargets(std::uint32_t resolution)
	{
		if (prefiltered && environmentMap.SourceSize == resolution)
		{
			return;
		}

		environmentMap = {};
		environmentMap.SourceSize = resolution;
		environmentMap.PrefilteredSize = std::max(resolution / 2, 16u);
		environmentMap.PrefilteredMipCount = 5;
		environmentMap.PrefilterSampleCount = 64;
		environmentMap.IrradianceFaceSize = std::min(16u, resolution);
		R::EnvironmentBuilder::Validate(environmentMap);
		prefiltered = device.CreateTexture(R::EnvironmentBuilder::PrefilteredCubeDesc(environmentMap));
		irradiance = device.CreateBuffer(R::EnvironmentBuilder::IrradianceBufferDesc());

		if (!prefiltered || !irradiance)
		{
			throw std::runtime_error("FrameRenderer: cannot create the environment maps");
		}

		builtSky.reset();
		environmentValid = false;
	}

	// ----------------------------------------------------------------------------------

	FrameRenderer::FrameRenderer(RenderDevice& deviceValue, Swim::Assets::AssetSystem& assets, Swim::IO::AsyncIoService& io,
		Swim::Jobs::JobSystem* jobs, const FrameRendererDesc& desc)
		: device(deviceValue), impl(std::make_unique<Impl>(deviceValue, assets, io, jobs, desc))
	{
		settings.Sanitize();
	}

	FrameRenderer::~FrameRenderer()
	{
		impl.reset();
	}

	MeshLibrary& FrameRenderer::GetMeshes() const
	{
		return *impl->meshes;
	}

	MaterialLibrary& FrameRenderer::GetMaterials() const
	{
		return *impl->materials;
	}

	R::GpuScene& FrameRenderer::GetScene() const
	{
		return *impl->scene;
	}

	R::GpuLightBuffer& FrameRenderer::GetLights() const
	{
		return *impl->lights;
	}

	R::ParticleSystem& FrameRenderer::GetParticles() const
	{
		return *impl->particles;
	}

	R::SkinningSystem& FrameRenderer::GetSkinning() const
	{
		return *impl->skinning;
	}

	R::BindlessResourceTable& FrameRenderer::GetBindless() const
	{
		return *impl->bindless;
	}

	void FrameRenderer::SetGlintTexture(Swim::Rhi::TextureView* view)
	{
		auto& I = *impl;

		if (I.glintTexture)
		{
			I.bindless->Release(*I.glintTexture, lastCompletion);
			I.glintTexture.reset();
		}

		I.glintAsset = {};

		if (view)
		{
			I.glintTexture = I.bindless->TryRegisterTexture(*view);
		}
	}

	void FrameRenderer::SetGlintTexture(Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> texture)
	{
		SetGlintTexture(static_cast<Swim::Rhi::TextureView*>(nullptr));

		if (texture.IsValid())
		{
			impl->glintAsset = texture;
			impl->residency->MakeUrgent(texture);
		}
	}

	R::AssetResidencyService& FrameRenderer::GetResidency() const
	{
		return *impl->residency;
	}

	R::GeometryHeap& FrameRenderer::GetGeometry() const
	{
		return *impl->geometry;
	}

	SceneViewports& FrameRenderer::GetViewports() const
	{
		return *impl->viewports;
	}

} // namespace Engine
