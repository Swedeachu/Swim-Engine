# Replay: scene recording, playback and the tool server

A generic recorder for the rendered scene. A recording keeps nodes, assets, events and the camera, so it needs no per-type game code. Playback rebuilds the scene through a resolver, and every frame can be edited. A small MCP tool server lets an agent or script drive cameras, edits and the environment.

| Folder | Contents | Builds into |
| --- | --- | --- |
| `Systems/Replay/` | Types, file format, recorder, tracks, project, player, replay tools | Engine and SwimTests (headless: std plus the animation math) |
| `Systems/Tooling/` | `Json`, `ToolServer` (MCP JSON-RPC) | Engine and SwimTests |
| `Systems/Replay/Runtime/` | `SceneReplaySource`, `SceneReplayPresenter` (entity scene adapters) | Engine |
| `Systems/Tooling/Runtime/` | `HttpToolTransport` (Streamable HTTP), `StdioToolTransport` | Engine |

## Model

A frame is a sorted list of **nodes** plus the **events** since the last frame.

**`NodeState`** fields:
- `Id`
- `Parent`
- `Kind` (an interned name)
- `Visible`
- `Pose` (position, rotation, scale)
- `Assets` (asset ids)
- `Props` (a sorted list of name to `Value`)

A `Value` is one of: bool, int64, float, vec2, vec3, vec4, string or asset id.

The built-in kinds are `mesh`, `light`, `camera`, `emitter` and `sound` (an event). Any other kind is the game's own.

**Assets** are `AssetRecord{Kind, Key, Hash, Data}`. They are stored by a stable **key** when the game can resolve that key at playback. Bytes are cached only when there is no such key, for example procedural meshes or textures changed at runtime. Cached bytes are deduplicated by content hash across keys.

`ReplayRecorderDesc::IsResolvable` decides which assets get cached. The default, `DefaultIsResolvable`, works like this:
- Sounds resolve by key.
- Meshes and textures resolve by key unless the key is empty or starts with `Procedural/`.
- Everything else is cached.

## Recording

```cpp
Swim::Replay::ReplayRecorder recorder;
recorder.AddSource(sceneSource);          // Any IReplayCaptureSource.
recorder.Start("match.swrp", { .KeyframeInterval = 5.0, .Metadata = "{...}" });
// Each frame:
recorder.Capture(time);
recorder.Stop();
```

A source fills `ReplayCapture` with three calls:
- `Node(id)` returns the node's state from the last frame, which the source then updates. Nodes it doesn't touch disappear.
- `Asset(kind, key, bytes)`
- `Event(...)`

`GetSession()` changes on each `Start`, so sources can reset their per-recording asset caches.

`SceneReplaySource` records an entity scene with no game code:

| Scene content | Recorded as |
| --- | --- |
| Each `MeshRenderer` part | A `mesh` node: world pose, assets [mesh, material], `castShadows` |
| `Light` | A `light` node |
| `ParticleEmitter` | An `emitter` node (the desc is an Effect asset) |
| The main camera | Node 1 (`fov`, `near`, `far`) |
| `AudioSystem` plays | `sound` events (`volume`, `pitch`, `positional`) |

More details:
- Node ids are `((entity + 1) << 16) | slot`.
- Materials are stored as a small JSON asset. Their textures are assets of the recording too.
- Procedural meshes and textures are captured from the data they were registered with, through a `MeshLibrary` observer. Create the source before the game registers runtime content.
- `ReplayAnnotation{Kind, Props, Members}` adds an entity as a node of its own kind. Mesh parts below it, or listed in `Members`, get an `owner` prop pointing at that node.
- `ReplayIgnore` leaves out an entity and its descendants.
- First-person overlay parts are skipped by default (`SkipOverlay`).

## File format (`.swrp`)

The file starts with the header `"SWRP"`, a u32 version and a metadata string. Chunks follow, each as `[u8 type][u32 size][payload]`:

