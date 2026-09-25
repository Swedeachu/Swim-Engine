#include "Engine/Systems/UI/UiCanvasRouter.h"
#include "Engine/Systems/UI/UiWidgets.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>

using namespace Swim::UI;

namespace
{
	// Reverse-Z infinite perspective (as the renderer's cameras), row-major.
	UiMatrix4 Perspective(float fovY, float aspect, float nearPlane = 0.1f)
	{
		const float f = 1.0f / std::tan(fovY * 0.5f);
		return { f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, 0, nearPlane, 0, 0, -1, 0 };
	}

	UiMatrix4 Orthographic(float halfWidth, float halfHeight)
	{
		return { 1.0f / halfWidth, 0, 0, 0, 0, 1.0f / halfHeight, 0, 0, 0, 0, -0.01f, 0.5f, 0, 0, 0, 1 };
	}

	// Camera at `position` looking down -Z (identity rotation).
	UiCameraView Camera(UiVec3 position, UiMatrix4 projection, float width = 800.0f, float height = 600.0f)
	{
		UiCameraView camera;
		camera.View = { 1, 0, 0, -position.X, 0, 1, 0, -position.Y, 0, 0, 1, -position.Z, 0, 0, 0, 1 };
		camera.Projection = projection;
		camera.ViewportWidth = width;
		camera.ViewportHeight = height;
		return camera;
	}

	UiMatrix3x4 RotationY90(UiVec3 translation, float scale = 1.0f)
	{
		// x -> -z, z -> x.
		return { 0, 0, scale, translation.X, 0, scale, 0, translation.Y, -scale, 0, 0, translation.Z };
	}

	float Distance(UiVec3 a, UiVec3 b)
	{
		return std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z));
	}

	UiVec3 Column(const UiMatrix3x4& m, int c)
	{
		return { m[c], m[4 + c], m[8 + c] };
	}

	float Length(UiVec3 v)
	{
		return std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
	}

	UiPoint Center(const UiRect& rect)
	{
		return { rect.X + rect.Width * 0.5f, rect.Y + rect.Height * 0.5f };
	}

	std::shared_ptr<UiTheme> FontTheme()
	{
		auto theme = std::make_shared<UiTheme>();
		theme->Fonts = Swim::Testing::LoadTextFontChain();
		return theme;
	}

	UiPointer At(UiPoint screen)
	{
		UiPointer pointer;
		pointer.Screen = screen;
		return pointer;
	}

	UiPointer Along(const UiRay& ray, std::span<const UiSurfaceHit> hits = {})
	{
		UiPointer pointer;
		pointer.Ray = ray;
		pointer.SurfaceHits = hits;
		return pointer;
	}

	UiCanvasDesc Desc(UiDocument& document, UiCanvasMode mode = UiCanvasMode::Screen)
	{
		UiCanvasDesc desc;
		desc.Document = &document;
		desc.Mode = mode;
		return desc;
	}

	std::size_t Count(const std::vector<UiEvent>& events, UiEventKind kind, UiNodeId node)
	{
		return std::count_if(events.begin(), events.end(),
			[&](const UiEvent& event)
			{
				return event.Kind == kind && event.Node == node;
			});
	}
} // namespace

SWIM_TEST("UI.Canvas", "ScreenCanvasesMapPixelsToClipSpaceWithTheRhiConvention")
{
	const auto m = ScreenClipFromCanvas(200.0f, 100.0f, 20.0f, 10.0f);
	const auto origin = ProjectCanvasPoint(m, { 200, 100 }, { 0, 0 });
	SWIM_REQUIRE(origin.has_value());
	SWIM_CHECK_NEAR(origin->X, 20.0f, 1e-4f);
	SWIM_CHECK_NEAR(origin->Y, 10.0f, 1e-4f);
	// +Y up in clip space: canvas pixel (0, 0) with no offset is NDC (-1, +1).
	const auto plain = ScreenClipFromCanvas(200.0f, 100.0f);
	SWIM_CHECK_NEAR(plain[3], -1.0f, 1e-6f);
	SWIM_CHECK_NEAR(plain[7], 1.0f, 1e-6f);
	SWIM_CHECK_NEAR(plain[5], -0.02f, 1e-6f);
	const auto rect = ProjectCanvasRect(m, { 200, 100 }, { 10, 5, 30, 20 });
	SWIM_REQUIRE(rect.has_value());
	SWIM_CHECK_NEAR(rect->X, 30.0f, 1e-4f);
	SWIM_CHECK_NEAR(rect->Height, 20.0f, 1e-4f);
	SWIM_CHECK_THROWS(ScreenClipFromCanvas(0.0f, 100.0f), std::invalid_argument);
}

