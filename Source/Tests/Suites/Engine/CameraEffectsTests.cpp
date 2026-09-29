#include "Engine/Systems/Renderer/Features/CameraEffects.h"
#include "Engine/Systems/Renderer/Features/GravitationalLensing.h"
#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"
#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"
#include "Tests/Fixtures/GpuSceneFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

	namespace R = Swim::Render;
	namespace S = Swim::Rhi;

	struct Slot
	{
		std::string Name;
		S::DescriptorType Type;
		S::Format Format = S::Format::Undefined;
	};

	// The mock device standing in for the runtime shader set: each program has the
	// bindings its Slang source declares (CameraLens.slang, DepthOfField.slang, FilmSensor.slang).
	struct CameraWorld
	{
		CameraWorld()
		{
			fixture.device.CreateTextures = true;
			const auto f16 = S::Format::RGBA16Float;
			const auto sampled = S::DescriptorType::SampledTexture;
			const auto storage = S::DescriptorType::StorageTexture;
			const auto sampler = S::DescriptorType::Sampler;
			Add("DepthOfFieldPrepare", { { "Color", sampled }, { "Depth", sampled }, { "LinearClamp", sampler }, { "Half", storage, f16 } });
			Add("DepthOfFieldGather", { { "Half", sampled }, { "LinearClamp", sampler }, { "Blurred", storage, f16 } });
			Add("DepthOfField", { { "Color", sampled }, { "Half", sampled }, { "Blurred", sampled }, { "LinearClamp", sampler },
									{ "Output", storage, f16 } });
			Add("CameraLensHalation", { { "Color", sampled }, { "LinearClamp", sampler }, { "Glow", storage, f16 } });
			Add("CameraLens",
				{ { "Color", sampled }, { "Glow", sampled }, { "LinearClamp", sampler }, { "Output", storage, f16 } });
			Add("FilmSensor", { { "Color", sampled }, { "Output", storage, S::Format::RGBA8Unorm } });
			S::SamplerDesc samplerDesc;
			linear = fixture.device.CreateSampler(samplerDesc);
			view.Width = 64;
			view.Height = 32;
			view.TanHalfFovX = 0.8f;
			view.TanHalfFovY = 0.4f;
			view.Projection[11] = 0.1f;
		}

		void Add(const std::string& name, const std::vector<Slot>& slots)
		{
			auto& program = programs[name];
			auto layout = std::make_unique<Swim::Testing::MockPipelineLayout>();
			S::DescriptorSchemaDesc space{ 0, {} };

			for (std::uint32_t i = 0; i < slots.size(); ++i)
			{
				S::DescriptorBindingDesc binding{ i, slots[i].Type, 1, S::ShaderStageMask::Compute };
				binding.StorageTextureFormat = slots[i].Format;
				space.Bindings.push_back(binding);
				program.Bindings.push_back({ slots[i].Name, 0, i, slots[i].Type, slots[i].Format });
			}

			layout->program.Interface.DescriptorSchemas = { space };
			layout->program.Interface.PushConstants = { { 0, 128, S::ShaderStageMask::Compute } };
			program.Layout = std::move(layout);
			program.Pipeline = std::make_unique<Swim::Testing::MockComputePipeline>();
			program.ThreadGroupSize = { 8, 8, 1 };
		}

		// Records `feature` at its stage over a scene colour of `format`; returns the
		// dispatched programs (in order) and their group counts.
		std::vector<std::pair<std::string, std::uint32_t>> Run(Engine::RenderFeature& feature, S::Format format, bool& replaced)
		{
			R::RenderGraph graph;
			S::TextureDesc desc;
			desc.Extent = { view.Width, view.Height, 1 };
			desc.PixelFormat = format;
			desc.Usage = S::TextureUsage::Sampled | S::TextureUsage::ColorAttachment;
			const auto color = graph.CreateTexture(desc);
			desc.PixelFormat = S::Format::D32Float;
			desc.Usage = S::TextureUsage::Sampled | S::TextureUsage::DepthStencilAttachment;
			const auto depth = graph.CreateTexture(desc);
			graph.AddPass(
				"Scene", S::QueueType::Graphics,
				[&](R::RenderGraphBuilder& b)
				{
					b.Write(color, S::ResourceState::ColorAttachment);
					b.Write(depth, S::ResourceState::DepthStencilWrite);
				},
				[](R::RenderCommandContext&)
				{
				});
			Engine::RenderFeatureContext::Services services;
			std::vector<std::string> loaded;
			services.LoadCompute = [&](std::string_view name) -> const Engine::RuntimeComputeProgram&
			{
				loaded.push_back(std::string(name));
				return programs.at(std::string(name));
			};
			services.GetSampler = [&](std::string_view) -> S::Sampler&
			{
				return *linear;
			};
			Engine::RenderFeatureContext context(graph, feature.GetStage(), view, settings, color, depth, services);
			feature.Record(context);
			replaced = context.Color() != color;
			graph.Export(context.Color(), S::ResourceState::ShaderRead);
			fixture.device.Commands->clear();
			fixture.executor->Execute(graph.Compile());
			fixture.executor->Wait();
			std::vector<std::pair<std::string, std::uint32_t>> dispatches;
			std::size_t next = 0;

			for (const auto& command : *fixture.device.Commands)
			{
				if (command.Kind == "Dispatch")
				{
					dispatches.push_back({ next < loaded.size() ? loaded[next] : std::string("?"), command.SourceOffset });
					++next;
				}
			}

			return dispatches;
		}

		Swim::Testing::GpuSceneFixture fixture;
		std::map<std::string, Engine::RuntimeComputeProgram> programs;
		std::unique_ptr<S::Sampler> linear;
		Engine::RenderFeatureView view;
		Engine::RenderSettings settings;
	};

} // namespace