| Type | Chunk |
| --- | --- |
| 1 | New names |
| 2 | Asset |
| 3 | Keyframe (every node) |
| 4 | Delta |
| 5 | Index |

- A delta lists the added and changed nodes with a field mask (parent, kind, visible, position, rotation, scale, assets, props). Its props hold only the entries that changed or were removed. It also lists the removed node ids and the events.
- Integers are varints.
- The writer streams from its own thread.
- `Stop` writes an index of frame offsets and keyframes, then a footer.
- A file without a footer, such as one from a crash, is recovered by scanning its chunks (`WasRecovered`).
- Seeking jumps to the nearest keyframe at or before the target and applies deltas from there.

## Playback

```cpp
Swim::Replay::ReplayPlayer player;
player.Open("match.swrp");
player.GetProject().Load("match.project.json"); // Optional saved edits.
Engine::SceneReplayPresenter presenter({ .Scene = &replayScene, .Render = render, .Player = &player,
	.Cameras = &cameras, .Audio = &audio, .Commands = &commands });
presenter.RegisterKind("terrain", terrainPresenter); // Game kinds.
// Each frame:
player.Update(dt);
player.Present(presenter);
```

`ReplayPlayer` does the following:
- **Evaluation:** it interpolates between the two recorded frames around the time. Transforms blend, as do float and vector props.
- **Edits:** it applies the project's edits.
- **Camera:** it evaluates the active camera.
- **Events:** events fire only while rolling forward. Seeks don't fire them.
- **Presenting:** `Present` calls `OnAdd`, `OnUpdate` or `OnRemove` only for nodes that changed. It then calls `OnEvent`, `OnCamera`, and `OnEnvironment` when the environment changed.
- **Transport:** `Play`, `Pause`, `SetSpeed` (±64), `Seek`, `Step(frames)`.
- **Peek:** `Peek(time)` returns the view at another time (camera, nodes, environment, no events) and leaves the transport, the presented view and the frame cache as they were. Previews, contact sheets and trajectories use it.
- **Safe cameras:** the evaluated camera is always finite. A non-finite pose (a NaN key, a degenerate path) falls back to the recorded camera, or to the last good pose. The fov is clamped to 1..170 and focus and aperture to 0 or more. Paths evaluate finitely when they are empty, have one point, have duplicate points or zero-length segments, or contain a NaN point, and the arc-length table rebuilds itself whenever `Kind`, `Closed` or `Points` change. Shots, cuts, jumps, markers and track keys refuse non-finite times.

`SceneReplayPresenter` turns nodes back into entities:

| Node | Becomes |
| --- | --- |
| `mesh` | A `MeshRenderer` |
| `light` | A `Light` |
| `emitter` | A `ParticleEmitter` |

- Sounds play through the `AudioSystem`.
- The presenter drives the main camera, including depth of field when the camera has an aperture.
- Environment values set console variables of the same name.
- Cached meshes and textures are registered as `Replay/mesh/<hash>` and `Replay/texture/<hash>`.
- Key-only meshes use `MeshLibrary::Find`, then `Load`. Key-only materials use `MaterialLibrary::FindByName`.
- `ResolveMesh` and `ResolveMaterial` let the game resolve its own keys first.
- `Clear()` restores the camera, depth of field, console variables and post-processing.
- `ReleaseAssets()` (also run by the destructor) clears, then unregisters the meshes and textures the presenter registered and releases the materials it created. Call it when a replay session ends, so repeated sessions don't fill the bindless table.
- Environment keys named `post.exposure`, `post.ev100`, `grade.temperature`, `grade.tint`, `grade.contrast`, `grade.saturation`, `grade.slope`, `grade.offset`, `grade.power` and `bloom.intensity` drive `RenderSettings::Post` directly. They apply on top of the settings found before the first such key, and those settings come back when no such key is left. Other names still set console variables.
- The game's camera controller must leave the camera alone while a replay is presented.

## Project (edits)

