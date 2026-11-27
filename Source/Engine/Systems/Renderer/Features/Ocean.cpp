#include "Engine/Systems/Renderer/Features/Ocean.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <span>

namespace Engine
{

	namespace
	{

		using Float4 = std::array<float, 4>;
		constexpr float Pi = 3.14159265358979f;
		// The spectrum's angular frequencies are multiples of 2 pi / RepeatSeconds, so the
		// field repeats exactly and time can wrap without a jump or a precision loss.
		constexpr double RepeatSeconds = 256.0;
		// Frames a readback may stay unanswered before its buffer is reused (a frame that was
		// never submitted); far more than the frames in flight.
		constexpr std::uint32_t ReadbackTimeoutFrames = 60;
		constexpr std::uint32_t SampledFrames = 120;

		struct SimulationConstants
		{
			Float4 Wind, Spectrum, Cascade, Foam;
			std::uint32_t Size[4];
		};

		static_assert(sizeof(SimulationConstants) == 80);

		struct SurfaceParams
		{
			Float4 Grid, GridShape, InverseSizes, Weights, DeepColor, ScatterColor, FoamColor, SunDirection, SunColor, Ambient;
			Float4 LodFade0, LodFade1, LodFade2, Far;
			GpuDistanceFog Fog;
		};

		float InverseRange(float start, float end)
		{
			return end > start ? 1.0f / (end - start) : 1.0e9f;
		}

		constexpr const char* DisplacementNames[3] = { "Displacement0", "Displacement1", "Displacement2" };
		constexpr const char* DerivativeNames[3] = { "Derivatives0", "Derivatives1", "Derivatives2" };
		constexpr const char* PreviousNames[3] = { "PreviousDisplacement0", "PreviousDisplacement1", "PreviousDisplacement2" };

		Float4 Vec(const std::array<float, 3>& v, float w)
		{
			return { v[0], v[1], v[2], w };
		}

		void AddUnique(std::vector<Swim::Render::GraphTexture>& list, Swim::Render::GraphTexture texture)
		{
			if (std::find(list.begin(), list.end(), texture) == list.end())
			{
				list.push_back(texture);
			}
		}

	} // namespace