SWIM_TEST("Engine.CameraEffects", "ThinLensCircleOfConfusion")
{
	// A 35 mm lens on a full-frame (24 mm) sensor has tan(half vertical fov) = 12 / 35.
	const float focal = Engine::DepthOfField::FocalLength(12.0f / 35.0f, 24.0f);
	SWIM_CHECK(std::abs(focal - 0.035f) < 1.0e-5f);
	// In focus: nothing; behind: positive; in front: negative.
	SWIM_CHECK(std::abs(Engine::DepthOfField::CircleOfConfusion(focal, 2.0f, 3.0f, 3.0f)) < 1.0e-9f);
	const float far = Engine::DepthOfField::CircleOfConfusion(focal, 2.0f, 3.0f, 10.0f);
	const float near = Engine::DepthOfField::CircleOfConfusion(focal, 2.0f, 3.0f, 1.0f);
	SWIM_CHECK(far > 0.0f && near < 0.0f);
	// Background at infinity: f^2 / (N (S - f)) - about 0.2 mm at f/2 focused at 3 m.
	const float infinity = Engine::DepthOfField::CircleOfConfusion(focal, 2.0f, 3.0f, 1.0e7f);
	SWIM_CHECK(std::abs(infinity - focal * focal / (2.0f * (3.0f - focal))) < 1.0e-6f);
	// Stopping down one stop (x sqrt 2) shrinks it by sqrt 2; halving N doubles it.
	SWIM_CHECK(std::abs(Engine::DepthOfField::CircleOfConfusion(focal, 1.0f, 3.0f, 10.0f) - 2.0f * far) < 1.0e-7f);
}

SWIM_TEST("Engine.CameraEffects", "LensGeometryFitsTheFrame")
{
	using L = Engine::CameraLens;
	// No distortion: the identity with no zoom.
	SWIM_CHECK(std::abs(L::SourceRadius(0.5f, 0.0f, 0.0f, 0.0f) - 0.5f) < 1.0e-6f);
	SWIM_CHECK(std::abs(L::FitZoom(0.0f, 0.0f, 0.0f) - 1.0f) < 1.0e-6f);
	// Barrel (k1 > 0): after the corner fit, mid-field points sample nearer the centre
	// (the image bulges); pincushion the other way. The corner always maps to the corner.
	for (const float k1 : { 0.1f, -0.1f })
	{
		const float zoom = L::FitZoom(k1, 0.0f, 0.0f);
		SWIM_CHECK(std::abs(L::SourceRadius(1.0f, k1, 0.0f, 0.0f) / zoom - 1.0f) < 1.0e-6f);
		const float mid = L::SourceRadius(0.5f, k1, 0.0f, 0.0f) / zoom;
		SWIM_CHECK(k1 > 0.0f ? mid < 0.5f : mid > 0.5f);
	}

	// A full fisheye magnifies the centre and keeps the corners.
	SWIM_CHECK(L::SourceRadius(0.2f, 0.0f, 0.0f, 1.0f) < 0.2f);
	SWIM_CHECK(std::abs(L::FitZoom(0.0f, 0.0f, 1.0f) - 1.0f) < 1.0e-5f);
	// Monotonic: no folding over the frame for the ranges the UI allows.
	for (const float k1 : { -0.3f, 0.3f })
	{
		float previous = 0.0f;

		for (int i = 1; i <= 100; ++i)
		{
			const float r = float(i) / 100.0f;
			const float source = L::SourceRadius(r, k1, 0.0f, 0.5f);
			SWIM_CHECK(source > previous);
			previous = source;
		}
	}
}