`ReplayProject` is the editable layer beside a recording. It is saved as readable JSON (`"format": "swim-replay-project"`), and all times are timeline seconds.

- **Node edits:** each is keyed on its own track with an ease (`step`, `linear`, `smooth`, `in`, `out`, `spline`; spline is Catmull-Rom, and rotations slerp). Edit types:
  - Visibility: applies from its key onward.
  - Transform: offset or override.
  - Asset swaps (by kind and key).
  - Props.
- **Added nodes:** their ids have the high bit set. They can be attached to a recorded node.
- **Cameras:** named rigs with keyed position, rotation, look-at (a point or a node plus an offset), relative-to node, fov, roll, focus distance, aperture and focus node.
  - **Path:** a rig with `Path` moves along that path by its `Progress` track (arc-length 0..1; key eases shape the speed). With `RelativeTo` the path is in the node's frame; `FollowRotation = false` keeps only the node's position.
  - **Aim:** `auto` (look-at keys, then the node, then rotation keys), `rotation`, `point` (the `LookAt` track), `node`, `direction` (the `LookDirection` track, or the direction of travel when none is keyed) or `path` (a point moving along `LookPath` by `LookProgress`).
- **Paths:** named splines (`ReplayPath.h`): `linear`, `catmull_rom`, `centripetal` (no cusps or self-loops on uneven spacing) and `bezier` (per-point in/out tangent offsets, filled in automatically when none are given), open or closed. An arc-length table (64 samples per segment) makes `Sample(fraction)` move at constant speed.
- **Shots:** a camera over `[Start, End)`. The shot covering a time wins over cuts, and consecutive shots cut instantly. A shot may move along a path by itself from `ProgressFrom` to `ProgressTo` (eased); its rig, when given, supplies aim, fov and focus, and without one the camera looks along the path. `SetShot` trims or splits the shots it overlaps.
- **Cuts:** these pick the active rig outside shots. Before the first cut, or on an empty name, the recorded camera is used.
- **Jump keys:** forward playback reaching `From` teleports to `To` (cut a stretch out, or repeat one). Seeks ignore them. `ReplayPlayer::GetDiscontinuities()` counts seeks, steps and jumps, so anything following the timeline can resync.
- **Environment:** keyed values by name.
- **Markers:** a time, a name and a kind (`user`, `beat`, `downbeat`, `onset`, `section` or a game's own).
- **Time remap:** keys from timeline time to recording time, for slow motion, holds and reversing.
- **Time scale:** world speed over timeline time (1 = real time). `BakeTimeScale(recordingLength)` integrates it into the time remap, so eased keys give smooth ramps. Steps are at most 1/30 s, never straddle a key and use Simpson's rule, so step and linear keys bake exactly.
- **Music:** a file, the timeline time where it starts and a volume.
- **Undo and redo:** `Checkpoint()` before a change, then `Undo()` or `Redo()`, keeping 256 snapshots.

## Editing helpers

`ReplayEditing.h` builds common edits from those primitives:

- `CreateShot(project, ShotDesc)`: a shot from a preset, with its rig and path named `shot:<name>`. Presets:
  - `static`: a fixed camera looking at the target.
  - `orbit`: circles the target (`Radius`, `Height`, `StartAngle`, `Degrees`).
  - `dolly`: moves from `Radius` to `EndRadius` toward the target.
  - `follow`: rides at `Offset` in the target's frame, looking at it.
  - `flythrough`: flies along `Points`, looking where it goes.
  The target is a node (moving presets then ride in its frame, position only) or a world point.
- `CutsToBeats(project, BeatCutDesc)`: consecutive shots cut at every n-th time in a range, rotating through sources (shot names are copied as templates, other names are rigs). Shots inside the range are replaced and neighbours are trimmed.
- `CollectSnapTargets(project)`: markers, shot edges, cuts and jump keys, sorted.
- `AddLight(project, LightDesc)`: an added `light` node with type, colour, intensity, range, cones and shadows as props.
- `ApplyLightingPreset(project, name, time, ease)`: keys exposure and grading environment values for `neutral`, `golden_hour`, `night`, `noir`, `studio` or `neon`. Two presets at different times blend.
- `SyncRegion(project, RegionSyncDesc, recordingLength)`: retimes a song region so that chosen moments land on beats.
  - Over the timeline range `[From, To]` (where the music plays), the content currently shown between `From` and `ContentTo` plays instead.
  - The time scale there is replaced by a fitted curve that passes each anchor exactly: linear speed ramps when `Smooth`, otherwise constant speed between anchors.
  - Anchors are `Pairs` (beat time, event time). Without pairs, each of `Events` lands on the nearest of `Beats` within `MaxShift` of where an even retime would put it.
  - Anchors that need a speed outside `MinScale..MaxScale` are dropped. A region that cannot fit at all throws.
  - Before `From` nothing moves, which is exact when the keys there are step or linear. After `To` the old speed resumes.
  - Markers of `RetimeMarkerKind` move with their content. Shots and cuts keep their timeline times.
  - It bakes the remap and reports the anchors, the mean speeds and the largest miss.
- `SuggestTimeScale(TimeScaleSuggestDesc)`: time scale keys from the music for a range.
  - `calm`: the speed follows the onset energy, slower in quiet passages, with smooth keys about every two beats.
  - `punchy`: real time, with a slow-motion hit (down to `Low`, then up to `High` and back) on each section start and on the strongest fifth of onsets. Hits snap to beats and are at least four beats apart.

## Previews and drawing data

`ReplayPreview.h`:

- `PreviewRequest` holds a timeline time, a `CameraPose`, a size and an opaque string map of host options (for example `hud: off`).
- The host renders through a `PreviewRenderer`. That is a function taking the request and a reply, and it calls `reply(image, error)` once, now or on a later frame. Images are `Tooling::Image` (RGBA8, top row first).
- `RenderPreviews` issues requests one after another and collects the images.
- `SampleTimes(from, to, n)` returns the centres of n equal parts.
- `SampleCamera(player, from, to, n)` returns the evaluated camera at those times, using `Peek`.
- `MakeFrustum(pose, aspect, near, far)` returns the apex, the axes and the near and far corners.
- `SamplePathViz(path, samples, ticks)` returns the curve, the control points, absolute Bezier handles and equal arc-length ticks with directions.

`Tooling/ImageEncoding.h` provides:

- `EncodePng` and `WritePng`: RGBA8 with per-row filters and fixed-Huffman deflate.
- `EncodeBase64`.
- `ImageContent`: an MCP image content item.
- `TileImages`: contact sheets with aspect-kept cells.

A tool result object can carry extra MCP content items in `"$content"`. The server moves them into the result's `content` after the text and keeps them out of `structuredContent`.

## Sequences

`ReplaySequence.h` plays several recordings back to back.

- **Items:** each `SequenceItem` names a recording, an optional project and the item's own `In` and `Out` timeline times, plus `Enter`, the transition from the previous item:
  - `cut`: instant.
  - `crossfade`: the items overlap by `Duration`, limited to half of either item.
  - `fade`: out to `Color` over the first half, then in from it, with no overlap.
- **Timing:** `Layout(lengths)` places the items on the sequence timeline. `Sample(items, spans, time)` gives the primary item, the secondary item during a crossfade, both item times, the crossfade weight, and the fade colour and amount.
- **Saving:** sequences save as JSON (`ToJson`, `Save`, `Load`).
- **Playback:** `SequencePlayer` owns one `ReplayPlayer` per item. Each opens on first use, through the root or an `Opener`, and keeps its player when items are reordered.
  - `Update(dt)` keeps the playing items at their item times. Small forward steps play through, so events fire, and anything else seeks.
  - `SequenceView` exposes the primary and secondary `ReplayPlayer` pointers, their indices, the weight and the fade. The host presents the primary player, presents the secondary one too during a crossfade, and blends or fades the output.

## Music

`ReplayAudio.h` analyses mono samples:

- `ComputePeaks`: min/max per bucket for drawing a waveform.
- `DetectOnsets`: log-magnitude spectral flux (FFT 1024, hop 512) with adaptive peak picking.
- `EstimateBeats`: a constant-tempo grid. Autocorrelation of the onset envelope, weighted toward 120 BPM against octave errors, then the best phase and the downbeat phase.
- `DetectSections`: boundaries from checkerboard novelty on beat-synchronous band energies, at least `minSectionSeconds` apart.
- `Snap(time, targets, tolerance)`.

`Runtime/ReplayMusic.h` is the engine side:

- `DecodeAudioFile(path)` decodes wav, flac, mp3 and ogg to mono (nothing when the engine is built without audio). Test builds compile miniaudio for decoding only (`SWIM_AUDIO_DECODE_MINIAUDIO`, with no playback backend), and an mp3 fixture is decoded and beat-tracked in the tests.
- `ReplayMusic::Sync(player)`, once a frame after `Update`, plays the project's music at `timeline time - Offset` with the player's speed as pitch. Seeks, steps and jumps restart it at the new position. It is silent while paused, before the offset, past the end and at speeds outside (0, 4]. With the player at 1x the output is in sync for screen capture.
- `Load(path)` decodes once per path for analysis.

## Tool server (MCP)

`ToolServer` speaks MCP's JSON-RPC: `initialize` (versions 2025-06-18, 2025-03-26 and 2024-11-05, capabilities `{ tools: {} }`), `ping`, `tools/list` and `tools/call`.

- Tools are `{name, description, inputSchema, handler}`. The `Schema::` helpers build the JSON Schema.
- A handler error becomes a result with `isError`. An unknown tool is error -32602.
- A tool whose result is ready only later (for example a frame capture) sets `AsyncHandler` instead of `Handler`. It calls `reply(json)` or `fail(text)` exactly once, on the main thread, during the call or from a later frame. Transports wait up to their request timeout. `Call` and `Handle` cannot wait, so an asynchronous tool that has not replied yet gives an error there.
- Transports call `Enqueue` from any thread. The game calls `Pump()` once a frame, so handlers always run on the main thread.

`RegisterReplayTools(server, player, desc)` adds these tools, each with an optional name prefix:

| Group | Tools |
| --- | --- |
| Transport | `status`, `open`, `play`, `pause`, `seek`, `step`, `speed` |
| Nodes | `nodes.list` (kind, near and radius, paging), `nodes.get`, `nodes.path` |
| Edits | `edit.visible`, `edit.transform`, `edit.asset`, `edit.prop`, `edit.clear`, `edit.delete_key`, `node.add`, `node.remove` |
| Cameras | `camera.key`, `camera.delete_key`, `camera.remove`, `camera.list`, `camera.cut` |
| Environment | `env.set`, `env.get` |
| Markers | `marker.add` (with kind), `marker.remove` (by name, kind and range, or nearest), `marker.list` |
| Time | `time.remap`, `time.scale` (keys, then rebakes the remap) |
| Paths | `path.set`, `path.get` (with a polyline), `path.list`, `path.remove`, `path.sample` |
| Camera moves | `camera.path` (path, progress keys, relative-to, follow rotation), `camera.aim` |
| Shots | `shot.create` (static, orbit, dolly, follow and flythrough presets), `shot.set`, `shot.remove`, `shot.list` |
| Jumps | `jump.add` (times or marker names), `jump.remove`, `jump.list` |
| Music | `music.set`, `music.analyze` (writes beat, downbeat, section and optionally onset markers), `music.peaks`, `music.sync_region` (pairs or auto-matched markers; smooth or constant), `music.suggest_timescale` (calm or punchy; applies unless `apply` is false) |
| Sync | `sync.cuts_to_beats`, `snap` |
| Lighting | `light.add`, `light.preset`, `grade.key` |
| Project | `project.get`, `project.set`, `project.save`, `project.load` |
| History | `undo`, `redo` |
| Previews | `preview.render` (time; the camera there or an explicit pose; size; options), `preview.contact_sheet` (a shot, a range or a path; frames, columns and cell size). Both are asynchronous, write a PNG and return its path, with the image inline unless `inline` is false |
| Drawing data | `viz.path`, `viz.camera` (pose and frustum), `viz.shot` (trajectory samples with frusta), `viz.keys` (a rig's keys in world space) |
| Sequences | `sequence.add`, `sequence.remove`, `sequence.move`, `sequence.set_transition`, `sequence.list`, `sequence.seek`, `sequence.play`, `sequence.pause`, `sequence.status`, `sequence.save`, `sequence.load` (only with `ReplayToolsDesc::Sequence`) |

Every edit tool takes an undo checkpoint and refreshes the view. The music tools decode through `ReplayToolsDesc::LoadMusic`; set it to a wrapper around `Engine::DecodeAudioFile`.

Other `ReplayToolsDesc` fields:

- `RenderPreview` renders the preview tools' frames. Previews never move the transport.
- `PreviewDirectory` is where unnamed PNGs go. The default is `Root/previews`, or the temp directory when there is no root.
- `Sequence` and `SequencePath` enable the sequence tools.

### Transports

**`HttpToolTransport`** is MCP "Streamable HTTP" on `127.0.0.1`, with one endpoint (`POST /mcp`):
- **Messages:** a JSON-RPC message goes in and an `application/json` response comes back. A notification gets 202. GET returns 405, because there is no SSE stream. `DELETE` ends the session.
- **Sessions:** `initialize` returns an `Mcp-Session-Id`. Later requests must send it: a missing id gets 400 and an unknown one gets 404.
- **Security:**
  - The socket binds to loopback only, and peers outside 127/8 are refused.
  - The Host header must be localhost.
  - An Origin header, when present, must be localhost; anything else gets 403, which blocks DNS rebinding.
  - `Authorization: Bearer <token>` is compared in constant time; a wrong or missing token gets 401. The token is random per start unless one is given.
- **Connection file:** `WriteConnectionFile(path)` writes the url, the token and the header for an agent's MCP configuration.

**`StdioToolTransport`** reads line-delimited JSON-RPC on stdin and writes to stdout, for a process the agent launches itself.

## Limitations

- **Skinned meshes** are recorded with their bind pose. Animated characters need a game source, for example an annotation plus a kind presenter, or bones as nodes.
- **Terrain** and other custom renderers need a game source too.
- **Emitter texture indices** are bindless indices from the recording run. They are only right when the same textures load in the same order.
- **Materials** are looked up by description (`GetOrCreate`). Runtime changes to a material set show up as a new material asset.
- **Particles** replay by emitter desc and seed, not per particle.
- **Sounds** start at their recorded time. Seeking doesn't resume voices midway.
- **Beat tracking** assumes a constant tempo. Songs with tempo changes get the dominant tempo.
- **Previews** need the host's `RenderPreview`. The engine has no built-in offscreen replay renderer.
- **Sequence transitions** are data. The host blends the two players' output, or fades it to the colour.

## Resource lifetime

- `MeshLibrary` keeps a redirect when a name is registered again: old handles resolve to the new asset (`Resolve`), so components, UI images and caches holding the old handle don't fall back to white. The material library, viewports and the scene bridge resolve through it.
- `UnregisterTexture` frees a texture's bindless slot. Retired assets are destroyed once the GPU is done with them; `FrameRenderer::BeginFrame` collects freed bindless slots every frame.
- The `resource_stats` console command prints bindless slots in use, residency, geometry heap use, MeshLibrary meshes, textures, pending releases and redirects, and the material count. Use it to check that repeated replay sessions return to the same numbers.
