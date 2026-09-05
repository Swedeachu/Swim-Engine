# Deprecated shader sources

This directory is an archive. **Nothing here is compiled, copied, referenced, or
shipped.** It exists only so the pre-Slang shader sources remain readable while
the Slang ports settle.

## What this is

The handwritten HLSL (Vulkan) and GLSL (OpenGL) shaders Swim used before the
engine moved to Slang as the single first-party shader language:

```text
Deprecated/Shaders/
  Vulkan/
    VertexShaders/*.hlsl      compiled by DXC to SPIR-V
    FragmentShaders/*.hlsl
    ComputeShaders/*.hlsl
  OpenGL/
    *.glsl                    loaded as source text at runtime
```

Each file has a `.slang` successor of the same name under `Source/Shaders/`.

## Why it is out of the source tree

`Source/Shaders` is globbed by the build. Leaving retired sources beside their
replacements invites a stale file being compiled, shipped, or edited by mistake,
and makes "which one is real?" a question a reader has to answer. Moving the
archive outside `Source/` makes the answer structural instead of conventional:
no CMake glob reaches this directory, and `scripts/verify-build-layout.py` fails
the build if a `.hlsl` or `.glsl` file reappears under `Source/Shaders`.

## Current pipeline

First-party shaders are Slang, compiled by the pinned `slangc` SDK through
`cmake/SlangShaders.cmake` into SPIR-V (Vulkan) or GLSL (legacy OpenGL), with
reflection JSON emitted alongside every artifact:

```text
Source/Shaders/Vulkan/{Vertex,Fragment,Compute}Shaders/*.slang -> *.spv  + *.reflection.json
Source/Shaders/OpenGL/*.slang                                  -> *.glsl + *.reflection.json
```

See `docs/SwimEngineArchitectureImplementationPlan.md` (Phase 7) for the shader
system contract.

## Deleting this directory

Safe once the Slang ports are confirmed good in both backends. Nothing in the
build, the runtime, or the tests depends on it.