SWIM_TEST("UI.Canvas", "WorldPanelsMapCanvasPixelsAndRaysBothWays")
{
	UiWorldPlacement placement;
	placement.Transform = RotationY90({ 1, 2, 3 });
	placement.UnitsPerPixel = 0.01f;
	const UiPoint size{ 200, 100 };
	const auto m = CanvasToWorld(UiCanvasMode::WorldPanel, placement, size);
	// The pivot (center) sits at the transform's origin; the top-left corner up and left.
	SWIM_CHECK(Distance(TransformPoint(m, { 100, 50, 0 }), { 1, 2, 3 }) < 1e-5f);
	SWIM_CHECK(Distance(TransformPoint(m, { 0, 0, 0 }), { 1, 2.5f, 4 }) < 1e-5f);
	SWIM_CHECK(Distance(Column(m, 2), { 1, 0, 0 }) < 1e-6f); // Front normal: local +Z -> world +X.

	// A ray from the front hits canvas (50, 25) at distance 4.
	UiRay ray{ { 5.0f, 2.25f, 3.5f }, { -1, 0, 0 } };
	auto hit = IntersectCanvas(ray, m, size);
	SWIM_REQUIRE(hit.has_value());
	SWIM_CHECK_NEAR(hit->Point.X, 50.0f, 1e-3f);
	SWIM_CHECK_NEAR(hit->Point.Y, 25.0f, 1e-3f);
	SWIM_CHECK_NEAR(hit->Distance, 4.0f, 1e-5f);
	SWIM_CHECK(hit->FrontFacing);
	// From behind: mirrored hits unless one-sided.
	UiRay back{ { -5.0f, 2.25f, 3.5f }, { 1, 0, 0 } };
	SWIM_CHECK(IntersectCanvas(back, m, size).has_value());
	SWIM_CHECK(!IntersectCanvas(back, m, size)->FrontFacing);
	SWIM_CHECK(!IntersectCanvas(back, m, size, false).has_value());
	// Edges: [0, size) — the far edge is outside, just inside is in.
	SWIM_CHECK(!IntersectCanvas({ { 5.0f, 2.5f, 3.0f - 1.0f }, { -1, 0, 0 } }, m, size).has_value()); // x = 200.
	SWIM_CHECK(IntersectCanvas({ { 5.0f, 2.49f, 3.0f - 0.999f }, { -1, 0, 0 } }, m, size).has_value());
	// Rays pointing away or parallel miss; the unbounded plane still catches rays off the panel.
	SWIM_CHECK(!IntersectCanvas({ { 5.0f, 2.25f, 3.5f }, { 1, 0, 0 } }, m, size).has_value());
	SWIM_CHECK(!IntersectCanvasPlane({ { 5.0f, 2.25f, 3.5f }, { 0, 1, 0 } }, m).has_value());
	const auto off = IntersectCanvasPlane({ { 5.0f, 2.25f, 13.0f }, { -1, 0, 0 } }, m);
	SWIM_REQUIRE(off.has_value());
	SWIM_CHECK_NEAR(off->Point.X, -900.0f, 1e-2f);
	// A scaled transform scales the panel.
	placement.Transform = RotationY90({ 1, 2, 3 }, 2.0f);
	const auto scaled = CanvasToWorld(UiCanvasMode::WorldPanel, placement, size);
	SWIM_CHECK_NEAR(Length(Column(scaled, 0)), 0.02f, 1e-6f);
	SWIM_CHECK(Distance(Column(scaled, 2), { 1, 0, 0 }) < 1e-6f);
	// Invalid placements.
	placement.UnitsPerPixel = 0.0f;
	SWIM_CHECK_THROWS(CanvasToWorld(UiCanvasMode::WorldPanel, placement, size), std::invalid_argument);
	placement.UnitsPerPixel = 0.01f;
	placement.Transform = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 }; // Zero up axis.
	SWIM_CHECK_THROWS(CanvasToWorld(UiCanvasMode::WorldPanel, placement, size), std::invalid_argument);
	SWIM_CHECK_THROWS(CanvasToWorld(UiCanvasMode::Screen, {}, size), std::invalid_argument);
	SWIM_CHECK_THROWS(CanvasToWorld(UiCanvasMode::Billboard, {}, size), std::invalid_argument); // Needs the camera.
}

