#include "Engine/Systems/Renderer/Features/Underwater.h"

#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <span>

namespace Engine
{

	namespace
	{

		using Float4 = std::array<float, 4>;

		Float4 Vec(const std::array<float, 3>& v, float w)
		{
			return { v[0], v[1], v[2], w };
		}

		struct MediumParams
		{
			Float4 InverseViewProjection[4];
			Float4 Camera, Surface, Absorption, Scatter, SunDirection, SunColor, Shafts;
			std::uint32_t Size[4];
		};

		struct LensConstants
		{
			Float4 Params, Shape;
			std::uint32_t Size[4];
		};

	} // namespace

	class Underwater::MediumFeature final : public RenderFeature
	{

	  public:

		explicit MediumFeature(const SettingsData& settingsValue) : settings(settingsValue) {}

		std::string_view GetName() const override { return "Underwater medium"; }

		RenderFeatureStage GetStage() const override { return RenderFeatureStage::BeforeTemporal; }

		// After sky features such as clouds, so they are seen through the water too.
		int GetOrder() const override { return 50; }

		void Record(RenderFeatureContext& context) override
		{
			const float submersion = std::clamp(settings.Submersion, 0.0f, 1.0f);

			if (submersion <= 0.0f || !settings.Medium)
			{
				return;
			}

			const auto& view = context.View();
			const glm::mat4 viewProjection = glm::transpose(glm::make_mat4(view.ViewProjection.data()));
			const glm::mat4 inverse = glm::inverse(viewProjection);
			MediumParams p{};

			for (int row = 0; row < 4; ++row)
			{
				p.InverseViewProjection[row] = { inverse[0][row], inverse[1][row], inverse[2][row], inverse[3][row] };
			}

			const float sun = std::max(settings.SunIntensity, 0.0f);
			p.Camera = Vec(view.Position, submersion);
			p.Surface = { settings.SurfaceHeight, view.Time, std::max(settings.CausticScale, 0.01f), std::max(settings.Caustics, 0.0f) };
			p.Absorption = Vec(settings.Absorption, std::max(settings.Density, 0.0f));
			p.Scatter = Vec(settings.ScatterColor, std::max(settings.DepthDarkening, 0.0f));
			p.SunDirection = Vec(view.SunDirection, std::max(settings.ShaftIntensity, 0.0f));
			p.SunColor = { view.SunColor[0] * sun, view.SunColor[1] * sun, view.SunColor[2] * sun, std::max(settings.MaxDistance, 1.0f) };
			p.Shafts = { float(std::min(settings.ShaftSteps, 64u)), std::max(settings.ShaftLength, 0.0f), settings.ChromaticCaustics ? 1.0f : 0.0f,
				std::clamp(settings.TotalInternalReflection, 0.0f, 1.0f) };
			p.Size[0] = view.Width;
			p.Size[1] = view.Height;
			p.Size[2] = view.Temporal ? view.Frame : 0u;

			const auto buffer =
				context.Graph().CreateUpload(std::as_bytes(std::span(&p, 1)), "Underwater params", Swim::Rhi::BufferUsage::Storage, 16);
			const auto output = context.CreateColorTarget("Underwater medium");
			context.Compute("UnderwaterMedium")
				.Buffer("Params", buffer)
				.Texture("Color", context.Color())
				.Texture("Depth", context.Depth())
				.Storage("Output", output)
				.Dispatch(view.Width, view.Height);
			context.SetColor(output);
		}

	  private:

		const SettingsData& settings;

	};

	class Underwater::LensFeature final : public RenderFeature
	{

	  public:

		explicit LensFeature(const SettingsData& settingsValue) : settings(settingsValue) {}

		std::string_view GetName() const override { return "Underwater lens"; }

		RenderFeatureStage GetStage() const override { return RenderFeatureStage::BeforePostProcess; }

		// Before the camera lens, which models the camera behind the water.
		int GetOrder() const override { return 25; }

		void Record(RenderFeatureContext& context) override
		{
			const float submersion = std::clamp(settings.Submersion, 0.0f, 1.0f);
			const bool visible = settings.LensBulge > 0.0f || settings.Wobble > 0.0f || settings.ChromaticAberration > 0.0f;

			if (submersion <= 0.0f || !settings.Lens || !visible)
			{
				return;
			}

			const auto& view = context.View();
			LensConstants constants{};
			constants.Params = { std::clamp(settings.LensBulge, 0.0f, 0.3f), std::max(settings.Wobble, 0.0f),
				std::max(settings.ChromaticAberration, 0.0f), std::fmod(view.Time, 3600.0f) };
			constants.Shape = { settings.WobbleFrequency, settings.WobbleSpeed, submersion, float(view.Width) / float(std::max(view.Height, 1u)) };
			constants.Size[0] = view.Width;
			constants.Size[1] = view.Height;
			const auto output = context.CreateColorTarget("Underwater lens");
			context.Compute("UnderwaterLens")
				.Texture("Color", context.Color())
				.Sampler("LinearClamp")
				.Storage("Output", output)
				.Constants(constants)
				.Dispatch(view.Width, view.Height);
			context.SetColor(output);
		}

	  private:

		const SettingsData& settings;

	};

	Underwater::Underwater() : medium(std::make_shared<MediumFeature>(Settings)), lens(std::make_shared<LensFeature>(Settings))
	{
	}

	Underwater::~Underwater()
	{
		Detach();
	}

	void Underwater::Attach(FrameRenderer& target)
	{
		Detach();
		renderer = &target;
		renderer->AddFeature(medium);
		renderer->AddFeature(lens);
	}

	void Underwater::Detach()
	{
		if (renderer)
		{
			renderer->RemoveFeature(medium.get());
			renderer->RemoveFeature(lens.get());
			renderer = nullptr;
		}
	}

} // namespace Engine