SWIM_TEST("Engine.CameraEffects", "PresetsArePhysicalSubtleAndDistinct")
{
	// Off: every effect disabled, neutral grading.
	const auto off = Engine::DeriveCameraLook(Engine::CameraPresetLook(Engine::CameraPreset::Off));
	SWIM_CHECK(!off.DepthOfFieldEnabled && !off.LensEnabled && !off.SensorEnabled);
	SWIM_CHECK(off.Temperature == 0.0f && off.Contrast == 1.0f && off.Saturation == 1.0f);

	std::set<std::string> names;

	for (std::uint32_t p = 1; p < Engine::CameraPresetCount; ++p)
	{
		const auto preset = static_cast<Engine::CameraPreset>(p);
		names.insert(std::string(Engine::CameraPresetName(preset)));
		const auto look = Engine::CameraPresetLook(preset);
		const auto s = Engine::DeriveCameraLook(look);
		SWIM_CHECK(s.LensEnabled && s.SensorEnabled);
		// Subtle: a fraction of a stop of corner falloff at the default field of view,
		// fringes under a pixel or two, grain a few display codes, gentle grading.
		SWIM_CHECK(s.Optics.Vignette > 0.0f && s.Optics.Vignette <= 0.8f);
		SWIM_CHECK(s.Optics.ChromaticAberration > 0.0f && s.Optics.ChromaticAberration <= 0.008f);
		SWIM_CHECK(s.Optics.Softness <= 3.5f);
		SWIM_CHECK(s.Film.Grain > 0.0f && s.Film.Grain <= 0.03f);
		SWIM_CHECK(std::abs(s.Temperature) <= 30.0f);
		SWIM_CHECK(s.Contrast >= 0.85f && s.Contrast <= 1.15f);
		SWIM_CHECK(s.Saturation >= 0.75f && s.Saturation <= 1.1f);
		// Film has halation, digital does not.
		SWIM_CHECK((s.Optics.Halation > 0.0f) == look.Film);
	}

	SWIM_CHECK_EQUAL(names.size(), std::size_t(Engine::CameraPresetCount - 1));

	// Derived, not tabled: older and faster lenses vignette and fringe more; grain grows with ISO.
	Engine::CameraLook look;
	look.Enabled = true;
	const auto modern = Engine::DeriveCameraLook(look);
	look.LensAge = 1.0f;
	look.FNumber = 1.4f;
	const auto old = Engine::DeriveCameraLook(look);
	SWIM_CHECK(old.Optics.Vignette > modern.Optics.Vignette);
	SWIM_CHECK(old.Optics.ChromaticAberration > modern.Optics.ChromaticAberration);
	look.Iso = 1600.0f;
	SWIM_CHECK(std::abs(Engine::DeriveCameraLook(look).Film.Grain - 4.0f * old.Film.Grain) < 1.0e-6f);
	// White balance: 6500 K is neutral; a higher setting renders daylight warmer, a lower one cooler.
	look.WhiteBalanceKelvin = 6500.0f;
	SWIM_CHECK(std::abs(Engine::DeriveCameraLook(look).Temperature) < 1.0e-3f);
	look.WhiteBalanceKelvin = 8000.0f;
	SWIM_CHECK(Engine::DeriveCameraLook(look).Temperature > 5.0f);
	look.WhiteBalanceKelvin = 5000.0f;
	SWIM_CHECK(Engine::DeriveCameraLook(look).Temperature < -5.0f);

	// Applying sets the features and the grading, leaving every value editable afterwards.
	Engine::DepthOfField dof;
	Engine::CameraLens lens;
	Engine::FilmSensor sensor;
	R::ColorGradingSettings grading;
	const auto cinematic = Engine::DeriveCameraLook(Engine::CameraPresetLook(Engine::CameraPreset::Cinematic35mm));
	Engine::ApplyCameraLook(cinematic, &dof, &lens, &sensor, grading);
	SWIM_CHECK(dof.Enabled && lens.Enabled && sensor.Enabled);
	SWIM_CHECK(lens.Settings.Halation == cinematic.Optics.Halation && grading.Contrast == cinematic.Contrast);
	R::ValidatePostProcessSettings([&]
		{
			R::PostProcessSettings post;
			post.Grading = grading;
			return post;
		}());
	Engine::ApplyCameraLook(off, &dof, &lens, &sensor, grading);
	SWIM_CHECK(!dof.Enabled && !lens.Enabled && !sensor.Enabled && grading.Temperature == 0.0f);
}