SWIM_TEST("UI.Canvas", "BillboardsFaceTheCameraAndCanKeepAConstantScreenSize")
{
	const auto camera = Camera({ 0, 0, 10 }, Perspective(1.5707963f, 800.0f / 600.0f));
	const UiPoint size{ 60, 30 };
	UiWorldPlacement placement;
	placement.Transform = { 1, 0, 0, 3, 0, 1, 0, 0, 0, 0, 1, 0 };
	placement.UnitsPerPixel = 0.01f;
	// Spherical: the front normal points at the camera; the pivot stays on the anchor.
	auto m = CanvasToWorld(UiCanvasMode::Billboard, placement, size, &camera);
	const auto front = Column(m, 2);
	const float expected = 1.0f / std::sqrt(109.0f);
	SWIM_CHECK(Distance(front, { -3.0f * expected, 0.0f, 10.0f * expected }) < 1e-5f);
	SWIM_CHECK(Distance(TransformPoint(m, { 30, 15, 0 }), { 3, 0, 0 }) < 1e-5f);
	SWIM_CHECK_NEAR(Length(Column(m, 0)), 0.01f, 1e-7f);
	SWIM_CHECK(Column(m, 1).Y < 0.0f); // Canvas +Y is world down.
	// The camera ray to the anchor hits the canvas center from the front.
	const auto hit = IntersectCanvas({ { 0, 0, 10 }, { 3, 0, -10 } }, m, size);
	SWIM_REQUIRE(hit.has_value());
	SWIM_CHECK(hit->FrontFacing);
	SWIM_CHECK_NEAR(hit->Point.X, 30.0f, 1e-3f);

	// Screen aligned: the camera's axes. Cylindrical: stays upright with the camera above.
	placement.Billboard = UiBillboardMode::ScreenAligned;
	m = CanvasToWorld(UiCanvasMode::Billboard, placement, size, &camera);
	SWIM_CHECK(Distance(Column(m, 2), { 0, 0, 1 }) < 1e-6f);
	const auto above = Camera({ 3, 20, 1 }, Perspective(1.5707963f, 800.0f / 600.0f));
	placement.Billboard = UiBillboardMode::Cylindrical;
	m = CanvasToWorld(UiCanvasMode::Billboard, placement, size, &above);
	SWIM_CHECK(Distance(Column(m, 1), { 0, -0.01f, 0 }) < 1e-6f);
	SWIM_CHECK(Distance(Column(m, 2), { 0, 0, 1 }) < 1e-5f);

	// Constant screen size: 1 canvas pixel = 1 screen pixel at any distance.
	placement.Billboard = UiBillboardMode::Spherical;
	placement.ConstantScreenSize = true;
	placement.Transform = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
	for (const float distance : { 5.0f, 40.0f })
	{
		const auto view = Camera({ 0, 0, distance }, Perspective(1.5707963f, 800.0f / 600.0f));
		m = CanvasToWorld(UiCanvasMode::Billboard, placement, size, &view);
		const auto clip = ClipFromCanvas(m, view);
		const auto left = ProjectCanvasPoint(clip, { 800, 600 }, { 0, 15 });
		const auto right = ProjectCanvasPoint(clip, { 800, 600 }, { 60, 15 });
		SWIM_REQUIRE(left && right);
		SWIM_CHECK_NEAR(right->X - left->X, 60.0f, 1e-2f);
		SWIM_CHECK_NEAR((left->X + right->X) * 0.5f, 400.0f, 1e-2f);
	}

	// Distance fade.
	UiWorldPlacement fade;
	fade.FadeStart = 10.0f;
	fade.FadeEnd = 20.0f;
	SWIM_CHECK_NEAR(CanvasFade(fade, 5.0f), 1.0f, 1e-6f);
	SWIM_CHECK_NEAR(CanvasFade(fade, 15.0f), 0.5f, 1e-6f);
	SWIM_CHECK_NEAR(CanvasFade(fade, 25.0f), 0.0f, 1e-6f);
	SWIM_CHECK_NEAR(CanvasFade({}, 1e6f), 1.0f, 1e-6f);
	SWIM_CHECK_NEAR(CanvasDistance(m, size, { 0.5f, 0.5f }, Camera({ 0, 0, 40 }, Perspective(1.5f, 1.0f)).View), 40.0f, 1e-4f);
}

