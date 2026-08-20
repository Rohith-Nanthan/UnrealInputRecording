# Build Prompt — Unreal Enhanced Input Recorder + Control Recap

> Paste everything below the line into a fresh Claude Code session, in a **new** Unreal Engine 5.x
> C++ project. Read the whole thing before writing any code — later sections constrain earlier ones.

---

## 0. Before you start

### 0.1 Confirm the ground

State back to me, in two lines, before you begin:

1. The engine version you are building against.
2. Whether this is a fresh template project or an existing one, and which template.

### 0.2 Prerequisites this system assumes

- **Enhanced Input**, not the legacy input system. There is no fallback path — every part of this
  is built on `UInputAction` / `UInputMappingContext` / `UEnhancedInputLocalPlayerSubsystem`.
- A playable pawn already bound to input actions through a mapping context. The **Third Person
  template** is the intended baseline: it ships `IMC_Default` with `IA_Jump`, `IA_Move`, `IA_Look`,
  `IA_MouseLook`.

If Enhanced Input is not set up yet, set it up first and say so.

### 0.3 Plugins to enable in the `.uproject`

Enable these before the first build, or the module will not link:

| Plugin | Why |
| --- | --- |
| `EnhancedInput` | The whole system. Usually already on. |
| `MediaIOFramework` | `UMediaOutput` / `UMediaCapture`. **Not enabled by default.** |
| `WmfMedia` (Windows) | `UMediaPlayer` playback of the captured `.mp4` during review. |
| `ModelContextProtocol` + `AllToolsets` | The MCP editor bridge — see §1.1. |

### 0.4 This is a from-scratch build

Do not port, adapt, or copy any existing recording code you may find in the project or elsewhere.
Every file listed in this document is to be written new. If you find prior art, read it only to
understand *what* a piece did — never to reuse *how* it did it. The prior implementation was
unreliable and its bugs are not to be inherited.

---

## 1. Two hard requirements that govern everything

### 1.1 Use the Unreal MCP server for all asset work

An MCP editor bridge is configured at `http://127.0.0.1:8000/mcp` (`.mcp.json`, server name
`unreal-mcp`). **Verify it is alive with `list_toolsets` before promising any asset creation.** If
it times out, the editor is closed or the server is not started — start the editor and run
`ModelContextProtocol.StartServer` in its console, or set `bAutoStartServer=True` and
`ServerPortNumber=8000` under `[/Script/ModelContextProtocol.ModelContextProtocolSettings]` in
`Config/DefaultEditorPerProjectUserSettings.ini`.

Toolsets you will need:

| Toolset | Use it for |
| --- | --- |
| `editor_toolset.toolsets.blueprint.BlueprintTools` | Creating the GameMode / PlayerController Blueprints |
| `UMGToolSet.UMGToolSet` | Creating every `WBP_*` widget and its tree |
| `editor_toolset.toolsets.object.ObjectTools` | `list_properties` → `get_properties` → `set_properties` on anything |
| `editor_toolset.toolsets.asset.AssetTools` | Creating/finding Input Actions, Mapping Contexts, Data Assets |
| `editor_toolset.toolsets.scene.SceneTools` | Creating the review map, placing actors |
| `editor_toolset.toolsets.data_asset.DataAssetTools` | The icon-mapping and UI-input data assets |
| `ConfigSettingsToolset.ConfigSettingsToolset` | Writing the developer-settings values into `DefaultGame.ini` |
| `EditorToolset.LogsToolset` | Reading the output log to verify your own work |
| `EditorToolset.EditorAppToolset` | Driving PIE to smoke-test |

**UMGToolSet workflow is not optional:** for every widget and slot, call
`ObjectTools.list_properties` first, then `get_properties`, then `set_properties`. Property names
vary per widget class and cannot be guessed — skipping the listing step makes `set_properties`
silently write nothing.

Do not hand me a list of manual editor steps for anything MCP can do. Manual steps are only
acceptable where the bridge genuinely cannot reach (see §11).

### 1.2 Everything must be extensible from Blueprint

C++ exists here for the things Blueprint cannot do: binary file I/O, H.264 encoding, `UMediaCapture`
plumbing, command-line parsing at module startup. **Everything else must be reachable, overridable,
and readable from Blueprint.**

Concretely, this is the standard every type in this system is held to:

- Every `USTRUCT` → `BlueprintType`, every field `BlueprintReadWrite` (or `BlueprintReadOnly` where
  it is genuinely derived state).
- Every `UENUM` → `BlueprintType`.
- Every subsystem / component / store function that a designer could plausibly want → `UFUNCTION(BlueprintCallable)`
  or `BlueprintPure`.
- Every state transition and event → `UPROPERTY(BlueprintAssignable)` dynamic multicast delegate.
- Every C++ class that has behaviour worth customising → `UCLASS(Blueprintable, BlueprintType)` with
  `BlueprintImplementableEvent` / `BlueprintNativeEvent` hooks at each decision point.
- Every non-trivial helper → a `UBlueprintFunctionLibrary` static so it is callable from a graph.

If you find yourself writing a C++ branch a designer might want to change, that branch belongs
behind a `BlueprintImplementableEvent` or a settings field instead.

---

## 2. What this system is, in one paragraph

A designer or tester plays the game normally while their input is recorded — every Enhanced Input
action value, timestamped — alongside a screen-capture video of what they saw. Later, anyone can
open a standalone review map, watch that video with an input timeline overlaid on it, and get
quizzed: the video plays, pauses at each significant input, waits for the viewer to press the same
thing, and names anything wrong they pressed while it waited. Everything is local files. No
networking.

---

## 3. Non-negotiable architecture decisions

Each of these exists because the obvious alternative has a specific failure mode. Follow them as
written rather than re-deriving something different.

1. **One `UGameInstanceSubsystem` (`UInputRecordingSubsystem`) owns everything** — replay component
   lookup, video capture, video playback, the session store, and widget creation. Widgets and
   Blueprints bind to *it*, never directly to the `UInputReplayComponent`. That component lives on a
   controller or pawn and dies on every respawn and level travel; a widget holding a raw pointer to
   it holds a dangling pointer within one map change. The subsystem outlives all of it and
   re-resolves the component lazily.

2. **Recording is a `UActorComponent` (`UInputReplayComponent`)**, not a PlayerController subclass.
   It resolves onto whichever controller/pawn exists and can be auto-created by the subsystem. This
   is what makes the system drop into any project without dictating a class hierarchy.

