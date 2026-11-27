# RHI contract

The RHI (`Source/Engine/Systems/Renderer/RHI`) is the only graphics API the renderer, the render graph and render features see. Each backend under `RHI/Backends/<Api>` implements it. Nothing above `Backends/` includes a backend header, except:

- `Runtime/RenderDevice.cpp`, which registers the built-in backends with `GraphicsFactory`;
- the backend's own test suites.

```text
RHI/
  RhiTypes.h            formats, usages, resource states, descriptor schemas, capabilities, GraphicsApi
  RhiContracts.h        the object model: GraphicsSystem > Adapter > Device > resources, pipelines,
                        tables, command pools/lists, queues, sync, swapchain
  RhiFactory.h          GraphicsFactory: GraphicsApi -> GraphicsSystem creation function
  Rhi*.h                backend-neutral helpers built on the contracts (upload/readback arenas,
                        frame lifetime, timestamps, pipeline-cache blobs, diagnostics)
  Backends/Vulkan       the Vulkan backend
  Backends/D3D12        the D3D12 backend (in progress)
```

## Selection

`RenderDeviceDesc::Api` picks the backend. The engine sets it from `--graphics=vulkan|d3d12`, or the same option spelled `--rhi=`. Vulkan is the default.

`RenderDevice` creates the requested backend's graphics system, chooses an adapter, then creates the device. If any step fails for a backend other than Vulkan, it logs `[Render] <Api> is unavailable (<reason>); falling back to Vulkan` and starts Vulkan instead. A backend that is not built into the engine counts as a failure.

`AdapterInfo::Api` reports which backend a device came from. Code that keeps per-backend files keys them by it:

- shader bytecode, through `ShaderBytecodeExtension`;
- the pipeline cache, through `PipelineCacheFileName`.

## Object model

| RHI | Contract | Vulkan | D3D12 |
|---|---|---|---|
| `GraphicsSystem` | enumerates adapters, owns validation and the diagnostic log | instance, debug messenger | DXGI factory, debug layer, info queue |
| `Adapter` | `AdapterInfo` (vendor, LUID, discrete/integrated, `GraphicsCapabilities`) | physical device | `IDXGIAdapter4` |
| `Device` | creates everything below; pipeline-cache blob in/out; memory budget; loss reporting | `VkDevice` + VMA | `ID3D12Device` + placed heaps |
| `Queue` (Graphics, Compute, Transfer) | `Submit` with timeline waits/signals; `WaitIdle` also covers presents issued through it | queue families | direct, compute and copy queues |
| `Timeline` | monotonically increasing 64-bit value, CPU wait, GPU wait/signal at submit | timeline semaphore | `ID3D12Fence` |
| `Semaphore` (binary) | only for swapchain acquire and present ordering | binary semaphore | a fence value on the direct queue; acquire is implicit |
| `Fence` | CPU-visible completion of one submission | `VkFence` | fence value + event |
| `Buffer`, `Texture`, `TextureView`, `Sampler` | described by `*Desc`; views carry dimension, format, mip/layer range, aspect | images, image views | committed or placed resources; descriptors are written into heaps at table-write time |
| `ShaderProgram` | stage bytecode + `ShaderProgramInterface` (descriptor schemas per space, push-constant ranges, compute group size) | shader modules | DXIL blobs |
| `PipelineLayout` | the program's spaces, optionally replaced by explicit schemas (the shared bindless space) | descriptor set layouts + push-constant ranges | root signature: one descriptor table per space, root constants for the push range |
| `DescriptorTable` | one space's bindings, written with `DescriptorWrite`; `PartiallyBound` and `UpdateAfterBind` for bindless elements | descriptor set | a range in the shader-visible CBV/SRV/UAV heap, plus one in the sampler heap |
| `GraphicsPipeline`, `ComputePipeline` | full state, dynamic-rendering formats, vertex layout | pipelines + `VkPipelineCache` | PSOs + `ID3D12PipelineLibrary` |
| `CommandList` | transitions, copies, `BeginRendering`/`EndRendering`, binds, push constants, draws (direct and indirect, with GPU counts), dispatch, timestamps, debug labels | command buffer, dynamic rendering, sync2 barriers | graphics command list; render passes or OM targets; enhanced or legacy barriers; `ExecuteIndirect` |
| `QueryPool` | timestamps with `TimestampInfo` for tick conversion | query pool | query heap + readback buffer |
| `Swapchain` | acquire / present / resize / present mode; HDR colour spaces | `VkSwapchainKHR` | DXGI flip-model swapchain, tearing allowed for `Immediate` |