SWIM_TEST("UI.Canvas", "ScreenRaysRoundTripProjectionForPerspectiveAndOrthographicCameras")
{
	for (const auto& projection : { Perspective(1.2f, 800.0f / 600.0f), Orthographic(8.0f, 6.0f) })
	{
		const auto camera = Camera({ 1, 2, 10 }, projection);
		UiWorldPlacement placement;
		placement.UnitsPerPixel = 0.02f;
		const UiPoint size{ 300, 200 };
		const auto m = CanvasToWorld(UiCanvasMode::WorldPanel, placement, size);
		const auto clip = ClipFromCanvas(m, camera);
		for (const UiPoint point : { UiPoint{ 10, 20 }, UiPoint{ 150, 100 }, UiPoint{ 290, 190 } })
		{
			const auto pixel = ProjectCanvasPoint(clip, { 800, 600 }, point);
			SWIM_REQUIRE(pixel.has_value());
			const auto ray = ScreenRay(camera, *pixel);
			const auto hit = IntersectCanvas(ray, m, size);
			SWIM_REQUIRE(hit.has_value());
			SWIM_CHECK_NEAR(hit->Point.X, point.X, 2e-2f);
			SWIM_CHECK_NEAR(hit->Point.Y, point.Y, 2e-2f);
			SWIM_CHECK(hit->FrontFacing);
		}
	}
	// The viewport center of a perspective camera looks straight down -Z from the eye.
	const auto camera = Camera({ 1, 2, 10 }, Perspective(1.2f, 800.0f / 600.0f));
	const auto ray = ScreenRay(camera, { 400, 300 });
	SWIM_CHECK(Distance(ray.Origin, { 1, 2, 10 }) < 1e-4f);
	SWIM_CHECK(Distance(ray.Direction, { 0, 0, -1 }) < 1e-5f);
	// Behind the camera nothing projects.
	UiWorldPlacement behind;
	behind.Transform = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 20 };
	const auto clip = ClipFromCanvas(CanvasToWorld(UiCanvasMode::WorldPanel, behind, { 10, 10 }), camera);
	SWIM_CHECK(!ProjectCanvasPoint(clip, { 800, 600 }, { 5, 5 }).has_value());
	SWIM_CHECK(!ProjectCanvasRect(clip, { 800, 600 }, { 0, 0, 10, 10 }).has_value());
}