3. **Recordings live in dated, indexed session folders, never bare files:**

   ```
   <RecordingRoot>/
   ├─ RecordingIndex.json              authoritative next-index counter, never reuses an index
   ├─ Recording_1/
   │   ├─ Recording_1.ghost            binary input timeline
   │   ├─ Recording_1.ghost.json       optional human-readable copy
   │   ├─ Recording_1.mp4              H.264 video
   │   └─ Session.json                 metadata: display name, duration, cue count, timestamps
   └─ Recording_7/                     indices go sparse after eviction and are never reused
   ```

   The folder is the unit of storage, eviction, listing, and review. Every file inside is named
   after the folder, so a session copied to a desk somewhere is still self-describing.

4. **A storage quota with LRU eviction.** Total size is capped (default 900 MB). When a new take
   needs room, the **least recently *updated*** session is deleted first — not oldest by creation,
   because reviewing a session counts as using it and should protect it. The session currently
   recording and the one currently under review are pinned and never evicted. If the quota is hit
   *during* a take, **stop the take** rather than evicting something else mid-write: trading a
   finished recording for an unfinished one is the wrong trade. The partial take is still saved and
   the UI must say it stopped early rather than claiming a normal stop.

5. **Two review surfaces, genuinely separate — not two configurations of one widget:**
   - A small **corner overlay** pinned bottom-right during normal gameplay.
   - A **full-screen standalone review map** (`ControlRecapLevel`) with its own GameMode and
     PlayerController.

   The full-screen one needs its own game mode because it has controller-level concerns (input
   mode, pawn state, level travel on exit) that a widget pushed over the gameplay HUD cannot own
   cleanly.

6. **Every UI class is a separate C++ widget class that builds no widget tree of its own.** See §9
   — this is expanded there and it is a firm requirement.

7. **Console commands are module-level `FAutoConsoleCommand` objects**, not `UFUNCTION(Exec)` on a
   PlayerController. Exec functions only route through whatever class the console happens to
   dispatch to, and stop working silently when that class changes. A console command object
   registers once at module load and works from any world state, including before a
   PlayerController exists.

8. **Video capture writes straight into the claimed session folder**, and the folder is claimed
   (index reserved, directory created) only *after* input recording has successfully started. A
   failed start must never burn a folder index or leave an empty directory behind.

9. **Video capture failure is non-fatal.** No viewport, no encoder, no platform support — log it and
   keep recording input. A `.ghost` with no `.mp4` is a usable recording; a lost `.ghost` is a take
   somebody has to re-perform.

---

## 4. Module A — recording / replay core

### 4.1 Data model

All `BlueprintType`, all fields Blueprint-accessible.

- `FRecordedInputSample` — one sample:
  | Field | Type | Note |
  | --- | --- | --- |
  | `ActionName` | `FName` | short asset name, e.g. `IA_Jump` |
  | `ActionIndex` | `int32` | index into the header's `ActionPaths` — the compact key |
  | `FrameIndex` | `int32` | logical tick; **this** drives playback, never the float |
  | `TimeSeconds` | `float` | derived, for tooling / scrubbing / JSON readability |
  | `TriggerEvent` | `uint8` | `ETriggerEvent` at capture, for debugging |
  | `ValueType` | `uint8` | `EInputActionValueType` — Boolean / Axis1D / Axis2D / Axis3D |
  | `Value` | `FVector` | always a full vector regardless of real dimensionality |

  Do **not** store a `UInputAction*` (meaningless across sessions) or an `FInputActionValue` (its
  members are private and not reflected, so neither the reflection system nor `FJsonObjectConverter`
  can round-trip it). Store the raw vector plus the type tag — that is lossless.

- `FInputRecordingHeader` — recording id (`FGuid`), display name, recorded-at UTC, level name,
  engine version, time mode, logical ticks per second, total frames, random seed,
  `TArray<FString> ActionPaths` (soft paths in registry order), and the indices of frame-delta
  actions.

- `FInputRecording` — header + `TArray<FRecordedInputSample> Samples` + optional per-frame delta
  seconds. Provide `IsValidRecording()`, `GetDurationSeconds()`, `Reset()`.

**Storage is delta-compressed:** write a sample only when an action's value or trigger event
actually changed. Playback reconstructs the dense stream by holding the last known value.

### 4.2 Timing modes

`EInputReplayTimeMode { RealTime, FixedLogicalStep }`, default `FixedLogicalStep` at 60 Hz.

- `RealTime` — wall-clock `GetWorld()->GetTimeSeconds()`.
- `FixedLogicalStep` — a fixed-tick accumulator independent of frame rate.

Fixed logical step is the default because MatchInput needs frame-rate-independent determinism: a
take recorded at 144 fps must line up identically when reviewed at 30 fps.

### 4.3 `UInputReplayComponent : UActorComponent`

`UCLASS(Blueprintable, BlueprintType, ClassGroup=(InputRecording), meta=(BlueprintSpawnableComponent))`

- Modes: `EInputReplayMode { Idle, Recording, PlayingGhost, MatchingInput }`, `BlueprintType`.
- `StartRecording()` / `StopRecording()` — on start, enumerate every `UInputAction` reachable
  through the configured mapping contexts via `UEnhancedInputLocalPlayerSubsystem`, bind a delegate
  to each, and append a sample on every triggered/value-changed event.
- Configurable action lists, each on the component *and* in project settings:
  - `AdditionalActions` — actions to track that no recorded context references.
  - `FrameDeltaActions` — actions whose value is a per-frame **delta** rather than a rate (mouse
    look, scroll wheel). These need distinct handling in cue extraction and must never fail a match:
    nobody reproduces a prior mouse delta pixel-for-pixel.
  - `EInputRecordingFilterMode { RecordAll, WhitelistOnly }` + `RecordedActionWhitelist`. The
    whitelist is **subtractive only** — it narrows what the contexts already reach, it never adds
    from outside them. Log a warning when a whitelist excludes every reachable action, because that
    produces a silently empty recording that looks identical to "nobody pressed anything".
- Save/load through a serializer class:
  - Binary `.ghost` via `FMemoryWriter`/`FMemoryReader` + `FFileHelper`. Magic number, version
    number, refuse to load a newer version.
  - JSON `.ghost.json` via `FJsonObjectConverter`, for reading and diffing only — JSON float
    round-tripping is not bit-exact.
  - **The serializer takes an absolute base path with no extension** and appends `.ghost` /
    `.ghost.json` itself. One base path produces every file in a take, which is what keeps the
    session-folder layout free of string surgery at call sites.