### Rules both backends keep

- **Resource states.** States are `ResourceState` flags, never layouts. Storage access is `ShaderRead | ShaderWrite`, and a dependent storage pass needs a barrier even without a state change. `Transitions` batches several barriers into one dependency.
- **Rendering.** There are no render passes or subpasses. `BeginRendering` takes the attachment views, load/store ops and clear values. Pipelines are created against attachment formats.
- **Descriptors.** Bindings are addressed by `(space, binding)`, the Slang `[[vk::binding(b, s)]]` pair. The backend maps them to its own model:
  - D3D12 register assignment comes from the program's DXIL reflection, matched to the interface by parameter name.
  - Space 1 is the shared bindless space (`BindlessResourceTable`).
- **Bound descriptor tables.** Both backends track bound tables per bind point (graphics, compute) with `Rhi::DescriptorBindingState` (`RhiDescriptorBindings.h`), so a command sequence succeeds or throws the same way on each. The rules follow Vulkan's *Pipeline Layout Compatibility*:
  - Two layouts are compatible for space N when spaces 0..N are defined identically and their push-constant ranges are identical.
  - `BindDescriptorTable` needs a bound pipeline. It applies to that pipeline's bind point, and the table must be made for that space with the same definition as the active layout's; otherwise it throws `std::invalid_argument`.
  - The table must also be complete: every element written, except those of `PartiallyBound` bindings (bindless arrays). Otherwise binding throws `std::invalid_argument`.
  - Binding a pipeline unbinds nothing. A table stays bound across pipeline switches, including switches to the other bind point and back.
  - A draw or dispatch throws `std::logic_error` unless every space its layout reflects has a table bound with a layout compatible for that space.
  - Binding a table to space N under layout L unbinds lower spaces M bound under a layout not compatible with L for M. It also unbinds higher spaces bound under a layout not compatible with L for N.
  - D3D12 changes root signatures natively. The backend sets the tracked tables again as root arguments when the layout changes.
- **Push constants.** These are one small range per stage set (`MaxPushConstantBytes`, at least 128). D3D12 maps them to root constants.
- **Releasing resources.** Destroying a buffer, texture or texture view never frees GPU memory that submitted work may still use. Each backend keeps a deferred-release queue keyed to the GPU timeline:
  - Every queue signals a progress fence (D3D12) or timeline semaphore (Vulkan) of its own on each submission.
  - A release records what each queue has submitted, plus the graphics queue's next submission, since the frame being recorded may also use the resource. The memory goes back once the GPU has passed all of it.
  - Each submission and each queue or device idle wait collects the releases the GPU has passed. A release made with nothing in flight frees at once.
  - Owners may release mid-frame without tracking frame completion themselves. They still keep their own bindless slots and descriptors until the GPU has passed the frames that used them: a descriptor rewritten too early is read by those frames.
  - Images a backend does not own (swapchain images) are released at once; the swapchain controls their lifetime.
  - D3D12 also keeps an empty placed-resource heap until the GPU has passed the work that could still touch it. `SWIM_D3D12_COMMITTED=1` makes every resource committed, as a diagnostic switch.
- **Synchronisation.** Cross-queue and CPU-GPU ordering uses `Timeline` points. Binary semaphores exist only for the swapchain.
- **Indirect arguments.** `DrawIndexedIndirectCommand` (20 bytes) and `DrawIndirectCommand` (16 bytes) match both APIs. Draws read their record index through the first instance (see *Shader portability*).
- **Capabilities.** Optional features are gated by `GraphicsCapabilities` flags, never by API checks in renderer code. The flags include:
  - mesh/task shaders;
  - ray query, ray-tracing pipelines;
  - indirect count, subgroup (wave) operations;
  - bindless descriptors;
  - extended storage formats (typed UAV loads);
  - sampled cube arrays;
  - BC/ETC2/ASTC;
  - HDR swapchain, memory budget.

## Shaders

Every runtime program is compiled from Slang at build time:

- `<Name>.spv`: SPIR-V 1.5.
- `<Name>.dxil`: shader model 6.8 by default (`SWIM_SLANG_DXIL_PROFILE`, 6.6 at the lowest). Slang drives the DXC from the Windows SDK, so no extra download is needed. The DXIL build is on when CMake finds `dxcompiler.dll` and `dxil.dll` in the Windows SDK.

