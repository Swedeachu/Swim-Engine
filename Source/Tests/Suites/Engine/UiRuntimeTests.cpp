#include "Engine/Components/Transform.h"
#include "Engine/Components/UiCanvas.h"
#include "Engine/Runtime/UiRuntime.h"
#include "Engine/Systems/Camera/Camera.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Tests/Framework/Test.h"

#include <filesystem>
#include <memory>
#include <stdexcept>

namespace
{
	Engine::UiRuntime::ViewDesc MakeView()
	{
		Engine::Camera camera;
		camera.SetPosition({ 0.0f, 0.0f, 0.0f }); // At the origin looking down -Z.
		camera.SetAspect(1280.0f / 720.0f);
		Engine::UiRuntime::ViewDesc view;
		view.Camera.View = camera.GetViewRowMajor();
		view.Camera.Projection = camera.GetProjectionRowMajor();
		view.Camera.ViewportWidth = 1280.0f;
		view.Camera.ViewportHeight = 720.0f;
		return view;
	}

	struct AddedCanvas
	{
		entt::entity Entity = entt::null;
		Swim::UI::UiDocument* Document = nullptr;
	};

	AddedCanvas AddCanvas(Engine::Scene& scene, Engine::UiRuntime& ui, Swim::UI::UiCanvasMode mode, std::int32_t order,
		const glm::vec3& position, bool visible = true)
	{
		const entt::entity entity = scene.CreateEntity();
		scene.AddComponent<Engine::Transform>(entity, Engine::Transform(position, glm::vec3(1.0f)));
		Engine::UiCanvas canvas;
		canvas.Document = ui.CreateDocument();
		canvas.Mode = mode;
		canvas.Order = order;
		canvas.Visible = visible;
		canvas.Size = { 200.0f, 100.0f };
		Swim::UI::UiDocument* document = canvas.Document.get();
		scene.AddComponent<Engine::UiCanvas>(entity, std::move(canvas));
		return { entity, document };
	}
} // namespace

SWIM_TEST("Engine.UiRuntime", "LoadsTheBundledFontsAndRejectsAMissingFolder")
{
	Engine::UiRuntime ui{ std::filesystem::path(SWIM_RESOURCE_DIR) };
	SWIM_CHECK(ui.GetFonts() != nullptr);
	SWIM_CHECK(ui.GetBoldFonts() != nullptr);
	SWIM_CHECK(ui.GetMonoFonts() != nullptr);
	SWIM_CHECK(ui.GetTheme() != nullptr);
	SWIM_CHECK(ui.CreateDocument() != nullptr);
	SWIM_CHECK_THROWS(Engine::UiRuntime(std::filesystem::path(SWIM_RESOURCE_DIR) / "does-not-exist"), std::runtime_error);
}

SWIM_TEST("Engine.UiRuntime", "DrawListPutsWorldCanvasesFirstThenScreensByOrder")
{
	Engine::UiRuntime ui{ std::filesystem::path(SWIM_RESOURCE_DIR) };
	Engine::Scene scene("UiRuntimeTest");
	const auto top = AddCanvas(scene, ui, Swim::UI::UiCanvasMode::Screen, 5, glm::vec3(0.0f));
	const auto bottom = AddCanvas(scene, ui, Swim::UI::UiCanvasMode::Screen, -1, glm::vec3(0.0f));
	const auto panel = AddCanvas(scene, ui, Swim::UI::UiCanvasMode::WorldPanel, 0, glm::vec3(0.0f, 0.0f, -3.0f));
	AddCanvas(scene, ui, Swim::UI::UiCanvasMode::Screen, 9, glm::vec3(0.0f), false); // Hidden.

	ui.Sync(&scene, MakeView());
	ui.ApplyInput(nullptr, 1.0f / 60.0f);
	const auto items = ui.Finish(1.0f / 60.0f);
	SWIM_REQUIRE_EQUAL(items.size(), std::size_t{ 3 });
	SWIM_CHECK(items[0].Document == panel.Document);
	SWIM_CHECK(items[0].ClipFromCanvas.has_value());
	SWIM_CHECK(items[1].Document == bottom.Document);
	SWIM_CHECK(!items[1].ClipFromCanvas.has_value());
	SWIM_CHECK(items[2].Document == top.Document);
	SWIM_CHECK(!ui.IsCapturingInput());
}

SWIM_TEST("Engine.UiRuntime", "CanvasesFollowTheSceneAndLeaveWithIt")
{
	Engine::UiRuntime ui{ std::filesystem::path(SWIM_RESOURCE_DIR) };
	Engine::Scene scene("UiRuntimeTest");
	const auto first = AddCanvas(scene, ui, Swim::UI::UiCanvasMode::Screen, 0, glm::vec3(0.0f));
	AddCanvas(scene, ui, Swim::UI::UiCanvasMode::Billboard, 0, glm::vec3(0.0f, 1.0f, -4.0f));
	ui.Sync(&scene, MakeView());
	SWIM_CHECK_EQUAL(ui.GetCanvasCount(), 2u);

	scene.DestroyEntity(first.Entity);
	ui.Sync(&scene, MakeView());
	SWIM_CHECK_EQUAL(ui.GetCanvasCount(), 1u);
	SWIM_CHECK_EQUAL(ui.Finish(0.0f).size(), std::size_t{ 1 });

	ui.Sync(nullptr, MakeView());
	SWIM_CHECK_EQUAL(ui.GetCanvasCount(), 0u);
	SWIM_CHECK_EQUAL(ui.Finish(0.0f).size(), std::size_t{ 0 });
}

SWIM_TEST("Engine.UiRuntime", "ConstantSizeBillboardsBehindTheCameraAreSkippedNotFatal")
{
	Engine::UiRuntime ui{ std::filesystem::path(SWIM_RESOURCE_DIR) };
	Engine::Scene scene("UiRuntimeTest");
	const auto front = AddCanvas(scene, ui, Swim::UI::UiCanvasMode::Billboard, 0, glm::vec3(0.0f, 0.0f, -5.0f));
	const auto behind = AddCanvas(scene, ui, Swim::UI::UiCanvasMode::Billboard, 0, glm::vec3(0.0f, 0.0f, 5.0f));
	for (const entt::entity entity : { front.Entity, behind.Entity })
	{
		scene.GetRegistry().get<Engine::UiCanvas>(entity).ConstantScreenSize = true;
	}
	ui.Sync(&scene, MakeView());
	auto items = ui.Finish(0.0f);
	SWIM_REQUIRE_EQUAL(items.size(), std::size_t{ 1 });
	SWIM_CHECK(items[0].Document == front.Document);

	// Once the camera turns around, the other one is drawn instead.
	Engine::Camera camera;
	camera.SetPosition({ 0.0f, 0.0f, 0.0f });
	camera.SetAspect(1280.0f / 720.0f);
	camera.SetYawPitch(180.0f, 0.0f);
	auto view = MakeView();
	view.Camera.View = camera.GetViewRowMajor();
	ui.Sync(&scene, view);
	items = ui.Finish(0.0f);
	SWIM_REQUIRE_EQUAL(items.size(), std::size_t{ 1 });
	SWIM_CHECK(items[0].Document == behind.Document);
}