- `GetLiveInputSnapshot(FString& OutActionName, FVector& OutValue) -> bool` — `BlueprintPure`,
  returns whichever tracked action is currently non-zero, for a live HUD read-out. False when
  nothing is active.
- `BlueprintAssignable` delegate on every recorded sample (`ActionName`, `TimeSeconds`, `Value`) so
  UI builds a running history without polling.
- Forward `PreProcessInput` / `PostProcessInput` from the owning PlayerController into the
  component. `PreProcessInput` runs before Enhanced Input evaluates the stack and `PostProcessInput`
  after, so sampling reads post-modifier values in the same frame. Falling back to `TickComponent`
  makes every judged input one frame stale — and judging input is the entire job of the review map.
  Guard against being stepped twice in one frame.

---

## 5. Module B — MatchInput (the quiz / ghost-comparison system)

### 5.1 `FMatchInputCue`

`BlueprintType`, fields Blueprint-readable:

| Field | Type | Note |
| --- | --- | --- |
| `Action` | `TSoftObjectPtr<UInputAction>` | precise, but goes stale if the asset is renamed |
| `ActionName` | `FString` | short name — survives a project reorganisation |
| `ActionIndex` | `int32` | index into the recording header |
| `FrameIndex` | `int32` | logical tick of the press onset |
| `TimeSeconds` | `float` | absolute time from recording start |
| `IntervalFromPreviousSeconds` | `float` | the gap MatchInput actually counts down |
| `ValueType` | `uint8` | |
| `ExpectedValue` | `FVector` | direction matters for axes, magnitude does not |
| `Description` | `FString` | pre-formatted, e.g. `IA_Move [Fwd-Right \| X=+0.71 Y=+0.71]` |

Store **both** the soft path and the short name; every consumer tries the path first and falls back
to the name.

### 5.2 Cue extraction — `BuildMatchInputCues`

Walks a full `FInputRecording` and picks the significant **press onsets** — the frames where an
action crossed from below the press threshold to above it — not every sample. Options struct
(`FMatchInputCueBuildOptions`, `BlueprintType`, all fields editable):

- `PressThreshold` (default `0.35`) — magnitude that counts as pressed. Doubles as the dead zone
  when listening to the live controller, so stick drift and trigger creep do not register.
- `MinimumCueSpacing` (default `0.05`) — two onsets of the same action closer than this collapse
  into one cue.
- `bIgnoreFrameDeltaActions` (default `true`) — skip mouse/scroll deltas; their values are
  continuous noise and would produce hundreds of meaningless cues.
- `IgnoredActions` (`TArray<FString>`, accepts full path or bare name) — default it to exclude look
  and camera actions (`IA_Look`, `IA_MouseLook`). Camera movement must never count as a wrong answer.

Put this in its own translation unit, not inside the component. Two consumers need identical
results (the live state machine and any editor-side preview), and duplicating the rules guarantees
they drift apart.

### 5.3 MatchInput playback

Given a loaded recording and its cue list, run a **virtual clock** forward. At each cue's timestamp,
pause the clock and wait for the live player to produce a matching input before resuming.

- Matching: booleans need presence; axes must point the same way within `MatchDirectionTolerance`
  (a dot product; `0.7` ≈ within 45°). Magnitude does not have to match.
- `BlueprintAssignable` delegates for every state — **cue presented** (index, total, expected
  description), **input matched** (index, total), **input mismatched** (expected description, what
  was actually received), **finished** (`bool bCompletedAllCues`).
- One description formatter used by every log line and every UI label, so they can never disagree
  about phrasing.

---

## 6. Module C — synchronised video capture

This is the part with the most subtle bugs. Read §10 before implementing.

- Capture the viewport with `UMediaCapture` via a custom `UMediaOutput` subclass, feeding frames
  into an H.264 encoder. On Windows use the Media Foundation **sink writer** (`IMFSinkWriter`) —
  it does H.264 encode *and* MP4 muxing in one object, picks up a hardware encoder when present,
  and needs no plugin, only `mfplat.lib`, `mfreadwrite.lib`, `mfuuid.lib`, `ole32.lib`.
- **Wrap the raw sink-writer plumbing in its own backend class**, behind a plain C++ interface,
  separate from the Unreal-facing capture object. To support another platform you add a backend and
  change nothing else. On a platform with no backend, `Create()` returns null and recording proceeds
  without video.
- Threading contract, and state it in the header:
  - `Initialize()` — creating thread, blocks briefly.
  - `SubmitFrame_AnyThread()` — render thread, once per captured frame. **Copies and returns; never
    encodes.** Encoding on the render thread will destroy frame time.
  - `Finalize()` — creating thread, blocks until the queue drains and the file closes.
  - The caller must not call submit concurrently with finalize; guard it with a critical section.
- **Capture at native viewport resolution.** No scale-factor knob "for performance" — it adds a
  variable that makes the orientation bug harder to diagnose, and 1× is correct for a review video
  anyone wants to read text in. Provide an explicit, off-by-default `bOverrideResolution` +
  `ForcedResolution` escape hatch instead. Snap both axes down to even — H.264 refuses odd dimensions.
- Bounded frame queue (`MaxQueuedFrames`, default 6). Drop frames rather than stalling the render
  thread. At 1080p each slot is ~8 MB.
- Start capture only once input recording has definitely started (§3.8); stop it in the same
  stop-and-save call that finalises the `.ghost`, so both halves of a take are always written together.
- A **video player** class wrapping `UMediaPlayer` + `UMediaTexture`: `OpenVideo(path)`,
  `GetMediaTexture()`, and a sync-to-clock call that keeps the player aligned with the MatchInput
  virtual clock every tick — so a cue that pauses the clock pauses the video too.
- A debug console command that dumps the encoder's **first captured frame** to a PNG next to the
  `.mp4`, containing the exact bytes handed to the encoder. PNG specifically: BMP's own row order is
  bottom-up by convention and would reproduce the very ambiguity this is meant to resolve.

---

## 7. Module D — the session store (`URecordingStore`)

`UCLASS(BlueprintType)`, a `UObject` owned by the subsystem so it outlives level travel.

### 7.1 Root resolution

