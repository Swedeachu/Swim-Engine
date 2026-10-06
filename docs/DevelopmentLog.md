# Swim Engine development log

- **2026-10-06:** Add cached, culled, LOD'd planar reflections (shared coplanar captures, SSR handoff) for the sandbox mirror cube and chrome spheres; make every retained-UI control a self-registering `UiControlBehavior` in its own file with value bindings so gameplay never tracks widget state; and replace per-frame behaviour snapshots with a zero-allocation per-phase scheduler while trimming other hot tick and frame paths.