SWIM_TEST("Engine.CameraEffects", "FeaturesRecordTheirPassesInTheirStages")
{
	CameraWorld world;
	bool replaced = false;

	// Inactive settings record nothing and keep the scene colour.
	Engine::CameraLens lens;
	Engine::FilmSensor sensor;
	SWIM_CHECK(world.Run(lens, S::Format::RGBA16Float, replaced).empty() && !replaced);
	SWIM_CHECK(world.Run(sensor, S::Format::RGBA8Unorm, replaced).empty() && !replaced);

	// Depth of field: prepare and gather at half resolution (32 x 16 -> 4 x 2 groups), composite at full.
	Engine::DepthOfField dof;
	SWIM_CHECK(dof.GetStage() == Engine::RenderFeatureStage::BeforePostProcess);
	auto passes = world.Run(dof, S::Format::RGBA16Float, replaced);
	SWIM_REQUIRE_EQUAL(passes.size(), std::size_t(3));
	SWIM_CHECK(passes[0].first == "DepthOfFieldPrepare" && passes[0].second == 4u);
	SWIM_CHECK(passes[1].first == "DepthOfFieldGather" && passes[1].second == 4u);
	SWIM_CHECK(passes[2].first == "DepthOfField" && passes[2].second == 8u);
	SWIM_CHECK(replaced);

	// The lens after it (order), halation at quarter resolution (16 x 8 -> 2 groups).
	lens.Settings.Vignette = 0.3f;
	lens.Settings.Halation = 0.1f;
	SWIM_CHECK(lens.GetOrder() > dof.GetOrder());
	passes = world.Run(lens, S::Format::RGBA16Float, replaced);
	SWIM_REQUIRE_EQUAL(passes.size(), std::size_t(2));
	SWIM_CHECK(passes[0].first == "CameraLensHalation" && passes[0].second == 2u);
	SWIM_CHECK(passes[1].first == "CameraLens" && passes[1].second == 8u);
	SWIM_CHECK(replaced);

	// The sensor on the display-referred frame.
	sensor.Settings.Grain = 0.01f;
	SWIM_CHECK(sensor.GetStage() == Engine::RenderFeatureStage::AfterPostProcess);
	passes = world.Run(sensor, S::Format::RGBA8Unorm, replaced);
	SWIM_REQUIRE_EQUAL(passes.size(), std::size_t(1));
	SWIM_CHECK(passes[0].first == "FilmSensor" && passes[0].second == 8u);
	SWIM_CHECK(replaced);
}

SWIM_TEST("Engine.GravitationalLensing", "GeodesicsCastTheShadowAndBendLikeEinstein")
{
	using L = Engine::GravitationalLensing;
	SWIM_CHECK_NEAR(L::ShadowRadius(1.0f), 2.598076f, 1e-5f);
	SWIM_CHECK_NEAR(L::Deflection(1.0f, 10.0f), 0.2f, 1e-6f);
	// Rays from far away (x = -400 R_s) with impact parameter b along +y.
	const auto trace = [](float b)
	{
		return L::TraceRay({ -400.0f, b, 0.0f }, { 1.0f, 0.0f, 0.0f }, 1.0f, 400.0f);
	};
	// Inside the photon-capture radius the ray falls in; just outside it escapes, strongly bent.
	SWIM_CHECK(trace(2.45f).Captured);
	const auto grazing = trace(2.75f);
	SWIM_REQUIRE(grazing.Escaped);
	SWIM_CHECK(std::acos(std::clamp(grazing.Direction[0], -1.0f, 1.0f)) > 1.0f); // Over 57 degrees.
	// Far out it matches Einstein's 2 R_s / b (plus the 15 pi / 4 (R_s / 2b)^2 second order).
	for (const float b : { 20.0f, 50.0f })
	{
		const auto far = trace(b);
		SWIM_REQUIRE(far.Escaped);
		const float angle = std::atan2(-far.Direction[1], far.Direction[0]); // Toward the mass (-y).
		const float expected = 2.0f / b + 15.0f * 3.14159265f / 16.0f / (b * b);
		SWIM_CHECK(std::abs(angle - expected) < 0.06f * expected);
	}

	// Bending grows monotonically inward (no seams in the image).
	float previous = 0.0f;

	for (const float b : { 40.0f, 20.0f, 10.0f, 6.0f, 4.0f, 3.0f })
	{
		const auto t = trace(b);
		SWIM_REQUIRE(t.Escaped);
		const float angle = std::atan2(-t.Direction[1], t.Direction[0]);
		SWIM_CHECK(angle > previous);
		previous = angle;
	}
}