| Platform | Root |
| --- | --- |
| Editor / Windows / Mac / Linux | `<ProjectSavedDir>/Recordings` |
| Console | `<ProjectPersistentDownloadDir>/Recordings` |
| Any, overridden | `-RecordingRoot=<path>` on the command line |

The console split is not cosmetic: `ProjectSavedDir()` is not reliably writable in a packaged
console title, and hundreds of megabytes of video has no business in user save data even where it
technically works.

Cache the resolved root, so a mid-session flag change cannot split a take across two folders.

### 7.2 API — all `BlueprintCallable` / `BlueprintPure`

`Initialize(quotaBytes)`, `Rescan()`, `BeginSession(displayName, mapName, reserveBytes)`,
`CommitSession(index, durationSeconds, cueCount)`, `AbortSession(index)`, `TouchSession(index)`,
`TrimToQuota(headroomBytes)`, `PinSession` / `UnpinSession` / `IsPinned`,
`GetMostRecentSession()`, `FindSession(index)`, `FindSessionByFolder(name)`, `GetSessions()`,
`GetStats()`, `GetTotalBytes()`, `HasHeadroom(extraBytes)`, `LogInventory(reason)`.

### 7.3 `FRecordingSessionInfo` (`BlueprintType`)

Split into two blocks and say so in comments:

- **Persisted in `Session.json`:** `Index`, `DisplayName`, `CreatedUtc`, `UpdatedUtc`,
  `DurationSeconds`, `CueCount`, `MapName`.
- **Derived from the folder on every scan:** `FolderName`, `AbsolutePath`, `TotalBytes`,
  `bHasGhost`, `bHasVideo`, `bHasJson`.

`Session.json` is metadata, never truth. Every field must be re-derivable from the folder alone,
which is exactly what happens when the manifest is missing or corrupt. A folder without a manifest
is still a usable session, just with unknown duration until rebuilt.

Helpers: `IsValid()`, `IsPlayable()` (has a `.ghost` — a folder still being written is newer than
everything and must not win "most recent"), `GetBasePath()`, `GetGhostPath()`, `GetJsonPath()`,
`GetVideoPath()`, `GetManifestPath()`, static `MakeFolderName(index)` / `ParseFolderName(name)`.

Also `FRecordingStoreStats` (`BlueprintType`): session count, total bytes, quota bytes, next index,
`GetUsedFraction()` clamped to 0..1, `GetFreeBytes()`.

### 7.4 Quota rules

Eviction happens at exactly two moments — **before a take starts** (reserving headroom) and **after
one commits**. Never during. Mid-take the store only reports; the subsystem stops the take.

| Moment | Behaviour |
| --- | --- |
| Boot | Scan, reconcile the index counter, log the inventory, trim if already over quota. |
| Before a take | Evict until `ReserveMegabytesPerTake` fits. If it cannot even then, refuse to start. |
| During a take | Polled ~1 Hz. On reaching quota the take **stops** and is flagged as quota-stopped. |
| After a take | Write the manifest, refresh sizes, unpin, then trim. |

### 7.5 One dedicated log category

Every file and quota operation logs to `LogRecordingStore` and nothing else does. The point is that
`log LogRecordingStore Verbose` gives the complete story of disk activity with zero input or
rendering noise mixed in.

---

## 8. Module E — the subsystem (`UInputRecordingSubsystem`)

`UGameInstanceSubsystem`, `BlueprintType`, everything exposed.

- **Replay component discovery**, in order: cached → local PlayerController → its pawn → any actor
  in the world carrying the component → auto-create on the PlayerController if settings allow.
- Recording control: `StartRecording(displayName)`, `StopRecording()`,
  `StopRecordingAndSave(name, bAsJson)`, `StopRecordingWithoutSaving()`, `CancelRecording()`
  (abandon **and delete** the session folder — distinct from stopping without saving, which leaves
  the folder and in-memory data intact).
- `RunControlRecapTest(FString SessionSpecifier)` — stop, save, then travel to the review map. The
  specifier is **optional** (empty = most recent). See §12.2.
- `StartMatchInputFromSession(FRecordingSessionInfo)` — touches the session for LRU protection, then
  starts MatchInput against it.
- Owns and lazily creates every widget, each from a Blueprint class read out of project settings
  (§9.3). Never hardcode a widget class.
- The quota guard: poll the store roughly once a second **while recording only**, piggybacking on a
  tick you already have — do not add a second ticker. Stop the take when live file size plus
  committed sizes would exceed quota, and flag the save as quota-stopped so the UI can say so
  truthfully.
- `BlueprintAssignable` delegates, relayed from the component so Blueprints only ever bind to the
  subsystem: mode changed, sync point recorded, video saved, recording saved
  (`bSuccess`, `SessionPath`, `bQuotaStopped`), match cue presented / matched / mismatched /
  finished.
- Blueprint-readable state: `GetMode()`, `IsRecording()`, `IsMatchingInput()`, `IsIdle()`,
  `IsAwaitingMatchInput()`, `GetExpectedInputDescription()`, `GetMatchProgress()`,
  `GetMatchCueCount()`, `GetCurrentMatchCueIndex()`, `GetMatchClockSeconds()`, `GetMatchCues()`,
  `GetMismatchCount()`, `GetLastMismatchDescription()`, `GetRecordingDurationSeconds()`,
  `GetStatusText()`, `GetLiveInputSnapshot()`.

---

## 9. UI — every surface is its own C++ widget with its own Blueprint child

This is a firm structural requirement, not a suggestion.

### 9.1 The rule

For **each** UI surface below:

1. Write a C++ `UUserWidget` subclass, `UCLASS(Blueprintable, BlueprintType)`, that **builds no
   widget tree of its own** — no `RebuildWidget`, no `WidgetTree->ConstructWidget`. C++ holds logic
   only: subsystem bindings, per-frame refresh, timeline maths.
2. Every visual element is a `UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))`.
3. **Create a Blueprint child of it via MCP**, build the actual tree in that Blueprint, and **use
   the Blueprint class** everywhere the widget is referenced — in the map, in project settings, in
   the player controller. Nothing anywhere may reference the raw C++ widget class as the thing to
   instantiate.
4. Add `BlueprintImplementableEvent` hooks at every visual decision point (cue presented, matched,
   mismatched, recording started/stopped, row added), so a designer can animate and restyle without
   touching C++.
5. Write a `ValidateBindings()` pass, called once on construct, that logs **every** missing hook in
   a single message.

