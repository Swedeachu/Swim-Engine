#pragma once

namespace Engine
{
	class FrameProfiler;
	class FrameRenderer;
	class MaterialLibrary;
	class MeshLibrary;
	class RenderToggles;
	class SceneRenderBridge;
	class UiRuntime;
	struct RenderSettings;
	struct RenderStats;

	// What scenes and behaviours reach of the renderer (Scene::GetRenderServices()).
	// Every pointer is null in headless tests without a GPU; check before use.
	struct RenderServices
	{
		FrameRenderer* Renderer = nullptr;
		MeshLibrary* Meshes = nullptr;		  // Procedural/cooked meshes and textures.
		MaterialLibrary* Materials = nullptr; // Material sets for MeshRenderer parts.
		RenderSettings* Settings = nullptr;	  // Live render switches (the sandbox panel edits them).
		const RenderStats* Stats = nullptr;	  // Last frame's diagnostics.
		SceneRenderBridge* Bridge = nullptr;  // Skinned meshes, extraction stats.
		UiRuntime* Ui = nullptr;			  // Theme, fonts, the UI input frame.
		RenderToggles* Toggles = nullptr;	  // Profiling switches (every pass, feature and scene part).
		FrameProfiler* Profiler = nullptr;	  // Per-frame CPU zones and GPU passes (the `profile` command).

		bool HasRenderer() const { return Renderer && Meshes && Materials && Settings; }
	};
} // namespace Engine
