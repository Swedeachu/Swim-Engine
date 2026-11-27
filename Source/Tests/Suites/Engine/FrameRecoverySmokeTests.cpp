// A frame that fails or gets an unusable camera must not poison the frames after it: the
// renderer keeps rendering on both backends.
#include "Engine/EngineConfig.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Renderer/Features/VolumetricClouds.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"
#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"
#include "Engine/Systems/Renderer/Runtime/RenderDevice.h"
#include "Engine/Systems/Renderer/Runtime/SceneViewports.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Systems/Scene/SceneSystem.h"
#include "Engine/SwimEngine.h"
#include "Tests/Fixtures/SmokeBackend.h"
#include "Tests/Framework/Test.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

	class RecoveryScene : public Engine::Scene
	{

	  public:
		using Scene::Scene;
	};

	// Records a pass that throws while the executor records it (once armed): a frame that
	// fails after every feature, history and cache has already imported into its graph.
	class FailingPass final : public Engine::RenderFeature
	{

	  public:
		std::string_view GetName() const override { return "Failing pass"; }

		Engine::RenderFeatureStage GetStage() const override { return Engine::RenderFeatureStage::AfterPostProcess; }

		void Record(Engine::RenderFeatureContext& context) override
		{
			if (!Armed)
			{
				return;
			}

			Armed = false;
			context.Graph().AddPass(
				"Failing pass", Swim::Rhi::QueueType::Graphics,
				[](Swim::Render::RenderGraphBuilder& builder)
				{
					builder.SideEffect();
				},
				[](Swim::Render::RenderCommandContext&)
				{
					throw std::runtime_error("injected pass failure");
				});
		}

		bool Armed = false;
	};

	std::array<float, 3> ToArray(const glm::vec3& value)
	{
		return { value.x, value.y, value.z };
	}

	// The input the engine would build from its main camera.
	Engine::RenderFrameInput CameraInput(const Engine::Camera& camera)
	{
		Engine::RenderFrameInput input;
		auto& render = input.Camera;
		render.View = camera.GetViewRowMajor();
		render.Projection = camera.GetProjectionRowMajor();
		render.Position = ToArray(camera.GetPosition());
		render.Forward = ToArray(camera.GetForward());
		render.Right = ToArray(camera.GetRight());
		render.Up = ToArray(camera.GetUp());
		render.VerticalFov = glm::radians(camera.GetFieldOfView());
		render.Aspect = camera.GetAspect();
		render.Near = camera.GetNearPlane();
		render.Far = camera.GetFarPlane();
		input.DeltaTime = 1.0f / 60.0f;
		return input;
	}

	// The staged runtime shader set of this build (with D3D12 bytecode), not a copy that may be
	// deployed next to the test executable.
	struct ShaderDirectoryOverride
	{
		ShaderDirectoryOverride() { _putenv_s("SWIM_SHADER_DIR", SWIM_RUNTIME_SHADER_DIR); }

		~ShaderDirectoryOverride() { _putenv_s("SWIM_SHADER_DIR", ""); }
	};

	void RunFrameRecovery(Engine::GraphicsBackend backend)
	{
		const ShaderDirectoryOverride shaders;
		Engine::EngineConfig config;
		config.Graphics = backend;
		config.Present = Engine::PresentMode::Headless;
		config.Render = true;
		config.Validation = true;
		config.Audio = Engine::AudioOutput::Off;
		config.PlatformInput = false;
		config.UserConfigFile.clear();
		config.FixedFrameDelta = 1.0 / 60.0;
		config.Window.Width = 320;
		config.Window.Height = 180;
		auto engine = std::make_unique<Engine::SwimEngine>(config);
		engine->GetSceneSystem()->RegisterSceneType<RecoveryScene>("Recovery");
		engine->GetSceneSystem()->SetStartupScene("Recovery");
		SWIM_REQUIRE(engine->Start() == 0);
		auto* renderer = engine->GetRenderer();
		SWIM_REQUIRE(renderer != nullptr);
		SWIM_REQUIRE(engine->GetGraphicsBackend() == backend);
		auto& camera = engine->GetCameraSystem()->GetCamera();
		camera.SetPosition({ 0.0f, 2.0f, 6.0f });
		// Features that cache what they imported into a graph, and temporal history.
		const auto clouds = std::make_shared<Engine::VolumetricClouds>();
		renderer->AddFeature(clouds);
		const auto failingPass = std::make_shared<FailingPass>();
		renderer->AddFeature(failingPass);
		renderer->GetSettings().AntiAliasing = Engine::AntiAliasingMode::Taa;
		renderer->GetSettings().TemporalAntiAliasing = true;

		for (int i = 0; i < 4; ++i)
		{
			SWIM_REQUIRE(engine->Tick());
		}

		const auto warmed = renderer->GetStats().Frame;
		SWIM_REQUIRE(warmed > 0);

		// A non-finite camera: the renderer keeps the last valid one and the frame renders.
		auto broken = CameraInput(camera);
		broken.Camera.View[3] = std::numeric_limits<float>::quiet_NaN();
		broken.Camera.Position[0] = std::numeric_limits<float>::infinity();
		SWIM_CHECK(renderer->Render(broken));
		SWIM_CHECK_EQUAL(renderer->GetStats().Frame, warmed + 1);

		// A frame that fails part-way (screen-space effects need a perspective projection):
		// it is skipped, and the frames after it render normally.
		auto failing = CameraInput(camera);
		failing.Camera.Projection = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			std::string error;

			try
			{
				renderer->Render(failing);
			}
			catch (const std::exception& failure)
			{
				error = failure.what();
			}

			// Each failure is the frame's own, never one left behind by an earlier failed frame.
			SWIM_CHECK(error.find("perspective") != std::string::npos);
		}

		// A frame that fails while its passes are recorded, after the clouds, TAA and the other
		// caches imported into its graph. The next graph usually sits at the same address with
		// the same frame number; nothing imported into the failed one may be reused.
		const auto render = [&](const Engine::RenderFrameInput& input)
		{
			std::string error;

			try
			{
				renderer->Render(input);
			}
			catch (const std::exception& failure)
			{
				error = failure.what();
			}

			return error;
		};

		for (int attempt = 0; attempt < 2; ++attempt)
		{
			failingPass->Armed = true;
			SWIM_CHECK_EQUAL(render(CameraInput(camera)), std::string("injected pass failure"));

			for (int i = 0; i < 3; ++i)
			{
				SWIM_CHECK_EQUAL(render(CameraInput(camera)), std::string());
			}
		}

		// A feature that met a handle of the failed graph would have been switched off.
		SWIM_CHECK(clouds->Enabled);
		SWIM_CHECK(failingPass->Enabled);
		const auto afterFailure = renderer->GetStats().Frame;

		for (int i = 0; i < 6; ++i)
		{
			SWIM_REQUIRE(engine->Tick());
		}

		SWIM_CHECK_EQUAL(renderer->GetStats().Frame, afterFailure + 6);

		for (int i = 0; i < 3; ++i)
		{
			SWIM_CHECK(renderer->Render(CameraInput(camera)));
		}

		SWIM_CHECK_EQUAL(renderer->GetStats().Frame, afterFailure + 9);
		SWIM_CHECK_EQUAL(renderer->GetRenderDevice().GetValidationErrorCount(), 0u);
		engine.reset();
	}

	// Scene viewports with opaque and translucent items in one scene (the translucent ones
	// switch pipelines mid-region): every frame renders, and the scene is drawn.
	void RunTranslucentViewport(Engine::GraphicsBackend backend, bool validation)
	{
		const ShaderDirectoryOverride shaders;
		Engine::EngineConfig config;
		config.Graphics = backend;
		config.Present = Engine::PresentMode::Headless;
		config.Render = true;
		config.Validation = validation;
		config.Audio = Engine::AudioOutput::Off;
		config.PlatformInput = false;
		config.UserConfigFile.clear();
		config.FixedFrameDelta = 1.0 / 60.0;
		config.Window.Width = 320;
		config.Window.Height = 180;
		auto engine = std::make_unique<Engine::SwimEngine>(config);
		engine->GetSceneSystem()->RegisterSceneType<RecoveryScene>("Recovery");
		engine->GetSceneSystem()->SetStartupScene("Recovery");
		SWIM_REQUIRE(engine->Start() == 0);
		auto* renderer = engine->GetRenderer();
		SWIM_REQUIRE(renderer != nullptr);
		auto& viewports = renderer->GetViewports();
		Engine::SceneViewportDesc desc;
		desc.Width = 512;
		desc.Height = 256;
		desc.DebugName = "Translucent icons";
		const auto handle = viewports.Create(desc);
		SWIM_REQUIRE(static_cast<bool>(handle));
		const auto cube = renderer->GetMeshes().Get(Engine::BuiltinMesh::Cube);
		// A 16 x 16 texture with clear, translucent and opaque texels (icons sample one).
		std::vector<std::uint8_t> pixels(16 * 16 * 4);

		for (std::size_t i = 0; i < 16 * 16; ++i)
		{
			pixels[i * 4 + 0] = std::uint8_t(i);
			pixels[i * 4 + 1] = 120;
			pixels[i * 4 + 2] = 200;
			pixels[i * 4 + 3] = i % 3 == 0 ? 0 : i % 3 == 1 ? 128 : 255;
		}

		const auto texture = renderer->GetMeshes().RegisterTexture("Test/Icon", 16, 16, pixels);

		// Regions of an atlas, each an opaque cube behind a translucent one (every other one
		// textured): each region starts with the opaque pipeline again after the last ended
		// translucent.
		constexpr std::uint32_t Regions = 32;

		for (std::uint32_t region = 0; region < Regions; ++region)
		{
			Engine::SceneViewportScene scene;
			scene.View = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -5, 0, 0, 0, 1 };
			scene.Projection = Swim::Render::OrthographicReverseZRowMajor(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 20.0f);
			scene.Region = { (region % 8) * 64, (region / 8) * 64, 64, 64 };
			Engine::SceneViewportItem back;
			back.Mesh = cube;

			if (region % 2 == 0)
			{
				back.Texture = texture;
			}
			back.Transform = { 1, 0, 0, 0.3f, 0, 1, 0, 0, 0, 0, 1, -1, 0, 0, 0, 1 };
			Engine::SceneViewportItem front = back;
			front.Transform = { 1, 0, 0, -0.3f, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1 };
			front.Tint = { 0.8f, 0.2f, 0.2f, 0.5f };
			front.AlphaCutoff = 0.01f;
			front.Translucent = true;
			scene.Items = { front, back };
			viewports.Draw(handle, std::move(scene));
		}

		const auto start = renderer->GetStats().Frame;
		int ticks = 0;

		for (; ticks < 120 && viewports.GetStats().Drawn < Regions; ++ticks)
		{
			SWIM_REQUIRE(engine->Tick());
		}

		SWIM_CHECK_EQUAL(viewports.GetStats().Drawn, std::uint64_t{ Regions });

		for (int i = 0; i < 3; ++i, ++ticks)
		{
			SWIM_REQUIRE(engine->Tick());
		}

		// No frame was skipped.
		SWIM_CHECK_EQUAL(renderer->GetStats().Frame, start + std::uint64_t(ticks));
		SWIM_CHECK_EQUAL(renderer->GetRenderDevice().GetValidationErrorCount(), 0u);
		engine.reset();
	}

	// A viewport destroyed and created again every frame (a new extent each time) while the
	// frames that drew it are still in flight: nothing they use is freed under them.
	void RunViewportRecreation(Engine::GraphicsBackend backend)
	{
		const ShaderDirectoryOverride shaders;
		Engine::EngineConfig config;
		config.Graphics = backend;
		config.Present = Engine::PresentMode::Headless;
		config.Render = true;
		config.Validation = true;
		config.Audio = Engine::AudioOutput::Off;
		config.PlatformInput = false;
		config.UserConfigFile.clear();
		config.FixedFrameDelta = 1.0 / 60.0;
		config.Window.Width = 320;
		config.Window.Height = 180;
		auto engine = std::make_unique<Engine::SwimEngine>(config);
		engine->GetSceneSystem()->RegisterSceneType<RecoveryScene>("Recovery");
		engine->GetSceneSystem()->SetStartupScene("Recovery");
		SWIM_REQUIRE(engine->Start() == 0);
		auto* renderer = engine->GetRenderer();
		SWIM_REQUIRE(renderer != nullptr);
		auto& viewports = renderer->GetViewports();
		const auto cube = renderer->GetMeshes().Get(Engine::BuiltinMesh::Cube);
		Engine::SceneViewportHandle handle;
		const auto start = renderer->GetStats().Frame;
		constexpr int Frames = 48;

		for (int frame = 0; frame < Frames; ++frame)
		{
			if (handle)
			{
				SWIM_CHECK(viewports.Destroy(handle));
			}

			Engine::SceneViewportDesc desc;
			desc.Width = 96 + std::uint32_t(frame % 5) * 32;
			desc.Height = 128 + std::uint32_t(frame % 3) * 16;
			desc.DebugName = "Recreated viewport";
			handle = viewports.Create(desc);
			SWIM_REQUIRE(static_cast<bool>(handle));
			Engine::SceneViewportScene scene;
			scene.View = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -5, 0, 0, 0, 1 };
			scene.Projection = Swim::Render::OrthographicReverseZRowMajor(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 20.0f);
			scene.Region = { 0, 0, desc.Width, desc.Height };
			Engine::SceneViewportItem item;
			item.Mesh = cube;
			item.Transform = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
			scene.Items = { item };
			viewports.Draw(handle, std::move(scene));
			SWIM_REQUIRE(engine->Tick());
		}

		for (int i = 0; i < 4; ++i)
		{
			SWIM_REQUIRE(engine->Tick());
		}

		SWIM_CHECK_EQUAL(renderer->GetStats().Frame, start + std::uint64_t(Frames + 4));
		SWIM_CHECK(viewports.GetStats().Drawn >= std::uint64_t(Frames / 2)); // Early draws may wait for residency.
		SWIM_CHECK_EQUAL(renderer->GetRenderDevice().GetValidationErrorCount(), 0u);
		engine.reset();
	}

	struct RegisterFrameRecovery
	{
		RegisterFrameRecovery()
		{
			if (!Swim::Testing::SmokesEnabled())
			{
				return;
			}

			Swim::Testing::TestRegistry::Get().Add(
				{ "RHI.Vulkan.Smoke", "EngineFramesRecoverAfterABadCameraOrAFailedFrame", SWIM_TEST_LOCATION,
					+[]
					{
						RunFrameRecovery(Engine::GraphicsBackend::Vulkan);
					} });
			Swim::Testing::TestRegistry::Get().Add(
				{ "RHI.Vulkan.Smoke", "SceneViewportsBlendTranslucentItemsOverOpaqueOnes", SWIM_TEST_LOCATION,
					+[]
					{
						RunTranslucentViewport(Engine::GraphicsBackend::Vulkan, true);
					} });
			Swim::Testing::TestRegistry::Get().Add(
				{ "RHI.Vulkan.Smoke", "SceneViewportsBlendTranslucentItemsWithoutValidation", SWIM_TEST_LOCATION,
					+[]
					{
						RunTranslucentViewport(Engine::GraphicsBackend::Vulkan, false);
					} });
			Swim::Testing::TestRegistry::Get().Add(
				{ "RHI.Vulkan.Smoke", "SceneViewportsRecreatedEveryFrameOutliveTheirFrames", SWIM_TEST_LOCATION,
					+[]
					{
						RunViewportRecreation(Engine::GraphicsBackend::Vulkan);
					} });
#if defined(SWIM_RHI_D3D12)
			Swim::Testing::TestRegistry::Get().Add(
				{ "RHI.D3D12.Smoke", "EngineFramesRecoverAfterABadCameraOrAFailedFrame", SWIM_TEST_LOCATION,
					+[]
					{
						RunFrameRecovery(Engine::GraphicsBackend::D3D12);
					} });
			Swim::Testing::TestRegistry::Get().Add(
				{ "RHI.D3D12.Smoke", "SceneViewportsBlendTranslucentItemsOverOpaqueOnes", SWIM_TEST_LOCATION,
					+[]
					{
						RunTranslucentViewport(Engine::GraphicsBackend::D3D12, true);
					} });
			Swim::Testing::TestRegistry::Get().Add(
				{ "RHI.D3D12.Smoke", "SceneViewportsBlendTranslucentItemsWithoutValidation", SWIM_TEST_LOCATION,
					+[]
					{
						RunTranslucentViewport(Engine::GraphicsBackend::D3D12, false);
					} });
			Swim::Testing::TestRegistry::Get().Add(
				{ "RHI.D3D12.Smoke", "SceneViewportsRecreatedEveryFrameOutliveTheirFrames", SWIM_TEST_LOCATION,
					+[]
					{
						RunViewportRecreation(Engine::GraphicsBackend::D3D12);
					} });
#endif
		}
	};

	const RegisterFrameRecovery registerFrameRecovery;

} // namespace
