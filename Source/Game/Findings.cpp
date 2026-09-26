#include "Game/Findings.h"

#include <array>

namespace Game
{
	namespace
	{
		constexpr std::array<Finding, 29> Items{ {
			{ "Behaviour Exit ran twice on scene exit",
				"InternalSceneExit called every behaviour's Exit and then DestroyAllEntities called it again. Scene exit now destroys the "
				"entities once, after the scene's own Exit, so every behaviour exits exactly once.",
				"Fixed" },
			{ "Cached component pointers dangled",
				"Behaviours cached their Transform pointer; EnTT moves components when storage grows, so the pointer could dangle. "
				"Behavior::GetTransform now looks the component up on every call.",
				"Fixed" },
			{ "Single step did nothing for gameplay",
				"Stepping while paused advanced physics but Playing-only behaviours stayed idle. Scenes now run a stepped frame under "
				"the Playing execution state (Scene::GetExecutionState).",
				"Fixed" },
			{ "Stop kept the played-out scene",
				"Stop now resets the active scene to its initial state at the start of the next frame (deferred, because Stop is often "
				"requested from a UI callback inside the scene update); Play starts from that fresh state.",
				"Fixed" },
			{ "Headless runs had no Vulkan loader",
				"The headless platform skipped SDL's video subsystem, which also provides the Vulkan loader. Headless now initialises "
				"video with SDL's offscreen driver (no display needed) and falls back to events only.",
				"Fixed" },
			{ "Forward+ loads every target without Clear",
				"With ForwardPlusTargets::Clear = false the opaque pass loads all seven colour targets and depth, so the sky pass that "
				"runs first clears all of them in the same render pass.",
				"Workaround" },
			{ "Page slots must match exactly",
				"Forward+ and shadows require the visibility bins to cover exactly the frame's page slots, so the runtime rebuilds its "
				"visibility instances whenever the number of GeometryHeap index pages in use changes.",
				"Workaround" },
			{ "Sky constants exceeded 128 bytes",
				"An inverse view-projection plus the sky parameters did not fit the guaranteed push-constant budget; the sky pass now "
				"passes the camera's frustum basis (three vectors) instead.",
				"Fixed" },
			{ "Push-constant stage masks",
				"Slang reflects push-constant ranges for both graphics stages; writes must name every stage of the range or the RHI "
				"rejects them.",
				"Fixed" },
			{ "Timings before the first frame",
				"RenderGraphExecutor::ReadTimings throws until a graph has executed; the frame renderer reads timings only after a "
				"submission.",
				"Fixed" },
			{ "DrawParameters on software rasterisers",
				"SV_VertexID/SV_InstanceID need the DrawParameters capability, which SwiftShader lacks. The UI, particle, sky and "
				"present shaders now use SV_VulkanVertexID/InstanceID: their draws start at vertex and instance 0, so the values are "
				"identical.",
				"Fixed" },
			{ "SwiftShader draw-indirect-count stub",
				"SwiftShader advertises drawIndirectCount but its vkCmdDrawIndexedIndirectCount draws nothing. The frame renderer "
				"selects the zero-filled DrawIndexedIndirect path on SwiftShader (or with SWIM_FORCE_INDIRECT_FALLBACK=1).",
				"Workaround" },
			{ "EnTT const storage access",
				"In EnTT 3.13, registry.storage<entt::entity>() on a const registry returns a pointer; entity counting now handles it.",
				"Fixed" },
			{ "Billboards behind the camera stopped the engine",
				"A constant-screen-size billboard whose anchor is behind the camera has no valid placement; the UI math throws and "
				"the exception ended the run. UiRuntime now skips such canvases for the frame (not drawn, not interactive).",
				"Fixed" },
			{ "UI bindings reported initial values as changes",
				"The HUD's value/checked/text watchers fired once on their first poll, which snapped the camera back to the first "
				"bookmark after a startup --exec. The first poll now only records the initial state.",
				"Fixed" },
			{ "MSVC rejected a default-initialized initializer_list member",
				"MeshSpawn::Tags was a std::initializer_list with a {} default member initializer: MSVC stops with C2797 (and the "
				"list would dangle after the braced initializer anyway). It is a std::vector now.",
				"Fixed" },
			{ "One material per GPU scene object",
				"Instances carry a single material set and the submesh material slot is not used by visibility or Forward+. "
				"Cooked models are regrouped by material at import (Sponza: 103 primitives become 25 meshes); per-submesh "
				"materials in the GPU scene would remove the regrouping.",
				"Workaround" },
			{ "KTX2/Basis textures cannot be uploaded",
				"TextureResidency uploads uncompressed native mip chains only; supercompressed KTX2 (BasisLZ/UASTC) payloads need "
				"a transcoder (retired with the legacy renderer) or BC encoding in the cooker. Such textures fall back to white.",
				"Open" },
			{ "PhysX contacts of destroyed bodies crashed the step",
				"After a body is destroyed, PhysX still reports its lost-touch pairs with the released actor flagged as removed; "
				"resolving that actor (a virtual call) was an access violation once the sandbox's balls expired. Removed actors and "
				"shapes are skipped now, and the shared physics contract destroys a resting body to cover it.",
				"Fixed" },
			{ "Swapchain rebuild before the first frame",
				"Windows can request a resize before anything was submitted; the RHI requires a same-device retirement timeline "
				"for swapchain replacement and the frame failed. RenderDevice now passes an already-signalled timeline then.",
				"Fixed" },
			{ "Colliders ignore Transform scale",
				"Rigidbody collider sizes are in world units and do not follow the entity's scale; the sandbox sizes every collider to "
				"its mesh explicitly.",
				"Open" },
			{ "Render surfaces are not mirrored yet",
				"UiCanvas supports screen overlays, world panels and billboards; RenderSurface canvases (UI rendered into a texture "
				"sampled by a material) exist in the UI renderer but the runtime does not route them yet.",
				"Open" },
			{ "Occlusion culling is not enabled",
				"The runtime uses single-phase GPU frustum culling; the two-phase HZB occlusion path (HzbBuilder) is built and tested "
				"but not wired into the frame yet.",
				"Open" },
			{ "Only validated on SwiftShader so far",
				"The assembled runtime was built and captured on Linux with SwiftShader (no validation layer available there, about "
				"3.5 s per frame). The four validation profiles, the RTX 4070 run and the 1080p pass budgets are still to be recorded.",
				"Open" },
			{ "No HDR output toggle or device-loss recovery",
				"The swapchain is SDR (BGRA8, vsync). The post stack can tone map to HDR10/scRGB and the RHI supports HDR swapchains, "
				"but the runtime neither offers the toggle nor recreates the device after a loss.",
				"Open" },
			{ "Physics backend and gravity are fixed at startup",
				"--physics selects PhysX or Jolt at launch; switching from the panel would rebuild every body, and PhysicsWorld has no "
				"runtime gravity setter yet.",
				"Open" },
			{ "Playground extras not built",
				"No domino run, trigger volumes or per-body inspector yet; the entity browser shows names, tags and transforms. "
				"Entity context menus and camera bookmarks beyond the five presets are also missing.",
				"Open" },
			{ "Shadow and debug-view controls are partial",
				"The panel toggles shadows and shows the cluster heat map; cascade count, resolution, bias and cascade debug views, "
				"wireframe and overdraw views are not exposed.",
				"Open" },
			{ "One procedural sky, no IBL map selection",
				"The environment is built from the procedural sky (prefiltered cube + irradiance); loading HDR environment maps "
				"from assets waits for Phase 24 asset handling.",
				"Open" },
		} };
	} // namespace

	std::span<const Finding> GetFindings()
	{
		return Items;
	}
} // namespace Game