	const OceanMaps& OceanSimulation::Update(RenderFeatureContext& context)
	{
		const auto& view = context.View();

		if (graph == context.Graph().GetId() && frame == view.Frame)
		{
			return maps;
		}

		graph = context.Graph().GetId();
		frame = view.Frame;
		maps = {};
		auto* device = context.Device();

		if (!device)
		{
			return maps;
		}

		++frameCount;

		if (sampled.exchange(false, std::memory_order_relaxed))
		{
			lastSampledFrame = frameCount;
			sampledRecently.store(true, std::memory_order_relaxed);
		}
		else if (frameCount - lastSampledFrame > SampledFrames)
		{
			sampledRecently.store(false, std::memory_order_relaxed);
		}

		CollectReadbacks(frameCount);

		const std::uint32_t n = std::clamp(std::bit_floor(std::max(Settings.Resolution, 1u)), 64u, 512u);
		const std::uint32_t count = std::clamp(Settings.Cascades, 1u, OceanMaps::MaxCascades);

		if (n != resolution || count != cascades || !history[0][0])
		{
			// Rare (a quality change): wait instead of tracking which frames still sample them.
			if (history[0][0])
			{
				device->WaitIdle();
			}

			Swim::Rhi::TextureDesc desc;
			desc.Extent = { n, n, 1 };
			desc.PixelFormat = Swim::Rhi::Format::RGBA16Float;
			// TransferSource: the CPU height readback copies it.
			desc.Usage = Swim::Rhi::TextureUsage::Sampled | Swim::Rhi::TextureUsage::Storage | Swim::Rhi::TextureUsage::TransferSource;
			desc.DebugName = "Ocean displacement";

			for (std::uint32_t c = 0; c < OceanMaps::MaxCascades; ++c)
			{
				for (auto& texture : history[c])
				{
					texture = c < count ? device->CreateTexture(desc) : nullptr;
				}
			}

			for (auto& readback : readbacks)
			{
				readback = {};
			}

			field.Clear();
			written = {};
			historyValid = false;
			latest = 0;
			resolution = n;
			cascades = count;
		}

		context.Graph().OnAbandoned(
			[this, w = written, l = latest, v = historyValid]
			{
				written = w;
				latest = l;
				historyValid = v;
			});
		const float dt = Settings.Paused ? 0.0f : view.DeltaTime * std::max(Settings.TimeScale, 0.0f);
		time = std::fmod(time + dt, RepeatSeconds);
		currentTime.store(time, std::memory_order_relaxed);
		const float direction = Settings.WindDirectionDegrees * Pi / 180.0f;
		const std::uint32_t target = historyValid ? 1u - latest : latest;

		SimulationConstants constants{};
		constants.Wind = { std::cos(direction), std::sin(direction), std::max(Settings.WindSpeed, 0.1f), static_cast<float>(time) };
		constants.Spectrum = { std::max(Settings.Amplitude, 0.0f), std::max(Settings.Fetch, 100.0f),
			2.0f + 30.0f * std::clamp(Settings.Spreading, 0.0f, 1.0f), static_cast<float>(2.0 * Pi / RepeatSeconds) };
		constants.Foam = { std::max(Settings.Choppiness, 0.0f), Settings.FoamThreshold, std::max(Settings.FoamInjection, 0.0f),
			std::exp(-std::max(Settings.FoamDecay, 0.0f) * dt) };
		constants.Size[0] = n;

		for (std::uint32_t c = 0; c < count; ++c)
		{
			const float size = std::max(Settings.TileSizes[c], 0.5f);
			// Each cascade keeps the wave numbers between its neighbours' cut-offs: waves
			// shorter than a sixth of the next tile belong to the next cascade.
			const float low = c == 0 ? 1.0e-4f : 12.0f * Pi / size;
			const float high = c + 1 < count ? 12.0f * Pi / std::max(Settings.TileSizes[c + 1], 0.5f) : 1.0e9f;
			auto cascade = constants;
			cascade.Spectrum[0] *= c == 0 ? 1.0f : std::clamp(Settings.Detail, 0.0f, 2.0f);
			cascade.Cascade = { size, low, high, std::max(Settings.SmallWaveCutoff, 0.0f) };
			cascade.Size[1] = 0x51u + c * 0x3Du;
			cascade.Size[3] = historyValid ? 1u : 0u;

			const auto spectrumA = context.CreateTexture(Swim::Rhi::Format::RGBA32Float, n, n, "Ocean spectrum A");
			const auto spectrumB = context.CreateTexture(Swim::Rhi::Format::RGBA32Float, n, n, "Ocean spectrum B");
			const auto rowsA = context.CreateTexture(Swim::Rhi::Format::RGBA32Float, n, n, "Ocean rows A");
			const auto rowsB = context.CreateTexture(Swim::Rhi::Format::RGBA32Float, n, n, "Ocean rows B");
			context.Compute("OceanSpectrum")
				.Storage("SpectrumA", spectrumA)
				.Storage("SpectrumB", spectrumB)
				.Constants(cascade)
				.Dispatch(n, n);

			for (std::uint32_t pass = 0; pass < 2; ++pass)
			{
				cascade.Size[2] = pass;
				const auto inA = pass == 0 ? spectrumA : rowsA;
				const auto inB = pass == 0 ? spectrumB : rowsB;
				const auto outA = pass == 0 ? rowsA : spectrumA;
				const auto outB = pass == 0 ? rowsB : spectrumB;
				context.Compute("OceanFft").Texture("Input", inA).Storage("Output", outA).Constants(cascade).Dispatch(256, n);
				context.Compute("OceanFft").Texture("Input", inB).Storage("Output", outB).Constants(cascade).Dispatch(256, n);
			}

			auto& graphRef = context.Graph();
			const auto output = graphRef.ImportTexture(
				*history[c][target], written[c][target] ? Swim::Rhi::ResourceState::ShaderRead : Swim::Rhi::ResourceState::Undefined);
			// Without history the shader ignores PreviousDisplacement, but it must still bind a
			// written texture other than its own inputs and outputs.
			const auto previous = historyValid ? graphRef.ImportTexture(*history[c][latest], Swim::Rhi::ResourceState::ShaderRead) : rowsA;
			const auto derivatives = context.CreateTexture(Swim::Rhi::Format::RGBA16Float, n, n, "Ocean derivatives");
			context.Compute("OceanAssemble")
				.Texture("FieldsA", spectrumA)
				.Texture("FieldsB", spectrumB)
				.Texture("PreviousDisplacement", previous)
				.Storage("Displacement", output)
				.Storage("Derivatives", derivatives)
				.Constants(cascade)
				.Dispatch(n, n);
			// Exported only once the pass that writes it is recorded, so a failure above never
			// leaves an export of unwritten contents in the frame.
			graphRef.Export(output, Swim::Rhi::ResourceState::ShaderRead);
			written[c][target] = true;
			maps.Displacement[c] = output;
			maps.Derivatives[c] = derivatives;
			maps.PreviousDisplacement[c] = historyValid ? previous : output;
			maps.TileSizes[c] = size;
			maps.Weights[c] = 1.0f;
		}

		for (std::uint32_t c = count; c < OceanMaps::MaxCascades; ++c)
		{
			maps.Displacement[c] = maps.Displacement[0];
			maps.Derivatives[c] = maps.Derivatives[0];
			maps.PreviousDisplacement[c] = maps.PreviousDisplacement[0];
			maps.TileSizes[c] = maps.TileSizes[0];
			maps.Weights[c] = 0.0f;
		}

		maps.HasPrevious = historyValid;
		maps.Resolution = n;
		maps.Valid = true;
		latest = target;
		historyValid = true;

		if (sampledRecently.load(std::memory_order_relaxed) && frameCount - lastReadbackFrame >= std::max(Settings.ReadbackInterval, 1u))
		{
			RequestReadback(context, frameCount);
		}

		return maps;
	}