**Use `BindWidgetOptional`, never `BindWidget`.** Strict `BindWidget` fails Blueprint *compilation*
the instant a single name does not match, one problem at a time, so you cannot save a
partially-built Blueprint and cannot rearrange a tree mid-design. Optional bindings never block;
null-check every one before use, and let `ValidateBindings()` be the safety net.

### 9.2 The widgets to build

| C++ class | Blueprint child | What it is |
| --- | --- | --- |
| `UInputRecorderOverlayWidget` | `WBP_InputRecorderOverlay` | Corner overlay during gameplay. Start/Stop toggle, status pill, live "current input" read-out, scrolling sync-point history. **No Test button** (§12.1). |
| `USyncPointRowWidget` | `WBP_SyncPointRow` | One row in the overlay's history list. |
| `UControlRecapWidget` | `WBP_ControlRecap` | The full-screen review surface. |
| `UVideoSurfaceWidget` | `WBP_VideoSurface` | Pure presentation: one `UImage` bound to whatever `UMediaTexture` the subsystem has open. Zero knowledge of recording or MatchInput — any screen that needs video embeds one. |
| `UMatchCueMarkerWidget` | `WBP_MatchCueMarker` | One cue marker on the review timeline; three visual states (pending / active / completed). |
| `UWrongInputRowWidget` | `WBP_WrongInputRow` | One "you pressed X" line in the wrong-input list (§13.3). |
| `URecordingListWidget` | `WBP_RecordingList` | In-game session browser (§12.3). |
| `URecordingListRowWidget` | `WBP_RecordingListRow` | One session row: folder, display name, size, relative age. |
| `URecordingToastWidget` | `WBP_RecordingToast` | Save/cancel confirmation toast. |

### 9.3 Widget classes come from project settings

A `UGameInstanceSubsystem` and a PlayerController have no details panel, so widget class assignment
has to live in the settings asset as `FSoftClassPath` with a `MetaClass` meta tag pointing at the
C++ base. Load with a fallback to the C++ class so a caller never gets null, and log an error when
a path is empty or will not load.

### 9.4 Corner overlay sizing

Wrap the overlay in a `USizeBox` and set `MaxDesiredWidth`/`MaxDesiredHeight` from C++ each frame —
**max, not fixed**, so a panel with little in it stays small. Cap it as a fraction of screen
**area**, not width and height independently: those two disagree at unusual aspect ratios and the
panel ends up eating an ultrawide screen. Resolve the viewport DPI scale via
`FPlatformApplicationMisc` when computing the cap.

### 9.5 Content folder layout — everything under `RecordingFolder`

Every asset this system creates goes under `/Game/RecordingFolder/`. Nothing anywhere else.

```
/Game/RecordingFolder/
├─ Maps/
│   └─ ControlRecapLevel                     the standalone review map
├─ Blueprints/
│   ├─ BP_ControlRecapGameMode               child of AControlRecapGameMode
│   └─ BP_ControlRecapPlayerController       child of AControlRecapPlayerController
├─ Widgets/
│   ├─ WBP_InputRecorderOverlay
│   ├─ WBP_SyncPointRow
│   ├─ WBP_ControlRecap
│   ├─ WBP_VideoSurface
│   ├─ WBP_MatchCueMarker
│   ├─ WBP_WrongInputRow
│   ├─ WBP_RecordingList
│   ├─ WBP_RecordingListRow
│   └─ WBP_RecordingToast
├─ Input/
│   ├─ IMC_RecordingUI                       semantic UI verbs only
│   ├─ IA_UI_Accept
│   ├─ IA_UI_Back
│   └─ IA_UI_ToggleRecord
├─ DataAssets/
│   ├─ DA_RecordingUIInput                   URecordingUIInputConfig instance
│   └─ DA_InputIcons                         action → sprite mapping
└─ Materials/
    └─ M_VideoSurface                        optional, for colour-grading / letterboxing the video
```

---

## 10. Module F — the standalone review map

### 10.1 `AControlRecapGameMode : AGameModeBase`

`UCLASS(Blueprintable)`. Sets its own `PlayerControllerClass` and `DefaultPawnClass` (a plain
non-interactive pawn — the player controls nothing, they only answer prompts). No `HUDClass`.
Exposes `TargetOnCancelMap` (`FSoftObjectPath`, editable) and `ForcedSessionFolder` (`FString`,
pins the level to one take instead of "most recent").

Create `BP_ControlRecapGameMode` as its child and set that as the map's GameMode override.

### 10.2 `AControlRecapPlayerController : APlayerController`

`UCLASS(Blueprintable)`. On `BeginPlay`:

1. Resolve the session (§10.3).
2. Create the review widget **from the Blueprint class in project settings**, add it to the viewport.
3. Call `BeginReview(session)` on it.
4. Set up input (§13.1) — do **not** lock anything out.

`LeaveRecap()` resolves a destination in order: game mode's `TargetOnCancelMap` → the project
setting's gameplay map → the engine's own default map. Never leave a dead end with nowhere to go.

Forward `PreProcessInput` / `PostProcessInput` to the replay component (§4.3).

Create `BP_ControlRecapPlayerController` as its child and set it on the GameMode Blueprint.

### 10.3 Session resolution order

1. `-IR=1` on the command line — forces the most recently updated playable session, skipping
   everything below.
2. `-ControlRecap=<name>` — that specific session. Accept `Recording_5` or a bare `5`.
3. The GameMode's `ForcedSessionFolder`, if the level pins one.
4. The most recently updated **playable** session.

The command line outranks the level's own pin on purpose: the pin is a design-time choice baked
into a map, and somebody typing a flag into a terminal is deliberately overriding it for this run.

Nothing found → the widget comes up with an explanatory empty state, not a black screen.

### 10.4 The review widget's layout (what the Blueprint builds)

Top to bottom:

```
header        session label + Cancel button
video         a WBP_VideoSurface, height driven from a VideoScreenFraction setting
cue counter   "cue 3 / 12"
track         progress bar with one WBP_MatchCueMarker per cue
expected      LARGE "press this now" label + icon
wrong input   the wrong-input list, directly BELOW the expected input   ← §13.3
legend        small
```

Drive the video height from C++ via a `USizeBox` override rather than leaving it to a Fill slot:
"the video is N% of the screen" has to stay true as the rows beneath it change height, and a Fill
slot silently gives the video whatever is left over instead.

