#include "Engine/Runtime/FrameProfiler.h"
#include "Engine/Runtime/RenderToggles.h"
#include "Tests/Framework/Test.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

SWIM_TEST("Engine.Profiling", "CapturesSumPerFrameAndReportPercentiles")
{
	Engine::FrameProfiler profiler;
	// Frames outside a capture are dropped.
	profiler.Add("cpu", "Update", 5.0);
	SWIM_CHECK(!profiler.EndFrame());
	const auto csv = std::filesystem::temp_directory_path() /
		("swim-profile-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv");
	{ std::error_code ignored; std::filesystem::remove(csv, ignored); }
	profiler.Begin(2, 4, csv, "unit");
	SWIM_CHECK(profiler.IsCapturing());

	for (int frame = 0; frame < 6; ++frame)
	{
		// Warm-up frames carry a huge value that must not count.
		const double base = frame < 2 ? 1000.0 : double(frame - 1);
		profiler.Add("cpu", "Update", base);
		profiler.Add("gpu", "Pass", 0.5);
		profiler.Add("gpu", "Pass", 0.25); // Same name twice in one frame: summed.

		if (frame == 5)
		{
			profiler.Add("gpu", "Rare", 2.0);
		}

		const bool done = profiler.EndFrame();
		SWIM_CHECK_EQUAL(done, frame == 5);
	}

	SWIM_CHECK(!profiler.IsCapturing());
	const auto& report = profiler.GetReport();
	SWIM_CHECK_EQUAL(report.Frames, 4u);
	const auto* update = report.Find("cpu", "Update");
	SWIM_REQUIRE(update != nullptr);
	SWIM_CHECK_NEAR(update->Mean, 2.5, 1e-9); // 1, 2, 3, 4.
	SWIM_CHECK_NEAR(update->Min, 1.0, 1e-9);
	SWIM_CHECK_NEAR(update->Max, 4.0, 1e-9);
	SWIM_CHECK_NEAR(update->P50, 2.5, 1e-9);
	const auto* pass = report.Find("gpu", "Pass");
	SWIM_REQUIRE(pass != nullptr);
	SWIM_CHECK_NEAR(pass->Mean, 0.75, 1e-9);
	const auto* rare = report.Find("gpu", "Rare");
	SWIM_REQUIRE(rare != nullptr);
	SWIM_CHECK_EQUAL(rare->Frames, 1u);
	SWIM_CHECK_NEAR(rare->PerFrame, 0.5, 1e-9); // 2 ms once in 4 frames.
	// Sorted: within a category, the costliest per frame first.
	SWIM_CHECK(report.Zones.front().Category == "cpu");
	// The CSV has a header and one row per zone.
	std::stringstream text;
	{
		std::ifstream in(csv);
		text << in.rdbuf();
	}
	SWIM_CHECK(text.str().starts_with("label,category,name,frames,per_frame_ms"));
	SWIM_CHECK(text.str().find("unit,gpu,Rare,1,0.5000") != std::string::npos);
	SWIM_CHECK(Engine::FrameProfiler::Summary(report).find("Update") != std::string::npos);
	{ std::error_code ignored; std::filesystem::remove(csv, ignored); }
}

SWIM_TEST("Engine.Profiling", "TogglesSetByNameGroupAndAll")
{
	Engine::RenderToggles toggles; // Headless: registered toggles only.
	bool a = true, b = true, c = true;
	const auto flag = [](std::string name, bool& value)
	{
		bool* target = &value;
		return Engine::RenderToggles::Toggle{ std::move(name), "test",
			[target]
			{
				return *target;
			},
			[target](bool on)
			{
				*target = on;
			} };
	};
	toggles.Register(flag("scene.a", a));
	toggles.Register(flag("scene.b", b));
	toggles.Register(flag("other.c", c));
	SWIM_CHECK_EQUAL(toggles.List().size(), std::size_t(3));
	SWIM_CHECK_EQUAL(toggles.Set("scene.a", false), std::size_t(1));
	SWIM_CHECK(!a && b && c);
	SWIM_CHECK(toggles.Get("scene.a") == false);
	SWIM_CHECK_EQUAL(toggles.Set("scene.*", false), std::size_t(2));
	SWIM_CHECK(!a && !b && c);
	SWIM_CHECK_EQUAL(toggles.Set("all", true), std::size_t(3));
	SWIM_CHECK(a && b && c);
	SWIM_CHECK_EQUAL(toggles.Set("missing", false), std::size_t(0));
	SWIM_CHECK(!toggles.Get("missing").has_value());
	// Registering a name again replaces it; Unregister removes by prefix.
	toggles.Register(flag("scene.a", c));
	SWIM_CHECK_EQUAL(toggles.List().size(), std::size_t(3));
	toggles.Unregister("scene.");
	SWIM_CHECK_EQUAL(toggles.List().size(), std::size_t(1));
	SWIM_CHECK(Engine::RenderToggles::Slug("Volumetric clouds (v2)") == "volumetric-clouds-v2");
}