SWIM_TEST("UI.CanvasRouter", "ScreenCanvasesStackAndOnlyBlockWhereTheyHaveNodesOrAskTo")
{
	auto theme = FontTheme();
	UiDocument hud;
	UiDocument modal;
	hud.SetTheme(theme);
	modal.SetTheme(theme);
	const auto button = CreateButton(hud, hud.GetRoot(), "Menu");
	hud.Layout({ 800, 600 });
	modal.Layout({ 100, 100 });
	UiCanvasRouter router;
	const auto hudCanvas = router.Add(Desc(hud));
	const auto modalCanvas = router.Add(
		[&]
		{
			auto d = Desc(modal);
			d.BlocksPointer = true;
			d.Order = 1;
			return d;
		}());
	router.SetScreenPlacement(modalCanvas, { 200, 200 }, { 100, 100 });
	const auto buttonCenter = Center(hud.GetBounds(button));
	SWIM_CHECK(router.PointerMove(At(buttonCenter)) == hudCanvas);
	SWIM_CHECK(HasState(hud.GetState(button), UiState::Hovered));
	SWIM_CHECK(router.PointerMove(At(UiPoint{ 250, 250 })) == modalCanvas); // Blocks without nodes.
	SWIM_CHECK(!HasState(hud.GetState(button), UiState::Hovered));			// The HUD lost the pointer.
	SWIM_CHECK(!router.PointerMove(At(UiPoint{ 500, 500 })));				// The world behind the HUD.
	SWIM_CHECK(!router.IsPointerOverUi());
	// A press on the HUD button focuses it and the HUD canvas.
	router.PointerMove(At(buttonCenter));
	router.PointerDown();
	SWIM_CHECK(router.GetCaptured() == hudCanvas);
	SWIM_CHECK(router.GetFocused() == hudCanvas);
	router.PointerUp();
	SWIM_CHECK_EQUAL(Count(hud.DrainEvents(), UiEventKind::Click, button), 1u);
	// A press outside every canvas gives the keyboard back to the game.
	router.PointerMove(At(UiPoint{ 500, 500 }));
	router.PointerDown();
	router.PointerUp();
	SWIM_CHECK(!router.GetFocused());
	SWIM_CHECK(!hud.GetFocus());
	// Tab without focus starts in the top-most canvas with focusable nodes.
	SWIM_CHECK(router.KeyDown(UiKey::Tab));
	SWIM_CHECK(router.GetFocused() == hudCanvas);
	SWIM_CHECK(hud.GetFocus() == button);
	SWIM_CHECK(router.KeyDown(UiKey::Escape));
	SWIM_CHECK(!router.GetFocused());
	SWIM_CHECK_THROWS(router.Add(UiCanvasDesc{}), std::invalid_argument);
	SWIM_CHECK_THROWS(router.SetWorldPlacement(hudCanvas, UiIdentity3x4, { 1, 1 }), std::invalid_argument);
	SWIM_CHECK(router.Remove(modalCanvas));
	SWIM_CHECK(!router.Contains(modalCanvas));
}