Position cue markers by **fractional anchor on a Canvas Panel overlaid on the progress bar**, so a
marker's X position and the bar's fill share one coordinate space and cannot visually drift apart.
Each cue renders as one marker widget carrying both its above-bar icon and its on-bar dot — one
widget on one anchor, so there is no second rail doing the same arithmetic and rounding it
differently.

Fire a `BlueprintAssignable` delegate on close (`bool bCompletedAllCues`) that the player controller
binds to `LeaveRecap()`.

---

## 11. Project settings — one `UDeveloperSettings`

`UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Input Recorder"))`, category `Game`,
backed by `DefaultGame.ini`. Everything a designer might tune lives here and nowhere else:

- **Recording** — recorded mapping contexts, `AdditionalActions`, `FrameDeltaActions`, filter mode +
  whitelist, `TimeMode`, `LogicalTicksPerSecond`, default recording name, `bAlsoExportJsonOnSave`.
- **Match Input** — `FMatchInputCueBuildOptions`, `MatchDirectionTolerance`.
- **Video** — `bCaptureVideoWithRecording`, `bPlayVideoDuringMatchInput`,
  `bCaptureVideoIncludingUI`, and a `FInputRecordingVideoOptions` struct (target frame rate,
  bitrate, resolution override toggle + forced resolution, max queued frames, orientation).
- **Storage** — `QuotaMegabytes` (900), `ReserveMegabytesPerTake` (150),
  `bStopRecordingWhenQuotaReached` (true).
- **Control Recap** — `ControlRecapMap` and `GameplayMap` (`FSoftObjectPath`, `AllowedClasses =
  "/Script/Engine.World"`), `UIInputConfig`.
- **UI** — `IconMapping`, plus `FSoftClassPath` for every widget class in §9.2.
- **Subsystem** — `bAutoCreateReplayComponent`.

`ApplyDefaultsTo(component, bForce)`: `bForce = true` overwrites everything (for components the
subsystem created itself); `false` only fills in fields the component left empty, so a component
configured by hand in the editor keeps its own setup.

Write the initial values into `DefaultGame.ini` through the MCP `ConfigSettingsToolset` once the
assets exist.

---

## 12. Console commands

Register as module-level `FAutoConsoleCommandWithWorldAndArgs` (§3.7). Prefix everything `ir.`.

| Command | Behaviour |
| --- | --- |
| `ir.record.start [name]` | Start a take, show the overlay. Optional display name. |
| `ir.record.stop` | Stop, save the session, hide the overlay. |
| `ir.record.cancel` | Abandon the take and delete its folder. |
| `ir.record.test [session]` | **Optional argument** — see §12.2. |
| `ir.store.list` | Print every session — see §12.3. |
| `ir.store.list.ui` | Same data, shown in-game — see §12.3. |
| `ir.store.trim` | Evict LRU sessions until back under quota. |
| `ir.ui.show` / `ir.ui.hide` | Show/hide the corner overlay without starting a take. |
| `ir.video.dumpframe` | Arm the one-shot first-frame PNG dump (§6). |

### 12.1 Remove the Test button from the corner overlay

The overlay has **no Test button**. Testing is a console/terminal action (`ir.record.test`, `-IR=1`),
not a button on a gameplay HUD. The overlay is Start/Stop, status, live input, and history — nothing
else.

### 12.2 `ir.record.test [session]` — optional target

- **No argument** — current behaviour: stop and save the take in progress (if any), then open the
  review map on the most recently updated playable session.
- **With an argument** — review *that* session instead. Accept all of:
  - a full folder name: `ir.record.test Recording_5`
  - a bare index: `ir.record.test 5`
  - a display name: `ir.record.test "Jump tutorial"` (case-insensitive; if it matches more than one,
    pick the most recently updated and log which one you picked and what else matched)

  If the named session does not exist or is not playable, **log an error naming what was asked for
  and list the available folder names** — do not silently fall back to the most recent one. Silently
  reviewing a different take than the one asked for is worse than doing nothing.

The specifier travels through `UInputRecordingSubsystem::RunControlRecapTest(FString SessionSpecifier)`
and is handed to the review map the same way `-ControlRecap=<name>` is, so both paths resolve
through the same code.

### 12.3 Listing recordings

Two surfaces over one data source. Build the row data once, in a `BlueprintCallable` function on the
store or subsystem returning `TArray<FRecordingListEntry>` (`BlueprintType`), and have both the
console printer and the widget consume it — never format the same thing twice.

Each entry shows:

| Column | Example |
| --- | --- |
| Folder | `Recording_7` |
| Display name | `Jump tutorial` |
| Size | `141.2 MB` |
| Last updated | `1 day 5 minutes 10 seconds ago` |
| Duration | `0:47` |
| Cues | `12` |
| Contents | `ghost + mp4 + json` |
| Playable | `yes` / `no` |

**`ir.store.list`** prints this as an aligned table to the log/console, followed by a totals line:
session count, total size, quota, and headroom remaining.

**`ir.store.list.ui`** raises `WBP_RecordingList` over whatever is on screen — one
`WBP_RecordingListRow` per session, sorted most-recently-updated first, with a close button. Rows
are clickable: clicking one runs `ir.record.test <that session>`. This widget is created from the
project-settings Blueprint class like every other (§9.3).

### 12.4 Relative time formatting — exact spec

Write **one** `UFUNCTION(BlueprintPure)` helper on a Blueprint function library, used by every
surface that prints an age. Do not reimplement it anywhere.

```
FString FormatRelativeTime(FDateTime UtcTimestamp)   // and an FTimespan overload
```

Rules:

- Compute `FDateTime::UtcNow() - UtcTimestamp`.
- Break it into **days, hours, minutes, seconds**.
- Emit only the **non-zero** components, in that order, each as `<n> <unit>` with the unit
  pluralised correctly (`1 day`, `2 days`, `1 second`, `36 seconds`).
- Join with single spaces and append ` ago`.
- Zero elapsed (or under one second) → `just now`.
- A future timestamp (clock skew) → `in <same format>`, never a negative number.

Worked examples — these are the acceptance criteria:

| Elapsed | Output |
| --- | --- |
| 36 s | `36 seconds ago` |
| 1 day, 0 h, 5 min, 10 s | `1 day 5 minutes 10 seconds ago` |
| 2 h, 0 min, 3 s | `2 hours 3 seconds ago` |
| 1 min exactly | `1 minute ago` |
| 0.4 s | `just now` |
| 3 days exactly | `3 days ago` |