	void OceanSimulation::CollectReadbacks(std::uint32_t frameNumber)
	{
		Readback* newest = nullptr;

		for (auto& readback : readbacks)
		{
			if (!readback.Pending)
			{
				continue;
			}

			// The stamp is written by a pass ordered after the copies, so it arrives last.
			const std::uint64_t dataSize = std::uint64_t(readback.Cascades) * readback.Resolution * readback.Resolution * 8;
			readback.Buffer->InvalidateMappedReads(dataSize, sizeof(std::uint64_t));
			const auto mapped = readback.Buffer->GetMappedReadSpan();
			std::uint64_t stamp = 0;
			std::memcpy(&stamp, mapped.data() + dataSize, sizeof(stamp));

			if (stamp == readback.Id)
			{
				if (!newest || readback.Id > newest->Id)
				{
					if (newest)
					{
						newest->Pending = false;
					}

					newest = &readback;
				}
				else
				{
					readback.Pending = false;
				}
			}
			else if (frameNumber - readback.Frame > ReadbackTimeoutFrames)
			{
				readback.Pending = false;
			}
		}

		if (!newest)
		{
			return;
		}

		const std::uint64_t dataSize = std::uint64_t(newest->Cascades) * newest->Resolution * newest->Resolution * 8;
		newest->Buffer->InvalidateMappedReads(0, dataSize);
		const auto mapped = newest->Buffer->GetMappedReadSpan();
		const std::uint32_t texels = newest->Resolution * newest->Resolution;
		std::uint32_t step = std::clamp(std::bit_floor(std::max(Settings.ReadbackStep, 1u)), 1u, 4u);
		step = std::min(step, newest->Resolution);
		std::vector<OceanFieldCascade> cascadesOut;

		for (std::uint32_t c = 0; c < newest->Cascades; ++c)
		{
			std::vector<std::uint16_t> halves(std::size_t(texels) * 4);
			std::memcpy(halves.data(), mapped.data() + std::size_t(c) * texels * 8, halves.size() * sizeof(std::uint16_t));
			cascadesOut.push_back(OceanFieldCascade::FromHalfFloats(halves, newest->Resolution, step, newest->TileSizes[c]));
		}

		field.Publish(std::move(cascadesOut), newest->Time, RepeatSeconds);
		newest->Pending = false;
	}

