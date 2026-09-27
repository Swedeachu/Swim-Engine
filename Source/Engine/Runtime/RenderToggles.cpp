#include "Engine/Runtime/RenderToggles.h"

#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"

#include <algorithm>
#include <cctype>

namespace Engine
{
	namespace
	{
		RenderToggles::Toggle Flag(std::string name, std::string description, bool& value)
		{
			bool* target = &value;
			return { std::move(name), std::move(description),
				[target]
				{
					return *target;
				},
				[target](bool on)
				{
					*target = on;
				} };
		}

		// A budget that is zero while off and restored when on.
		RenderToggles::Toggle Budget(std::string name, std::string description, std::uint32_t& value, unsigned& saved)
		{
			std::uint32_t* target = &value;
			unsigned* keep = &saved;
			return { std::move(name), std::move(description),
				[target]
				{
					return *target != 0;
				},
				[target, keep](bool on)
				{
					if (on && *target == 0)
					{
						*target = std::max(*keep, 1u);
					}
					else if (!on && *target != 0)
					{
						*keep = *target;
						*target = 0;
					}
				} };
		}
	} // namespace

	RenderToggles::RenderToggles(FrameRenderer* rendererValue) : renderer(rendererValue)
	{
	}

	std::string RenderToggles::Slug(std::string_view text)
	{
		std::string slug;
		bool dash = false;
		for (const char c : text)
		{
			if (std::isalnum(static_cast<unsigned char>(c)))
			{
				if (dash && !slug.empty())
				{
					slug.push_back('-');
				}
				slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
				dash = false;
			}
			else
			{
				dash = true;
			}
		}
		return slug;
	}

	std::vector<RenderToggles::Toggle> RenderToggles::BuiltIn() const
	{
		std::vector<Toggle> toggles;
		if (!renderer)
		{
			return toggles;
		}
		auto& s = renderer->GetSettings();
		toggles.push_back(Flag("lighting.ibl", "Image-based lighting (environment diffuse + specular)", s.Environment));
		toggles.push_back(Flag("lighting.local-lights", "Every point and spot light (uploads, clusters, shading)", s.LocalLights));
		toggles.push_back(Flag("lighting.environment-updates", "Re-recording the environment with clouds every refresh", s.EnvironmentUpdates));
		toggles.push_back(Flag("sky.background", "The sky pass behind the scene", s.SkyBackground));
		toggles.push_back(Flag("shadows.all", "Every shadow map (atlas pass and shadow lookups)", s.Shadows));
		toggles.push_back(Budget("shadows.sun", "The sun's cascades", s.Shadow.MaxDirectionalShadows, saved.SunShadows));
		toggles.push_back(Budget("shadows.spot", "Spot light shadows", s.Shadow.MaxSpotShadows, saved.SpotShadows));
		toggles.push_back(Budget("shadows.point", "Point light cube shadows", s.Shadow.MaxPointShadows, saved.PointShadows));
		toggles.push_back(Flag("shadows.cascade-cache", "Reuse far sun cascades between frames (off: every cascade every frame)",
			s.ShadowCascadeCache));
		toggles.push_back(Flag("forward.transparent", "The transparent sort and pass", s.Transparent));
		toggles.push_back(Flag("lighting.deferred-local-lights", "Local lights in a compute pass (off: in the Forward+ fragment stage)",
			s.DeferredLocalLights));
		toggles.push_back(Flag("screen.ao", "GTAO and its blur", s.ScreenSpace.AmbientOcclusion.Enabled));
		toggles.push_back(Flag("screen.ssr", "Screen-space reflections", s.ScreenSpace.Reflections.Enabled));
		toggles.push_back(Flag("screen.ssr-history", "SSR reading the previous frame (reflections of reflections)", s.ScreenSpace.Reflections.History));
		toggles.push_back(Flag("screen.ssr-back-faces", "The back-face depth pass (SSR thickness)", s.ScreenSpace.Reflections.BackFaces));
		toggles.push_back(Flag("screen.fog", "Height fog", s.ScreenSpace.Fog.Enabled));
		toggles.push_back(Flag("reflections.probes", "Local reflection probes (captures, filtering, lookups)", s.ReflectionProbes.Enabled));
		toggles.push_back(Flag("temporal.taa", "Temporal anti-aliasing", s.TemporalAntiAliasing));
		toggles.push_back(Flag("effects.particles", "GPU particles (simulation and drawing)", s.Particles));
		toggles.push_back(Flag("post.bloom", "Bloom (down and up chains)", s.Post.Bloom.Enabled));
		toggles.push_back({ "post.auto-exposure", "The luminance histogram and adaptation (off: manual EV)",
			[&s]
			{
				return s.Post.Exposure.Mode == Swim::Render::ExposureMode::Automatic;
			},
			[&s](bool on)
			{
				s.Post.Exposure.Mode = on ? Swim::Render::ExposureMode::Automatic : Swim::Render::ExposureMode::Manual;
			} });
		toggles.push_back(Flag("ui.draw", "Drawing every UI canvas (layout still runs)", s.Ui));
		for (const auto& feature : renderer->GetFeatures())
		{
			if (!feature)
			{
				continue;
			}
			RenderFeature* raw = feature.get();
			toggles.push_back({ "feature." + Slug(feature->GetName()), std::string(feature->GetName()) + " (render feature)",
				[raw]
				{
					return raw->Enabled;
				},
				[raw](bool on)
				{
					raw->Enabled = on;
				} });
		}
		return toggles;
	}

	std::vector<RenderToggles::Toggle> RenderToggles::List() const
	{
		auto toggles = BuiltIn();
		toggles.insert(toggles.end(), registered.begin(), registered.end());
		return toggles;
	}

	std::optional<bool> RenderToggles::Get(std::string_view name) const
	{
		for (const auto& toggle : List())
		{
			if (toggle.Name == name)
			{
				return toggle.Get();
			}
		}
		return std::nullopt;
	}

	std::size_t RenderToggles::Set(std::string_view pattern, bool on)
	{
		const bool all = pattern == "all" || pattern == "*";
		const bool group = pattern.size() > 2 && pattern.substr(pattern.size() - 2) == ".*";
		const std::string_view prefix = group ? pattern.substr(0, pattern.size() - 1) : pattern;
		std::size_t changed = 0;
		for (const auto& toggle : List())
		{
			const bool match = all || (group ? toggle.Name.starts_with(prefix) : toggle.Name == pattern);
			if (match)
			{
				toggle.Set(on);
				++changed;
			}
		}
		return changed;
	}

	void RenderToggles::Register(Toggle toggle)
	{
		std::erase_if(registered,
			[&](const Toggle& existing)
			{
				return existing.Name == toggle.Name;
			});
		registered.push_back(std::move(toggle));
	}

	void RenderToggles::Unregister(std::string_view prefix)
	{
		std::erase_if(registered,
			[&](const Toggle& existing)
			{
				return existing.Name.starts_with(prefix);
			});
	}
} // namespace Engine