Note the second and third rows: a zero component in the middle is **dropped**, not printed as `0
hours`.

---

## 13. The Control Recap level — input handling

This level is a **video player with an input listener**. That framing decides everything below.

### 13.1 Block nothing

- Read **every** input the player produces. Do not consume, swallow, or gate any of it.
- **Do not use `FInputModeUIOnly`.** MatchInput reads live Enhanced Input action values to judge
  what was pressed; UI-only mode stops gameplay input reaching the input stack entirely, and every
  cue hangs forever waiting for input that can never arrive. This is easy to get backwards because
  "lock the UI to input-only" sounds like the right instinct and is exactly wrong here.
- Use `FInputModeGameAndUI` with `bConsumeCaptureMouseDown = false` and the cursor shown.
- Do **not** call `SetIgnoreMoveInput` / `SetIgnoreLookInput` either. There is nothing to move — the
  default pawn is non-interactive — and suppressing input is precisely what must not happen here.
  If a pawn ever needs to hold still, make it a pawn that does not respond to input, rather than a
  pawn whose input is being filtered.
- Push the gameplay mapping context(s) in this level so every recorded action is live and
  observable. The player pressing something wrong must produce a real, named Enhanced Input event —
  otherwise there is nothing to report.

### 13.2 Listen to everything, judge against one thing

The listener binds to **every** action in the recorded contexts, not only the expected one:

- Input matches the current cue → advance, resume the clock and the video.
- Input is any other tracked action above the press threshold → record a **mismatch** with the real
  action name and value. Do not stall, do not fail the session, do not skip the cue — keep waiting
  for the right input.
- Ignore anything in `IgnoredActions` and any frame-delta action (§5.2) — camera and mouse movement
  must never count as wrong.

### 13.3 Show what was actually pressed

Directly **below** the expected-input prompt, maintain a wrong-input list:

- Each wrong press appends a `WBP_WrongInputRow` reading, unambiguously, what was pressed — e.g.
  `You pressed: IA_Jump [Pressed]` under `Press: IA_Move [Fwd | X=+0.00 Y=+1.00]`.
- Use the same description formatter (§5.3) for both lines so the expected and actual read in
  identical phrasing and can be compared at a glance.
- Style it distinctly (a different colour) from the expected prompt.
- Cap the list (last ~5) and **clear it the moment the cue is answered correctly**. Never leave a
  stale "you got it wrong" line sitting next to a cue the player just got right — it reads as though
  *that* one was wrong too.
- Keep a running mismatch count for the session and show it.

### 13.4 Gamepad

The level must be fully usable on a pad. Two separate systems, and conflating them is the classic
mistake:

- **Moving focus between widgets is Slate's job** — `FNavigationConfig` + `UWidgetNavigation`. It
  works with zero Enhanced Input involvement. Turn on **analog-stick navigation** with a custom
  `FNavigationConfig` subclass for this map; stock Slate only responds to d-pad and arrow keys, and
  a full-screen quiz that ignores the stick reads as broken.
- **Semantic verbs** — Accept, Back, toggle record — are Enhanced Input's job, configured through
  `DA_RecordingUIInput` (a `URecordingUIInputConfig` data asset holding a dedicated
  `IMC_RecordingUI` plus a push priority above gameplay).

Do **not** bind "navigate up/down" as an Enhanced Input action and call `SetFocus` by hand. It
fights Slate's own navigation, breaks the instant a widget is added or reordered, and never matches
how mouse focus already behaves.

Keep analog navigation **off** for the corner overlay — a stick that also moves UI focus during
gameplay is a nuisance, not a feature.

---

## 14. Terminal execution — the `-IR` argument

The game must be drivable straight from a terminal, a shortcut, or a CI job:

| Command line | Result |
| --- | --- |
| *(no flag)* or `-IR=0` | Normal boot into the gameplay map. |
| `-IR=1` | Boots **directly** into `ControlRecapLevel`, reviewing the most recent playable session. |
| `-ControlRecap` | Same as `-IR=1`. |
| `-ControlRecap=Recording_5` / `-ControlRecap=5` | Review that specific session. |
| `-RecordingRoot=D:/Takes` | Read sessions from somewhere else. |

`-IR=0` is spelled out as a value rather than left as "just omit the flag" because a launcher that
always appends `-IR=<n>` needs a value meaning "boot normally". An explicit `-IR=0` also **vetoes**
`-ControlRecap`, so that launcher can override a shortcut with the long flag baked in.

### 14.1 Rewrite the map, do not travel to it

The parsing and the override must happen in the game module's **`StartupModule()`**, calling
`UGameMapsSettings::SetGameDefaultMap()`. That is the last point in the boot sequence where it is
still possible.

The obvious implementation — boot, then `OpenLevel` — loads the gameplay map first: its actors
spawn, its game mode runs, and the player sees a frame or two of a level they did not ask for.
Rewriting `GameDefaultMap` during module startup happens *before* the engine picks a map at all.
Nothing loads twice.

Register a **one-shot fallback** on `FCoreUObjectDelegates::PostLoadMapWithWorld` as a safety net
for the case where module startup somehow ran after map resolution. It must log loudly, self-
unregister after firing once so it never fights a legitimate later level change, and unbind on
module shutdown.

### 14.2 Parse whole tokens, not substrings

`FParse::Value(CmdLine, TEXT("IR="), ...)` substring-matches — asking for `IR=` also finds the tail
of `-SomeDir=...` and hands back that path. With a two-letter switch that is a real collision, not a
theoretical one. Walk whole tokens with `FParse::Token`, strip a leading `-` or `/`, split on `=`,
and compare the key exactly. Trim quotes off the value — nobody writes `-IR="1"` by hand, but a
batch file building the command line from a variable does.

An unparseable `-IR=<junk>` should **warn** and boot normally, not fail silently. That is ten
minutes somebody would otherwise spend staring at a shortcut.

Expose the parsed flags to Blueprint through a `UBlueprintFunctionLibrary` (`GetBootMode()`,
`IsControlRecapBoot()`, `GetRequestedSessionFolder()`, `DescribeBootFlags()`), so a menu can show a
"Review last recording" entry only when the build was launched for review.

---

## 15. Gotchas — get these right the first time