	void OceanSimulation::RequestReadback(RenderFeatureContext& context, std::uint32_t frameNumber)
	{
		auto* device = context.Device();
		const auto free = std::find_if(readbacks.begin(), readbacks.end(),
			[](const Readback& r)
			{
				return !r.Pending;
			});

		if (!device || free == readbacks.end())
		{
			return;
		}

		const std::uint32_t count = std::min(std::clamp(Settings.ReadbackCascades, 1u, OceanMaps::MaxCascades), cascades);
		const std::uint32_t n = resolution;
		const std::uint64_t dataSize = std::uint64_t(count) * n * n * 8;
		const std::uint64_t size = dataSize + sizeof(std::uint64_t);

		if (!free->Buffer || free->Buffer->GetDesc().Size != size)
		{
			free->Buffer = device->CreateBuffer({ size, Swim::Rhi::BufferUsage::TransferDestination, Swim::Rhi::MemoryPreference::GpuToCpu,
				"Ocean height readback", true });

			if (!free->Buffer || free->Buffer->GetMappedReadSpan().size() < size)
			{
				free->Buffer.reset();
				return;
			}
		}

		using S = Swim::Rhi::ResourceState;
		auto& graphRef = context.Graph();
		const auto buffer = graphRef.ImportBuffer(*free->Buffer, S::HostRead);
		std::array<Swim::Render::GraphTexture, OceanMaps::MaxCascades> sources{};

		for (std::uint32_t c = 0; c < count; ++c)
		{
			sources[c] = maps.Displacement[c];
		}

		graphRef.AddPass(
			"Ocean height readback", Swim::Rhi::QueueType::Transfer,
			[&](Swim::Render::RenderGraphBuilder& b)
			{
				for (std::uint32_t c = 0; c < count; ++c)
				{
					b.Read(sources[c], S::CopySource);
				}

				b.ReadWrite(buffer, S::CopyDestination);
			},
			[=](Swim::Render::RenderCommandContext& c)
			{
				auto& target = c.Get(buffer);

				for (std::uint32_t i = 0; i < count; ++i)
				{
					c.Commands().CopyTextureToBuffer(c.Get(sources[i]), target, { std::uint64_t(i) * n * n * 8, {}, {}, { n, n, 1 } });
				}
			});
		const std::uint64_t id = nextReadbackId++;
		Swim::Render::AddBufferUpload(graphRef, "Ocean height readback stamp", std::as_bytes(std::span(&id, 1)), buffer, dataSize);
		graphRef.Export(buffer, S::HostRead);

		free->Id = id;
		free->Frame = frameNumber;
		free->Cascades = count;
		free->Resolution = n;
		free->TileSizes = maps.TileSizes;
		free->Time = time;
		free->Pending = true;
		lastReadbackFrame = frameNumber;
	}

	FluidSample OceanSimulation::SampleSurface(float x, float z, float seaLevel) const
	{
		sampled.store(true, std::memory_order_relaxed);
		return field.Sample(x, z, seaLevel, currentTime.load(std::memory_order_relaxed));
	}

	FluidSurfaceFunction MakeOceanFluidSurface(std::shared_ptr<const OceanSimulation> simulation, float seaLevel)
	{
		return [simulation = std::move(simulation), seaLevel](float x, float z, double)
		{
			return simulation->SampleSurface(x, z, seaLevel);
		};
	}

	namespace
	{

		// Whether any of the square [centre +- half] at height y can be in the view (the corners
		// are not all beyond one side of the clip volume). Row-major view-projection.
		bool SquareInView(const std::array<float, 16>& m, float x, float z, float half, float y)
		{
			std::array<std::array<float, 4>, 4> clip{};
			const std::array<std::array<float, 2>, 4> corners{ { { -half, -half }, { half, -half }, { half, half }, { -half, half } } };

			for (std::size_t i = 0; i < 4; ++i)
			{
				const float p[4] = { x + corners[i][0], y, z + corners[i][1], 1.0f };

				for (std::size_t r = 0; r < 4; ++r)
				{
					clip[i][r] = m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3] * p[3];
				}
			}

			const auto allOutside = [&](auto&& outside)
			{
				return std::all_of(clip.begin(), clip.end(), outside);
			};