Both sit beside `<Name>.reflection.json`. The SPIR-V reflection is the RHI interface for both backends. The DXIL build adds `<Name>.dxil.reflection.json`, the register layout D3D12 needs, and `ShaderLibrary` loads the bytecode for its device's API.

`cmake/SlangDxil.cmake` builds one program's DXIL:

1. A `-target hlsl` pass writes the register reflection.
2. One `-target dxil` compile per entry point. A multi-entry compile would produce a DXIL library, which pipelines cannot use.
3. The per-entry containers are concatenated. Every DXIL container records its own size, so the backend finds each stage by its shader kind.
4. The HLSL text is scanned for `GetDimensions` calls on structured buffers. Their registers go into the register reflection as `nativeSizeQueries` (see *Root descriptors*). A buffer passed to a function is matched by element type, so every global buffer of that type is listed.

### D3D12 bindings

- **Registers.** DXIL ignores `[[vk::binding]]`; Slang assigns registers per class, and unbounded arrays get their own register spaces. `ShaderCompiler::BuildNativeBindings` matches each `(space, binding)` of the SPIR-V reflection to its register by parameter name. The resulting `NativeBinding` list travels in `ShaderProgramInterface::NativeBindings`.
- **Entry-point parameters.** The HLSL reflection gives entry-point parameter registers relative to the entry point. In each compiled stage they follow the space-0 registers of the global parameters, per register class, so `BuildNativeBindings` adds the end of the globals' range.
- **Root signature.** The layout is:
  1. the push-constant block as root constants;
  2. for graphics, the draw base (2 root constants at `b0, space1000`);
  3. single structured buffers as root SRVs/UAVs (see below);
  4. per descriptor space, one view table and one sampler table, with slots in binding order.
- **Root descriptors.** A structured buffer view addresses whole elements (`FirstElement`), but the RHI allows any 16-byte buffer offset, and graph uploads use exactly that. Each single (non-array) structured buffer therefore becomes a root SRV or UAV, which takes a byte address. The descriptor is still written into the table as well. The root signature stays within 64 DWORDs; budget: two per root descriptor, one per table, two tables per space. Buffers past the budget keep their table range.
  - Root descriptors carry no size, so `GetDimensions` on one is undefined. Buffers listed in `nativeSizeQueries` (`NativeBinding::SizeQueried`) keep their table range, and must be bound at offsets that are a multiple of their stride.
- **Pipeline layout interface.** `PipelineLayout::GetInterface` returns the program interface with the explicit descriptor spaces in place of the reflected ones, as on Vulkan.
- **Reserved spaces.** Register spaces 999 (push constants that have no reflected register) and 1000 (the draw base) are reserved. Shaders must not use them.
- **Descriptor tables.** Each table owns contiguous ranges of the shader-visible view and sampler heaps. A table binds only once every element of its non-partially-bound bindings is written, as on Vulkan. As a D3D12 safety measure, not a behaviour difference, the elements of partially bound bindings (bindless arrays) start as null descriptors. D3D12 leaves reading an uninitialised descriptor undefined, and it can hang the GPU, so a shader index past what was written reads zeros instead. The engine's bindless table writes its fallback back into an element before reusing it.
- **Single-layer views.** D3D12's non-array SRV and UAV dimensions cannot select a slice. A 1D, 2D or storage view of a later layer, or of any layer of an array texture, is written as the array dimension with one slice. A cube view with a base layer is written as a one-cube cube array. The shader's non-array declaration reads that slice as its first layer.
  - GPU-based validation checks such a storage (UAV) view by the shader's declaration, so it checks layer 0 of that mip. When layer 0 is in another layout, it reports an incompatible layout (D3D12 message 1358) although the write lands on the right layer. RHI.D3D12.Smoke.TypedStorageTexturesAndSubresourceReadback hits this under `SWIM_RHI_VALIDATION=all` only.
- **Vertex inputs.** Vertex attributes reach the input layout through `NativeVertexInputs`: semantic names by location, from the vertex entry point's register reflection.

### Draw ids

Vulkan's vertex and instance ids include the draw's first vertex and first instance, and GPU-driven draws use the first instance as their record index. `Resources/DrawIndex.slang` gives both targets the same values:

