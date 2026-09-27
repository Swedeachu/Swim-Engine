#include "Game/Findings.h"

#include <array>

namespace Game
{
	namespace
	{
		constexpr std::array<Finding, 54> Items{ {
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
			{ "KTX2/Basis textures could not be uploaded",
				"TextureResidency uploads uncompressed native mip chains only, so KHR_texture_basisu textures fell back to white. The "
				"cooker now transcodes Basis Universal KTX2 (ETC1S/BasisLZ and UASTC) into RGBA8 mip chains with the Basis transcoder "
				"(compiler-only; the runtime links none), and the sandbox loads the Draco + KTX2 Sponza GLB. Keeping the textures "
				"block-compressed (BC7) on the GPU needs block-aware uploads in the RHI and residency.",
				"Workaround" },
			{ "PhysX contacts of destroyed bodies crashed the step",
				"After a body is destroyed, PhysX still reports its lost-touch pairs with the released actor flagged as removed; "
				"resolving that actor (a virtual call) was an access violation once the sandbox's balls expired. Removed actors and "
				"shapes are skipped now, and the shared physics contract destroys a resting body to cover it.",
				"Fixed" },
			{ "Swapchain rebuild before the first frame",
				"Windows can request a resize before anything was submitted; the RHI requires a same-device retirement timeline "
				"for swapchain replacement and the frame failed. RenderDevice now passes an already-signalled timeline then.",
				"Fixed" },
			{ "Sandbox colliders were scaled twice",
				"The physics bridge multiplies collider sizes by the Transform's world scale, but the sandbox passed world-unit sizes "
				"to scaled entities: balls rested half sunk into the floor, stacked boxes overlapped and the ramp collider was nearly "
				"flat. Collider sizes are now mesh-local (radius 0.5, half extents 0.5 for the unit meshes) and a test pins a scaled "
				"sphere resting exactly on the floor.",
				"Fixed" },
			{ "Fired balls dropped straight down",
				"Adding the Rigidbody component creates the physics body at once, so SetInitialLinearVelocity called afterwards (the "
				"order BallShooter and ball rain use) was never applied. The bridge now applies pending initial velocities to existing "
				"bodies before the next step.",
				"Fixed" },
			{ "Unlit screen tiles among many lights",
				"Cluster light lists were capped at MaxLightsPerCluster and the rest dropped in index order, so over a dense swarm "
				"(especially seen from afar, where one cluster covers many lights) neighbouring clusters kept different subsets and "
				"showed as square, darker tiles (an overflowing 2^20 index pool made it worse). Clusters now keep a bitmask over every "
				"local light plus occupancy words: nothing is ever truncated, memory is bounded by clusters x lights / 8 bytes, and "
				"MaxLightsPerCluster is only the heatmap scale.",
				"Fixed" },
			{ "Jagged shadows and cascade pops",
				"Shadows compared a point-sampled 3x3 texel box (stair-stepped edges) and switched cascades abruptly. Sampling is now "
				"bilinear-weighted PCF (the box slides continuously over the texels), cascades cross-fade over the far 20 % of their "
				"range and the last one fades out, and cascades are 2048 texels over 70 m.",
				"Fixed" },
			{ "Forward+ shaded hidden surfaces",
				"The opaque pass rasterised both faces and discarded back faces in the shader, which disabled early depth testing, so "
				"every overlapping surface ran the full clustered lighting loop. A depth prepass now lays down the nearest depth and "
				"the shading pass tests it with early fragment tests and no depth writes, so each pixel is lit once.",
				"Fixed" },
			{ "Development assets were copied next to the executable",
				"Every engine build copied Assets/ beside the executable and the engine cooked into that copy, while the build "
				"scripts and SwimAssetCooker cooked the repository's Assets/Cooked: two caches, copies that only refreshed when the "
				"engine relinked, and deleted sources that lingered in the copy. Development builds now read and cook the repository's "
				"Assets/ directly (SWIM_DEVELOPMENT_ASSET_ROOT, --assets overrides it), cook/load errors and the cooked models found "
				"are logged, and SWIM_DEPLOY_ASSETS copies assets only for packaged runs.",
				"Fixed" },
			{ "CPU and GPU frames ran back to back",
				"FrameRenderer::BeginFrame waited for the previous frame before gameplay, physics and extraction ran, so a frame took "
				"about CPU + GPU time. The wait now happens right before the next frame is recorded, overlapping the game update "
				"with GPU work; recording is still serialized with the GPU (one submission in flight per executor). Two executors "
				"used alternately are the next step (docs/PerformanceAnalysis.md).",
				"Workaround" },
			{ "Ground collider shorter than the floor",
				"The checkered plane is 130 m square but its box collider had 40 m half extents, so bodies fell through the outer 25 m "
				"of every edge (including under Sponza). Both now come from one GroundSize, and a test drops balls near all four edges.",
				"Fixed" },
			{ "Particles erased near the horizon",
				"Particles were drawn into the Forward+ colour before the screen-space composite and the cloud feature: the fog "
				"re-fogged them with the depth behind them and the clouds were composited over them wherever sky showed through. They "
				"are now drawn over the composited colour, after the BeforeTemporal features (and are not fogged).",
				"Fixed" },
			{ "Sponza missing on a fresh checkout",
				"The Draco + KTX2 Sponza GLB had been dropped from Assets/Models/Sponza/ in a commit, so only the light swarm appeared. "
				"It is restored; verify-build-layout.py fails when it is missing and when an include's case does not match the file.",
				"Fixed" },
			{ "SSR edges aliased to the march stride",
				"A ray near a silhouette hit or missed depending on where its samples landed, so reflected edges were stair-stepped and "
				"TAA smeared them. Crossings between samples are now refined by bisection, and the hit colour is sampled at the exact "
				"hit position (bilinear, luminance-weighted).",
				"Fixed" },
			{ "SSR grain and flicker",
				"Rays toward the camera spread their steps over a projected line far off screen, leaving one or two samples on screen, "
				"and self-intersections and silhouettes the ray passed behind counted as hits, so hits came and went with the per-frame "
				"jitter. The march now covers only the on-screen segment, ignores the start pixel's tangent plane, rejects "
				"discontinuous grazing hits and fades edge-on hits: 2.5 % of hit pixels flicker in the stability test, down from 17 %.",
				"Fixed" },
			{ "Chrome balls showed a fuzzy blue inner ball",
				"Reflection rays that ended beyond the 20 m SSR range or left the screen fell back to the environment, whose ground was "
				"sea blue, and the distance fade blurred the boundary. The sandbox traces 70 m rays and its environment ground is sand, "
				"the colour of the floor.",
				"Fixed" },
			{ "Dark UI displayed as grey",
				"UI colours are linear and encoded to sRGB at composition, but the theme palette and the sandbox panels were written "
				"as sRGB numbers, so every near-black showed as mid grey and the accent was washed out. Colours are now authored "
				"with UiSrgb / UiSrgbHex; panels are near-opaque and the accent borders and slider track are thicker.",
				"Fixed" },
			{ "No reflections of reflections",
				"Reflection rays read the current frame's colour, which has no reflections yet, so chrome balls showed each other "
				"without their own reflections. With TAA on, hits now read the previous resolved frame at the position the hit's "
				"motion vector gives (TemporalAntiAliasing::ImportPreviousOutput).",
				"Fixed" },
			{ "Sky below the horizon grey or brown",
				"The procedural sky's ground colour served both the visible void and the reflection fallback, and its gentle "
				"horizon-to-ground blend left reflections of the floor sky-blue. RenderSettings::SkyBackgroundGround draws the void "
				"blue while ProceduralSky::GroundFalloff gives lighting and reflections a floor-like ground right under the horizon.",
				"Fixed" },
			{ "Fuzzy, fading reflections on flat glossy faces",
				"Rough metals got mirror-sharp reflections whose hits moved with a per-frame jitter (grain TAA could not resolve), and "
				"hits on surfaces seen at a shallow angle (the floor, in most of what a wall reflects) faded out over a wide facing "
				"range. The jitter is now fixed per pixel, the composite softens reflections by roughness over same-surface taps, "
				"and only surfaces seen nearly edge-on fade.",
				"Fixed" },
			{ "Cooked asset validation is slow in Debug",
				"Every start re-hashes every cooked .sasset to decide whether it is current; in Debug builds that takes minutes for "
				"Sponza's RGBA8 textures. Recording file sizes and times beside the hashes would skip unchanged files.",
				"Open" },
			{ "Render surfaces are not mirrored yet",
				"UiCanvas supports screen overlays, world panels and billboards; RenderSurface canvases (UI rendered into a texture "
				"sampled by a material) exist in the UI renderer but the runtime does not route them yet.",
				"Open" },
			{ "Occlusion culling is not enabled",
				"The runtime uses single-phase GPU frustum culling; the two-phase HZB occlusion path (HzbBuilder) is built and tested "
				"but not wired into the frame yet.",
				"Open" },
			{ "Desktop validation is partial",
				"The runtime is built and captured on Linux with SwiftShader and on Windows on the RTX 4070 (the full suite, the "
				"validated screen-space and particle smokes, and 1080p headless captures). The four validation profiles over every "
				"smoke and the 1080p pass budgets are still to be recorded.",
				"Open" },
			{ "No HDR output toggle or device-loss recovery",
				"The swapchain is SDR (BGRA8; vsync off by default, --vsync=on for FIFO). The post stack can tone map to HDR10/scRGB and "
				"the RHI supports HDR swapchains, "
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
			{ "Reflections fell back to a floor-coloured sky",
				"Screen-space reflection misses used the global environment with a floor-albedo ground colour, so chrome showed a sandy "
				"blur below the horizon, touching spheres showed dark blobs and objects beside a mirror reflected sliced. Reflections are "
				"now a hierarchy: SSR (with back-face thickness and hidden-side rejection) -> time-sliced, parallax-corrected local probes "
				"captured by the Forward+ pipeline -> the global environment with clouds.",
				"Fixed" },
			{ "Probe parallax stopped at occluder silhouettes",
				"The probe march took the first sample beyond the captured distance as the hit, so rays passing behind a nearby object "
				"(as the probe saw it) stopped at its silhouette and reflections beside a mirror smeared in bands, one per march step. A "
				"crossing now counts only when the refined point lies on the captured surface, and the march offset is jittered per "
				"pixel for the temporal resolve.",
				"Fixed" },
			{ "One procedural sky, no HDR environment maps",
				"The global environment is built from the procedural sky with the volumetric clouds folded in; local reflection probes "
				"now cover nearby geometry, but loading HDR environment maps waits for Phase 24.",
				"Open" },
			{ "Probe reflections are limited by what the probe sees",
				"A probe stores one distance per direction, so a reflected surface hidden from the probe's centre (behind another "
				"object) cannot be found; the march then falls back to the ray direction. Probes are 128 px per face, so sharp mirrors "
				"show soft probe reflections where SSR has no data (off screen, behind the camera).",
				"Open" },
			{ "Depth of field is physically weak at wide angles",
				"The thin-lens CoC follows the camera's field of view (60 degrees is a 21 mm lens on full frame), so the sandbox view "
				"shows little blur even at f/1.4; narrow the field of view in the Camera/Post tab for shallow focus. Autofocus reads the "
				"centre of the depth buffer each frame without smoothing.",
				"Open" },
			{ "Black-hole lensing sampled the frame with the thin-lens formula",
				"The first lensing pass bent the rendered frame by the weak-field angle and lensed a rasterized disk, which sliced "
				"the disk where its depth crossed the lens plane and drew the horizon sphere into the Einstein ring. Each pixel's ray is "
				"now traced as a Schwarzschild geodesic through the hole's region, crossing a volumetric gas instead of a textured disk; "
				"only the escaped ray's background comes from the frame.",
				"Fixed" },
			{ "Black-hole image cut into slices near the floor",
				"The lensed background was looked up at the depth the pixel's unbent ray had, so where the bent ray and that depth "
				"disagreed (near the floor, across walls behind the hole) the image was cut into slices and bands. Each bent ray is now "
				"tested against the depth buffer along its whole path, and on in a straight line after it leaves the region.",
				"Fixed" },
			{ "Black-hole backgrounds are screen space",
				"The lensed image is the rendered frame: what is off screen or hidden behind nearer surfaces cannot be seen through "
				"the lens (a ray that leaves the screen fades to the pixel's own colour, one bent behind the camera keeps the last "
				"surface it crossed).",
				"Open" },
			{ "240 FPS not reached yet",
				"Every pass and feature can be switched off, profiled and benchmarked (docs/Profiling.md). Half-resolution reflections "
				"and AO, deferred local lights, the colour-only sky pass, faster cluster masks and the cascade cache took the Sponza "
				"atrium from 14 to 10 ms and the overview from 7.3 to 6.7 ms on the RTX 4070 laptop at 1080p; the frame is GPU-bound "
				"(shadows, local lights, the reflection composite, lensing and clouds lead).",
				"Open" },
		} };
	} // namespace

	std::span<const Finding> GetFindings()
	{
		return Items;
	}
} // namespace Game