SWIM_TEST("UI.CanvasRouter", "WorldPanelsTakeTheNearestHitAndDragsFollowTheRayOffThePanel")
{
	auto theme = FontTheme();
	const auto camera = Camera({ 0, 0, 10 }, Perspective(1.5707963f, 800.0f / 600.0f));
	UiDocument nearDoc;
	UiDocument farDoc;
	nearDoc.SetTheme(theme);
	farDoc.SetTheme(theme);
	const auto slider = CreateSlider(nearDoc, nearDoc.GetRoot(), { .Min = 0.0f, .Max = 1.0f });
	const auto farButton = CreateButton(farDoc, farDoc.GetRoot(), "Far");
	auto style = farDoc.GetStyle(farButton);
	style.Width = UiLength::Pixels(400);
	style.Height = UiLength::Pixels(200);
	farDoc.SetStyle(farButton, style);
	nearDoc.Layout({ 400, 200 });
	farDoc.Layout({ 400, 200 });
	UiCanvasRouter router;
	router.SetCamera(camera);
	const auto nearCanvas = router.Add(Desc(nearDoc, UiCanvasMode::WorldPanel));
	const auto farCanvas = router.Add(Desc(farDoc, UiCanvasMode::Billboard));
	UiWorldPlacement placement;
	placement.UnitsPerPixel = 0.01f;
	placement.Transform = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 2 }; // 8 m from the camera.
	const auto nearToWorld = CanvasToWorld(UiCanvasMode::WorldPanel, placement, { 400, 200 });
	placement.Transform[11] = -2.0f;
	const auto farToWorld = CanvasToWorld(UiCanvasMode::Billboard, placement, { 400, 200 }, &camera);
	router.SetWorldPlacement(nearCanvas, nearToWorld, { 400, 200 });
	router.SetWorldPlacement(farCanvas, farToWorld, { 400, 200 });
	const auto rayTo = [&](const UiMatrix3x4& m, UiPoint canvasPoint)
	{
		const auto pixel = ProjectCanvasPoint(ClipFromCanvas(m, camera), { 800, 600 }, canvasPoint);
		return ScreenRay(camera, *pixel);
	};
	// Over the near slider: the near panel. Over empty near panel space (not blocking):
	// the far button behind it.
	SWIM_CHECK(router.PointerMove(Along(rayTo(nearToWorld, { 8, 8 }))) == nearCanvas);
	SWIM_CHECK(router.PointerMove(Along(rayTo(nearToWorld, { 300, 150 }))) == farCanvas);
	SWIM_CHECK(HasState(farDoc.GetState(farButton), UiState::Hovered));
	router.SetInteractive(farCanvas, false);
	SWIM_CHECK(!router.PointerMove(Along(rayTo(nearToWorld, { 300, 150 }))));
	router.SetInteractive(farCanvas, true);

	// Press the thumb and drag the ray far off the panel: the capture follows the plane.
	router.PointerMove(Along(rayTo(nearToWorld, { 8, 8 })));
	router.PointerDown();
	SWIM_CHECK(router.GetCaptured() == nearCanvas);
	SWIM_CHECK(router.GetFocused() == nearCanvas);
	UiRay offPanel{ { 0, 0, 10 }, { 3.0f, 0.1f, -1.0f } }; // Crosses the plane far right of the panel.
	SWIM_CHECK(router.PointerMove(Along(offPanel)) == nearCanvas);
	SWIM_CHECK_NEAR(nearDoc.GetValue(slider), 1.0f, 1e-6f);
	router.PointerMove(Along(UiRay{ { 0, 0, 10 }, { 1, 0, 0 } })); // Parallel: keeps the last point.
	router.PointerUp();
	SWIM_CHECK(!router.GetCaptured());
	const auto events = nearDoc.DrainEvents();
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::ValueCommitted, slider), 1u);

	// Surface hits (render surfaces on meshes) compete by distance.
	UiDocument surfaceDoc;
	surfaceDoc.SetTheme(theme);
	const auto surfaceButton = CreateButton(surfaceDoc, surfaceDoc.GetRoot(), "Screen");
	surfaceDoc.Layout({ 256, 128 });
	const auto surface = router.Add(Desc(surfaceDoc, UiCanvasMode::RenderSurface));
	const std::array hits{ UiSurfaceHit{ surface, Center(surfaceDoc.GetBounds(surfaceButton)), 1.0f } };
	SWIM_CHECK(router.PointerMove(Along(rayTo(nearToWorld, { 8, 8 }), hits)) == surface);
	router.PointerDown();
	SWIM_CHECK(router.GetFocused() == surface);
	SWIM_CHECK(!nearDoc.GetFocus());	// Focus moved between canvases.
	SWIM_CHECK(router.Remove(surface)); // Removal while captured ends the capture.
	SWIM_CHECK(!router.GetCaptured());
	SWIM_CHECK(!router.GetFocused());
	router.PointerUp();
}

