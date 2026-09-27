#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{
	class FrameRenderer;

	// Every switch that removes a slice of the frame, by name, for profiling and A/B checks
	// (`render.toggles`, `render.toggle <name|group.*|all> [0|1]`, and the panel's Profiling
	// section): the renderer's own passes (RenderSettings), each render feature
	// ("feature.<name>", whatever gameplay added) and anything a scene registers ("scene.*").
	// Turning a switch off must leave the frame valid and turning it back on must restore
	// exactly what was there (budgets and intervals a switch zeroes are remembered).
	class RenderToggles
	{
	  public:
		struct Toggle
		{
			std::string Name;		 // "group.name", lower case.
			std::string Description; // What turning it off removes.
			std::function<bool()> Get;
			std::function<void(bool)> Set;
		};

		// Null renderer (headless): only registered toggles exist.
		explicit RenderToggles(FrameRenderer* renderer = nullptr);

		// Built-in, feature and registered toggles, in that order.
		std::vector<Toggle> List() const;
		std::optional<bool> Get(std::string_view name) const;
		// Sets one toggle, a group ("shadows.*") or everything ("all"); the number changed.
		std::size_t Set(std::string_view pattern, bool on);

		// Scenes add their own (replacing one of the same name) and remove them on exit.
		void Register(Toggle toggle);
		void Unregister(std::string_view prefix);

		// "Volumetric clouds" -> "volumetric-clouds".
		static std::string Slug(std::string_view text);

	  private:
		std::vector<Toggle> BuiltIn() const;

		FrameRenderer* renderer;
		std::vector<Toggle> registered;
		// Values a switch replaced while off.
		mutable struct Saved
		{
			unsigned SunShadows = 1;
			unsigned SpotShadows = 16;
			unsigned PointShadows = 2;
		} saved;
	};
} // namespace Engine