SWIM_TEST("Engine.GravitationalLensing", "TheGasOrbitsInATorusAndRings")
{
	using L = Engine::GravitationalLensing;
	L::Lens lens;
	// Nothing inside 1.2 R_s or beyond the gas radius; none without density.
	SWIM_CHECK(L::GasDensityAt({ 1.0f, 0.0f, 0.0f }, 0.0f, lens) == 0.0f);
	SWIM_CHECK(L::GasDensityAt({ lens.GasRadius + 0.5f, 0.0f, 0.0f }, 0.0f, lens) == 0.0f);
	// The torus lies in the disk plane (axis +Y): around 4-6 R_s there is gas in the plane,
	// much less well above it.
	float inPlane = 0.0f;
	float above = 0.0f;
	lens.Orbits = 0;

	for (int i = 0; i < 64; ++i)
	{
		const float a = float(i) * 0.0981748f;
		inPlane += L::GasDensityAt({ 5.0f * std::cos(a), 0.0f, 5.0f * std::sin(a) }, 0.0f, lens);
		above += L::GasDensityAt({ 5.0f * std::cos(a), 3.0f, 5.0f * std::sin(a) }, 0.0f, lens);
	}

	SWIM_CHECK(inPlane > 0.5f);
	SWIM_CHECK(above < 0.2f * inPlane);
	// It flows: the field at a point changes with time, and a tilted axis tilts it.
	float change = 0.0f;

	for (int i = 0; i < 16; ++i)
	{
		const L::Float3 point{ 4.5f * std::cos(float(i)), 0.1f, 4.5f * std::sin(float(i)) };
		change += std::abs(L::GasDensityAt(point, 0.0f, lens) - L::GasDensityAt(point, 0.7f, lens));
	}

	SWIM_CHECK(change > 0.05f);
	// The rings add gas off the disk plane (on their inclined orbits).
	lens.Orbits = 3;
	float ringGas = 0.0f;

	for (int i = 0; i < 400; ++i)
	{
		const float u = float(i) * 0.61803f;
		const float v = float(i) * 0.137f;
		const L::Float3 point{ 5.0f * std::cos(u) * std::cos(v), 5.0f * std::sin(v), 5.0f * std::sin(u) * std::cos(v) };

		if (std::abs(point[1]) > 2.5f)
		{
			ringGas += L::GasDensityAt(point, 0.0f, lens);
		}
	}

	SWIM_CHECK(ringGas > 0.1f);
	lens.GasDensity = 0.0f;
	SWIM_CHECK(L::GasDensityAt({ 5.0f, 0.0f, 0.0f }, 0.0f, lens) == 0.0f);
}

SWIM_TEST("Engine.GravitationalLensing", "LensesAreOwnedAndRecordOnePass")
{
	CameraWorld world;
	world.Add("GravitationalLensing", { { "Params", S::DescriptorType::ReadOnlyStorageBuffer }, { "Color", S::DescriptorType::SampledTexture },
										  { "Depth", S::DescriptorType::SampledTexture }, { "LinearClamp", S::DescriptorType::Sampler },
										  { "Output", S::DescriptorType::StorageTexture, S::Format::RGBA16Float } });
	Engine::GravitationalLensing lensing;
	bool replaced = false;
	SWIM_CHECK(world.Run(lensing, S::Format::RGBA16Float, replaced).empty() && !replaced);

	auto& a = lensing.Upsert(7);
	a.Position = { 0.0f, 0.0f, -10.0f };
	lensing.Upsert(9).SchwarzschildRadius = 0.5f;
	SWIM_CHECK_EQUAL(lensing.Lenses.size(), std::size_t(2));
	SWIM_CHECK(&lensing.Upsert(7) == &lensing.Lenses[0]); // The same owner keeps its lens.
	lensing.Remove(9);
	SWIM_CHECK_EQUAL(lensing.Lenses.size(), std::size_t(1));

	const auto passes = world.Run(lensing, S::Format::RGBA16Float, replaced);
	SWIM_REQUIRE_EQUAL(passes.size(), std::size_t(1));
	SWIM_CHECK(passes[0].first == "GravitationalLensing" && passes[0].second == 8u);
	SWIM_CHECK(replaced);
	// Before the camera's depth of field and lens.
	SWIM_CHECK(lensing.GetOrder() < Engine::DepthOfField().GetOrder());
}