1. **Video orientation is machine-dependent and genuinely ambiguous.** `UMediaCapture`'s CPU
   readback delivers frames top-down; Media Foundation's raw RGB surface convention is bottom-up
   (legacy DIB order). Whether the final `.mp4` is inverted depends on which conversion the sink
   writer inserts, which you cannot determine by reading documentation or reasoning about the
   pipeline. **Build `ir.video.dumpframe` first and check empirically on the target machine**, then
   set the orientation flag. Do not ship a guess. Make the setting a named enum
   (`Auto` / `TopDown` / `BottomUp`), not a bool — "flip" means nothing until you know what it is
   flipping from, and every bug in this area came from two pieces of code each assuming the other's
   convention.

2. **`UMediaTexture` is a `UTexture` but not a `UTexture2D`.** Assign it to a `UImage` brush through
   `SetBrushResourceObject()`, not `SetBrushFromTexture()` — the typed setter silently rejects it or
   refuses to compile depending on how you call it.

3. **Input lockout on the review map — see §13.1.** `FInputModeUIOnly` breaks the entire feature.

4. **Slate navigation vs Enhanced Input — see §13.4.** Two systems. Do not conflate them.

5. **Boot-time map override must precede map selection — see §14.1.**

6. **`BindWidgetOptional`, never `BindWidget` — see §9.1.**

7. **A widget field only ever filled in by a Blueprint subclass is a trap for any C++-created
   instance.** If any path creates a widget straight from its C++ class — a full-screen widget
   instantiated by a player controller, with no placed Blueprint assigning the field — that field is
   silently null and every lookup through it silently returns nothing. The symptom looks exactly
   like "the data asset is misconfigured". Give every designer-content reference **two** paths: a
   per-widget override field *and* a project-settings fallback, resolved as "override if set, else
   the setting", everywhere it is used. (Following §9.1 — always instantiate the Blueprint child —
   avoids most of this, but the fallback is still required.)

8. **Building the editor target requires the editor closed.** UBT refuses a build while Live Coding
   is active, and new `UCLASS`es cannot be hot-reloaded in anyway — you need a real editor-target
   rebuild before any new C++ class shows up in the Create Blueprint / Create Data Asset dialogs.
   Close the editor, build, relaunch. Plan your MCP work around this: **all new C++ classes must
   compile and the editor must be restarted before any MCP Blueprint creation that derives from
   them.**

9. **A modular editor DLL needs dependencies a monolithic game target gets away with omitting.**
   `UGameMapsSettings` lives in the `EngineSettings` module; that must be in `.Build.cs` or only the
   editor build fails to link. If a symbol resolves in one target and link-fails in the other, check
   the module list before assuming the code is wrong.

10. **Flat module layout needs `PublicIncludePaths.Add(ModuleDirectory)`.** With no Public/Private
    split, UBT does not add the module root to the include path automatically, and cross-subfolder
    includes (`#include "InputReplay/InputReplayTypes.h"`) will not resolve.

---

## 16. Build order — work through this in sequence

Build the editor target and confirm a clean compile after each step. Do not let compile errors stack
across modules.

| # | Deliverable | Verified by |
| --- | --- | --- |
| 1 | `FInputRecording` + `UInputReplayComponent`, save/load binary + JSON | Record a short take, read the `.ghost.json` |
| 2 | Cue extraction + MatchInput playback mode | Console logging of each cue against a recorded file |
| 3 | Video capture pipeline + `ir.video.dumpframe` | The frame-dump PNG — **before** any UI is written around it |
| 4 | `URecordingStore` — folders, index, quota, LRU eviction | `ir.store.list` / `ir.store.trim` |
| 5 | `UInputRecordingSubsystem` tying 1–4 together | Console commands only, still no UI |
| 6 | The `UDeveloperSettings` asset, every module reading from it | Project Settings page populated |
| 7 | Boot flags: `-IR=0`, `-IR=1`, `-ControlRecap[=name]` | Launch from a terminal both ways |
| 8 | Console surface incl. `ir.record.test [session]` and both list commands | §12.4's worked examples |
| 9 | All C++ widget classes | Compile; `ValidateBindings()` logs cleanly |
| 10 | **Rebuild editor, restart**, then create every `WBP_*` Blueprint via MCP | Widgets render |
| 11 | Review map + GameMode/PlayerController Blueprints via MCP | Test path and `-IR=1` path both land in it |
| 12 | Gamepad pass (§13.4) | Full recap flow completed with a pad, no keyboard |
| 13 | Full pass | See §17 |

### 16.1 Final acceptance pass

1. Record a take with a gamepad connected; confirm the overlay never exceeds its area cap at 16:9,
   21:9, and 4:3.
2. `ir.store.list` — confirm sizes and relative times against §12.4's table.
3. `ir.store.list.ui` — same data on screen; click a row to review it.
4. `ir.record.test` with no argument → most recent. With `Recording_N` → that one. With a bogus name
   → a clear error naming what was asked for, and no fallback.
5. In the recap map: answer a cue correctly, answer several wrong and confirm each wrong press is
   named below the expected prompt, then answer correctly and confirm the wrong list clears.
6. Cancel out; confirm it lands on the configured gameplay map.
7. Relaunch from a terminal with `-IR=1`; confirm zero frames of the gameplay map ever render.
8. Fill the store past quota; confirm LRU eviction, and confirm a take stopped by quota is saved and
   *reported* as quota-stopped.

---

## 17. What you need from me — document it

Maintain a single `Docs/InputRecorder_Setup.md` as you go, and keep a running list in your replies
of anything you need from me. Be specific and exhaustive about:

- Every `.uproject` plugin to enable, and whether you enabled it or I have to.
- Every moment you need the **editor closed** for a C++ build, and every moment you need it **open**
  for MCP asset work. Say it plainly at the point you need it — do not batch these into a final
  list.
- Any asset you cannot create through MCP (sprites and textures for the icon mapping are the likely
  case) with an exact name, size, and where it goes.
- Every Project Settings value I need to verify by eye after you write it.
- The exact command lines for: building the editor target, building the game target, launching
  normally, and launching in review mode.
- Which gotcha in §15 was resolved empirically on this machine (orientation especially) and what the
  answer turned out to be.

---

## 18. Style

Match the codebase's existing conventions. Where a decision is non-obvious — and most of the ones in
this document are — write a short comment explaining **why the obvious alternative is wrong**, not
what the code does. The failure modes named throughout this prompt are exactly the comments worth
keeping.

---

*End of prompt.*