			return !(allOutside(
						 [](const auto& c)
						 {
							 return c[0] < -c[3];
						 }) ||
				allOutside(
					[](const auto& c)
					{
						return c[0] > c[3];
					}) ||
				allOutside(
					[](const auto& c)
					{
						return c[1] < -c[3];
					}) ||
				allOutside(
					[](const auto& c)
					{
						return c[1] > c[3];
					}) ||
				allOutside(
					[](const auto& c)
					{
						return c[3] <= 0.0f;
					}));
		}

	} // namespace

	std::uint32_t OceanSurface::GetGeometryPasses() const
	{
		return RenderGeometryPassBit(RenderGeometryPass::Opaque);
	}

	RenderGeometryResources OceanSurface::PrepareGeometry(RenderFeatureContext& context)
	{
		maps = {};
		const auto& view = context.View();
		const float reach =
			Settings.Lod.Enabled && Settings.Lod.FarField ? std::max(Settings.Extent, Settings.Lod.FarFieldExtent) : Settings.Extent;

		if (!Simulation)
		{
			return {};
		}

		// Out of view (looking up, or the sea is elsewhere): no draw, and no simulation unless
		// something samples its heights on the CPU.
		if (!SquareInView(view.ViewProjection, view.Position[0], view.Position[2], reach, Settings.Height))
		{
			if (Simulation->IsSampled())
			{
				Simulation->Update(context);
			}

			return {};
		}

		maps = Simulation->Update(context);

		if (!maps.Valid)
		{
			return {};
		}

		quads = std::clamp(Settings.GridResolution, 16u, 1024u);
		const float extent = std::max(Settings.Extent, 1.0f);
		const float spacing = std::max(Settings.NearSpacing, 0.01f);
		// offset(u) = scale * (exp(rate * |u|) - 1): the spacing grows in proportion to the
		// distance, starting at NearSpacing and reaching Extent at the edge.
		const float scale = spacing * float(quads) * 0.5f;
		float low = 1.0e-3f;
		float high = 30.0f;

		for (int i = 0; i < 40; ++i)
		{
			const float rate = 0.5f * (low + high);
			(scale * std::expm1(rate) / rate < extent ? low : high) = rate;
		}

		const float rate = 0.5f * (low + high);

		SurfaceParams p{};
		// The grid follows the camera without snapping: vertices slide over a surface sampled
		// only as finely as their spacing resolves (OceanGridWeights), so it never pops.
		p.Grid = { view.Position[0], view.Position[2], Settings.Height, rate };
		p.GridShape = { float(quads), scale / rate, 2.0f * view.TanHalfFovY / float(std::max(view.Height, 1u)), Simulation->GetTime() };

		for (std::uint32_t c = 0; c < OceanMaps::MaxCascades; ++c)
		{
			p.InverseSizes[c] = 1.0f / maps.TileSizes[c];
			p.Weights[c] = maps.Weights[c];
		}

		p.Weights[3] = float(maps.Resolution);
		p.DeepColor = Vec(Settings.DeepColor, Settings.Roughness);
		p.ScatterColor = Vec(Settings.ScatterColor, Settings.SunScatter);
		p.FoamColor = Vec(Settings.FoamColor, Settings.Foam);
		p.SunDirection = Vec(view.SunDirection, Settings.AmbientScatter);
		p.SunColor = Vec(view.SunColor, Settings.CrestScatterHeight);
		DistanceFogSettings fog;
		fog.Enabled = Settings.FogShape != FogCurve::Exponential || Settings.FogDensity > 0.0f;
		fog.Curve = Settings.FogShape;
		fog.Color = Settings.FogColor;
		fog.StartDistance = Settings.FogStart;
		fog.EndDistance = Settings.FogEnd;
		fog.Density = Settings.FogDensity;
		fog.Exponent = Settings.FogExponent;
		fog.MaxOpacity = Settings.FogMaxOpacity;
		p.Fog = PackDistanceFog(fog);
		p.Ambient = Vec(Settings.AmbientColor, maps.HasPrevious ? 1.0f : 0.0f);
		const auto& lod = Settings.Lod;
		ValidateWaterLodSettings(lod);
		const bool farField = lod.Enabled && lod.FarField && lod.FarFieldExtent > extent;
		p.LodFade0 = { lod.DetailStart, InverseRange(lod.DetailStart, lod.DetailEnd), lod.AmplitudeStart,
			InverseRange(lod.AmplitudeStart, lod.AmplitudeEnd) };
		p.LodFade1 = { lod.FarAmplitude, lod.NormalStart, InverseRange(lod.NormalStart, lod.NormalEnd), lod.FarRoughness };
		p.LodFade2 = { 0.0f, 1.0e9f, lod.Enabled ? 1.0f : 0.0f, lod.Debug ? 1.0f : 0.0f };
		p.Far = { farField ? lod.FarFieldExtent : 0.0f, extent, 0.0f, 0.0f };
		farVertices = farField ? 24u : 0u;
		params = context.Graph().CreateUpload(std::as_bytes(std::span(&p, 1)), "Ocean surface params", Swim::Rhi::BufferUsage::Storage, 16);

		std::vector<Swim::Render::GraphTexture> textures;

		for (std::uint32_t c = 0; c < OceanMaps::MaxCascades; ++c)
		{
			AddUnique(textures, maps.Displacement[c]);
			AddUnique(textures, maps.Derivatives[c]);
			AddUnique(textures, maps.PreviousDisplacement[c]);
		}

		return { { params }, {}, std::move(textures) };
	}

	void OceanSurface::DrawGeometry(RenderFeatureDrawContext& context)
	{
		if (!maps.Valid || context.Pass() != RenderGeometryPass::Opaque)
		{
			return;
		}

		auto draw = context.Program("OceanSurface");
		draw.Buffer("Params", params);

		for (std::uint32_t c = 0; c < OceanMaps::MaxCascades; ++c)
		{
			draw.Texture(DisplacementNames[c], maps.Displacement[c]);
			draw.Texture(DerivativeNames[c], maps.Derivatives[c]);
			draw.Texture(PreviousNames[c], maps.PreviousDisplacement[c]);
		}

		draw.Sampler("WaveSampler", "LinearRepeat");
		draw.Draw(quads * quads * 6 + farVertices);
	}

} // namespace Engine
