# No-IndirectCount fallback validation — September 23, 2026

This record covers the Phase 13 requirement to have a fallback path only for capabilities that genuinely require it. Devices without `GraphicsCapabilities::IndirectCount` draw GPU visibility bins with `DrawIndexedIndirect` over zero-filled command buffers; every other device keeps `DrawIndexedIndirectCount`. The contract is in [GPU visibility](../GpuVisibility.md#drawing-the-bins-and-the-no-count-fallback).

## Changes

- **`Renderer/Visibility/VisibilityDraws.h/.cpp`:**
  - `VisibilityDrawPath` (`IndirectCount`, `ZeroFilledIndirect`);
  - `SelectVisibilityDrawPath` and `NeedsZeroedCommands`;
  - `DrawVisibilityBin`, which issues one bin on either path.
- **`VisibilityFrameDesc::ZeroUnusedCommands`:** the clear pass also zeroes the command buffer, the cull declares it `ReadWrite`, and the command buffer gained `TransferDestination` usage.
- **Native smoke:** `RHI.Vulkan.Smoke.GpuVisibilityCullsBinsAndDrawsIndirect` now draws through `DrawVisibilityBin`. Its fourth frame uses the fallback and additionally requires every unwritten slot of every bin to be a zero-instance, zero-index command.

## Results

| Gate | Cases | Checks | Result |
| --- | ---: | ---: | --- |
| Official Linux configuration | 486 | 11,266 | Pass |
| Previous baseline (items 50–51) | 485 | 11,248 | Pass |
| New case `Render.GpuVisibility.NoIndirectCountFallbackZeroesCommandsAndDrawsWholeBins` | 1 | — | Pass |
| `SwimRenderResourcesPublicHeaders` (now with `VisibilityDraws.h`), `scripts/verify-build-layout.py` | — | — | Pass |
| Native smoke with the fallback frame | 1 | — | Compiles; stops at SDL platform init in the container (no GPU). **Not executed.** |

## Desktop steps still required

Run the four validation profiles. Expect 28 native cases per profile, the same count as before.