- Vertex entry points take `uint vertexIdInput : SWIM_VERTEX_INDEX, uint instanceInput : SWIM_INSTANCE_INDEX SWIM_DRAW_BASE_PARAMETERS`.
- They then read `DrawVertexIndex(vertexIdInput)` and `DrawInstanceIndex(instanceInput)`.
- On DXIL at shader model 6.8, these add `SV_StartVertexLocation` / `SV_StartInstanceLocation`, which indirect draws also set.
- Below 6.8, the build defines `SWIM_DRAW_BASE_ROOT_CONSTANTS` and the backend sets the draw base as root constants at each direct draw.

`PyroVolumes` reads its parameters as a `StructuredBuffer<float4>`, because DXIL limits structured elements to 2048 bytes. The bytes the CPU uploads are unchanged.

All runtime programs compile for both targets.

Development hot reload on D3D12 builds DXIL the same way the build does:

- It runs the SPIR-V compile, whose reflection is the interface.
- It runs the HLSL register pass with the size-query scan, then one DXIL compile per entry point.
- `DevPaths.cfg` gives it `DxcDirectory` and `DxilProfile`.

### D3D12 status

The backend needs no Agility SDK. On the in-box runtime, current desktop drivers report all of these:

- feature level 12_2 and shader model 6.8;
- resource binding tier 3;
- enhanced barriers;
- mesh shader tier 1 and raytracing tier 1.1;
- work graphs 1.0.

The GPU smokes run only with `SWIM_RUN_RHI_SMOKE=1`. Each smoke function is registered for both backends by `Swim::Testing::AddSmoke` (`Tests/Fixtures/SmokeBackend.h`):

- as `RHI.Vulkan.Smoke.<Name>`, with the Vulkan advisory filtering;
- as `RHI.D3D12.Smoke.<Name>`. This copy is strict: any debug-layer warning, error or dropped message fails it.

The D3D12 copy loads `.dxil` in place of `.spv` and adds each program's native bindings and vertex inputs. It runs the same checks against the same CPU references. Every smoke passes on both backends at the default profile, including the render graph, the GPU-driven, Forward+, shadow, screen-space, temporal, particle, skinning and UI feature passes, and the window, HDR and memory-budget cases. Under `SWIM_RHI_VALIDATION=all`, only the GPU-based-validation report described under *Single-layer views* remains.

Backend behaviour the smokes pin:

- **Swapchain.** It is created dormant while the window is minimized (no images, `Acquire` returns `Suspended`) and built on the first resize with a client area. HDR pairs (RGB10A2 with HDR10 ST.2084, RGBA16F with scRGB) are offered only while the window's output is in ST.2084 mode. Formats are picked by the same rules as on Vulkan and picked again at every resize.
- **Memory budget.** Live allocations and bytes are counted per segment (local, and non-local unless the adapter is UMA). Every resource is committed, so blocks equal allocations.
- **Empty dispatches.** A dispatch with a zero dimension is skipped, as Vulkan treats it as a no-op.
- **Block-compressed copies.** Copy boxes of block-compressed formats are rounded up to whole blocks.

Not done yet:

- the placed-resource transient pool (every resource is committed);
- in-client parity runs (same image, clean debug layer, comparable frame rate).

## Audit summary (phase 1)

The public RHI headers were already backend-neutral:

- no Vulkan types or handles;
- dynamic rendering instead of render passes;
- timeline points for queue sync;
- descriptor tables per space;
- `ResourceState` flags instead of layouts.

What leaked, and where it went:

| Leak | Resolution |
|---|---|
| `RenderDevice` created the Vulkan graphics system directly | goes through `GraphicsFactory` with `RenderDeviceDesc::Api` and Vulkan fallback |
| The engine refused every backend but Vulkan at startup | D3D12 is accepted; `--rhi=` alias |
| `ShaderLibrary` hard-coded `.spv` (and probed `ForwardOpaque.spv`) | `ShaderBytecodeExtension(AdapterInfo::Api)`; probes the reflection sidecar |
| One `pipelines.bin` for every backend | `PipelineCacheFileName` keys the file by backend |
| Contract comments in Vulkan terms (General layout, queue families) | restated as RHI states and queue types |
| A separate Vulkan presentation queue was not covered by `Queue::WaitIdle` (present semaphores destroyed in use) | `WaitIdle` on the graphics queue also idles the presentation queue |

Still Vulkan-specific, and owned by its backend or platform layer:

- `Platform/Internal/VulkanWsi` and `WindowGraphicsSupport::Vulkan`. A D3D12 swapchain needs only the window handle. Windows keep Vulkan support so the fallback can start.
- KTX2 `vkFormat` codes. These belong to the container format, not the API.