SWIM_TEST("UI.CanvasRouter", "KeyboardTextAndImeGoToTheFocusedCanvasAndProjectTheirCaret")
{
	auto theme = FontTheme();
	const auto camera = Camera({ 0, 0, 5 }, Perspective(1.5707963f, 800.0f / 600.0f));
	UiDocument screen;
	UiDocument world;
	screen.SetTheme(theme);
	world.SetTheme(theme);
	const auto screenField = CreateTextField(screen, screen.GetRoot());
	const auto worldField = CreateTextField(world, world.GetRoot());
	for (auto* document : { &screen, &world })
	{
		auto style = document->GetStyle(document == &screen ? screenField : worldField);
		style.Width = UiLength::Pixels(200);
		document->SetStyle(document == &screen ? screenField : worldField, style);
	}
	screen.Layout({ 800, 600 });
	world.Layout({ 400, 200 });
	UiCanvasRouter router;
	router.SetCamera(camera);
	const auto screenCanvas = router.Add(Desc(screen));
	const auto worldCanvas = router.Add(Desc(world, UiCanvasMode::WorldPanel));
	router.SetScreenPlacement(screenCanvas, { 10, 20 });
	UiWorldPlacement placement;
	placement.UnitsPerPixel = 0.01f;
	const auto toWorld = CanvasToWorld(UiCanvasMode::WorldPanel, placement, { 400, 200 });
	router.SetWorldPlacement(worldCanvas, toWorld, { 400, 200 });

	const auto screenPoint = Center(screen.GetBounds(screenField));
	router.PointerMove(At({ screenPoint.X + 10, screenPoint.Y + 20 }));
	router.PointerDown();
	router.PointerUp();
	SWIM_CHECK(router.GetFocused() == screenCanvas);
	SWIM_CHECK(router.WantsTextInput());
	router.TextInput("hi");
	SWIM_CHECK_EQUAL(screen.GetText(screenField), std::string("hi"));
	screen.Layout({ 800, 600 });
	const auto screenRect = router.GetTextInputRect();
	SWIM_REQUIRE(screenRect.has_value());
	SWIM_CHECK_NEAR(screenRect->X, screen.GetTextInputRect().X + 10.0f, 1e-4f);

	// Clicking the world field moves focus: the screen field blurs.
	const auto worldPoint = Center(world.GetBounds(worldField));
	const auto clip = ClipFromCanvas(toWorld, camera);
	const auto pixel = ProjectCanvasPoint(clip, { 800, 600 }, worldPoint);
	SWIM_REQUIRE(pixel.has_value());
	auto both = At(*pixel);
	both.Ray = ScreenRay(camera, *pixel);
	router.PointerMove(both);
	router.PointerDown();
	router.PointerUp();
	SWIM_CHECK(router.GetFocused() == worldCanvas);
	SWIM_CHECK(!screen.GetFocus());
	router.TextInput("xy");
	router.SetComposition("z", 1);
	SWIM_CHECK_EQUAL(world.GetText(worldField), std::string("xy"));
	SWIM_CHECK_EQUAL(screen.GetText(screenField), std::string("hi"));
	world.Layout({ 400, 200 });
	// The IME rectangle is the caret projected through the camera.
	const auto worldRect = router.GetTextInputRect();
	SWIM_REQUIRE(worldRect.has_value());
	const auto caret = world.GetTextInputRect();
	const auto expected = ProjectCanvasRect(clip, { 800, 600 }, caret);
	SWIM_REQUIRE(expected.has_value());
	SWIM_CHECK_NEAR(worldRect->X, expected->X, 1e-3f);
	SWIM_CHECK_NEAR(worldRect->Height, expected->Height, 1e-3f);
	SWIM_CHECK(worldRect->Height > 0.0f);
	// Keys go to the owner only.
	SWIM_CHECK(router.KeyDown(UiKey::Backspace));
	SWIM_CHECK_EQUAL(screen.GetText(screenField), std::string("hi"));
	router.ClearFocus();
	SWIM_CHECK(!world.GetFocus());
	SWIM_CHECK(!router.KeyDown(UiKey::Backspace));
}
