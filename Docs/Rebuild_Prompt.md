# Prompt: Build the Input Recording + Control Recap system

Copy everything below the line into a fresh Claude Code session, in a new or existing Unreal Engine
5.x C++ project. It is written as a single request — read it fully before starting, since later
sections depend on decisions made in earlier ones.

---

## 0. Starting point / prerequisites

Assume an Unreal Engine 5.x project using **Enhanced Input** (not the legacy input system), with a
playable pawn already wired to a set of `UInputAction` assets through an `UInputMappingContext`. The
Third Person C++ template is a good baseline: it already has `IMC_Default` with `IA_Jump`, `IA_Move`,
`IA_Look`, `IA_MouseLook`.

If Enhanced Input is not yet set up, set that up first — the system below is built entirely on top of
it and has no fallback for the legacy input system.

State your engine version and confirm whether you're starting in a template project or an existing
one, then proceed.

## 1. What this system is, in one paragraph

A designer or tester plays the game normally while their input is recorded (every Enhanced Input
action value, timestamped) alongside a screen-capture video of what they saw. Later, anyone can watch
that video with an input timeline overlaid on it — a "ghost" of what was pressed and when — and
optionally get quizzed: the video plays, pauses at each significant input, and waits for the viewer to
press the same thing before it continues. This is for teaching controls, QA repro capture, and
tutorial creation. Everything is local files; there is no networking.

## 2. Non-negotiable architecture decisions

These are decisions that took real iteration to get right. Follow them as stated rather than
re-deriving a different structure — each one exists because the obvious alternative has a specific
failure mode, noted inline.

1. **One `UGameInstanceSubsystem`** (`UInputRecordingSubsystem`) owns everything: the replay
   component lookup, the video player, the session store, and both UI widgets. Widgets bind to *it*,
   never directly to a `UInputReplayComponent` — that component lives on a `PlayerController` or pawn
   and dies on every respawn/travel, which would leave a widget holding a stale pointer. The subsystem
   outlives all of that and re-resolves the component lazily on first use each time.

2. **Recording is a `UActorComponent`** (`UInputReplayComponent`), not baked into a PlayerController
   subclass. It resolves itself onto whichever controller/pawn exists, and can be auto-created by the
   subsystem if the project settings allow it. This means the system attaches to *any* project's
   character setup without requiring a particular controller class hierarchy.

3. **Recordings live in dated, indexed session folders**, not bare files:
   ```
   <RecordingRoot>/Recording_7/Recording_7.ghost   (binary input timeline)
                                Recording_7.ghost.json (optional, human-readable copy)
                                Recording_7.mp4      (video)
                                Session.json          (metadata: duration, cue count, timestamps)
   ```
   A folder is the unit of storage, eviction, and review — never a bare filename. `RecordingIndex.json`
   at the root holds the next never-reused index counter, so indices stay stable even after eviction
   deletes folders out of order.

4. **A storage quota with LRU eviction**, because unattended video recording will happily fill a disk.
   Total size is capped (default ~900MB); when a new take needs room, the *least recently updated*
   session is deleted first (not oldest by creation — reviewing a session counts as "using" it and
   should protect it). The session currently recording and the one currently under review are always
   pinned and never evicted, even if they're technically LRU. If the quota is hit *during* a take, the
   take is stopped (not another session evicted mid-write) — the partial recording is still saved.
   Evicting a different session while this one is still being written trades a finished recording for
   an unfinished one, which is the wrong trade.

5. **Two review surfaces, kept genuinely separate**, not two configurations of one widget:
   - A small **corner overlay** (pinned bottom-right, capped at a fraction of screen *area* — not
     width and height independently, since those disagree at unusual aspect ratios) with Start/Stop,
     Test, and a live sync-point history. This sits on top of the gameplay map while the player is
     actually playing.
   - A **full-screen standalone review map** (`ControlRecapLevel`) with its own `AGameModeBase`
     subclass and `APlayerController` subclass, entered either by clicking Test or by launching the
     game with `-ControlRecap`. It owns the whole screen, locks gameplay input, and plays back a
     chosen session with a quiz-style match-input flow.

   Do not build one widget that tries to be both. The full-screen one needs its own game mode because
   it needs to lock movement input while still evaluating Enhanced Input action values (see gotcha
   #3 below) — that is a controller-level concern, not something a widget pushed over the gameplay
   HUD can do cleanly.

6. **Both UI widget C++ classes build no widget tree of their own.** Every visual element is a
   `meta = (BindWidgetOptional)` `UPROPERTY`, filled in by a Blueprint child in the UMG designer. C++
   holds only logic (subsystem event bindings, per-frame refresh, timeline math). This is what makes
   the UI actually editable by a designer instead of only a programmer. Use `BindWidgetOptional`, not
   `BindWidget` — see gotcha #6 for why.

7. **Console commands are registered as module-level `FAutoConsoleCommand` objects**, not `UFUNCTION
   (Exec)` on a PlayerController. Exec functions only route through whatever class the console happens
   to dispatch to, and silently stop working if that class changes. A console command object registers
   once at module load and works from any world state, including before a PlayerController exists.

8. **Video capture writes straight into the claimed session folder**, and the folder is claimed (index
   reserved, directory created) only *after* input recording has actually started successfully — never
   before. A failed recording start should never burn a folder index or leave an empty directory.

## 3. Module A — the recording/replay core

Build the underlying data model first.

- `FInputRecording`: a header (start time, logical-tick rate, target actions) plus a flat time-ordered
  array of samples. Each sample is `{ FName ActionName, float TimeSeconds, uint8 ValueType (bool/
  axis1D/axis2D/axis3D), FVector Value }`. `Value` is always stored as `FVector` regardless of the
  action's real dimensionality — simpler serialization, and the ValueType tag tells consumers how much
  of it is meaningful.
- Provide **two timing modes**: `RealTime` (wall-clock `GetWorld()->GetTimeSeconds()`) and
  `FixedLogicalStep` (a fixed-tick counter, e.g. 60/sec, independent of frame rate). Fixed logical step
  exists because MatchInput playback needs frame-rate-independent determinism — a recording made at
  144fps must line up identically when reviewed at 30fps.
- `UInputReplayComponent : public UActorComponent`:
  - Modes: `Idle`, `Recording`, `PlayingGhost`, `MatchingInput` (an enum, `EInputReplayMode`).
  - `StartRecording()` / `StopRecording()`: on start, binds delegates to every `UInputAction` reachable
    through the configured `UInputMappingContext`(s) via the local player's
    `UEnhancedInputLocalPlayerSubsystem`, and appends a sample on every triggered/value-changed event.
  - **Two configurable action lists layered on top of "everything in the mapping contexts":**
    - `AdditionalActions`: actions to also track even if not in a recorded context (rare).
    - `FrameDeltaActions`: actions whose values are **relative-per-frame** (e.g. mouse look delta)
      rather than absolute state. These need special handling during MatchInput comparison — a
      "wrong" mouse-look delta should never fail a match, since the exact delta a player produces is
      never going to match a prior recording pixel-for-pixel. Default this to include mouse-look-style
      actions and exclude them from match failure by default (see Module B).
  - **A whitelist filter, configurable per-component and via project settings:**
    `EInputRecordingFilterMode { RecordAll, WhitelistOnly }` plus a `TArray<TSoftObjectPtr<UInputAction>>
    RecordedActionWhitelist`. Whitelist mode is *subtractive only* — it narrows the set already
    reachable through the mapping contexts, it never adds actions from outside them. A component whose
    filter is still at the default `RecordAll` is treated as "not yet configured by hand" and the
    project setting is free to push a whitelist into it on first use; a component already set to
    `WhitelistOnly` keeps its own list untouched. Log a warning if a whitelist ends up excluding every
    reachable action, since that produces a silently-empty recording that looks identical to "nothing
    was pressed."
  - Save/load to a binary format via a serializer class (`UInputReplaySerializer` or similar) —
    accept an **absolute base path with no extension**, and append `.ghost` / `.ghost.json` /
    `.mp4` yourselves at each call site. This is what lets the session-folder layout (§2.3) work
    without string surgery scattered through the codebase: one base path produces every file that
    belongs to a take.
  - Also support saving/loading as JSON (human-readable, for debugging and hand-editing) as an
    alternate format alongside the binary — same data, `bAsJson` flag on the save/load call.
  - `GetLiveInputSnapshot(OUT ActionName, OUT Value) -> bool`: returns whichever tracked action is
    currently non-zero/pressed, for a live "what's being pressed right now" HUD read-out. Return false
    when nothing is active.
  - Broadcast a delegate on every recorded sample (`ActionName, TimeSeconds, Value`) so a UI can build
    a running history list without polling.

## 4. Module B — MatchInput (the quiz/ghost-comparison system)

- `FMatchInputCue`: `{ TSoftObjectPtr<UInputAction> Action, FString ActionName, float TimeSeconds,
  uint8 ValueType, FVector Value }`. Store **both** a soft path to the action and its short name string
  — the soft path is precise, but it goes stale if the action asset is later renamed or moved; the
  short name is what survives a project reorganization. Every consumer should try the soft path first
  and fall back to the name.
- A cue-extraction pass (`BuildMatchInputCues`) walks a full `FInputRecording` and picks out the
  significant "onset" moments — not every sample, just the ones that cross a press threshold — using
  configurable options: `PressThreshold` (magnitude to count as "pressed"), `MinimumCueSpacing`
  (debounce so a held stick doesn't spam cues), `bIgnoreFrameDeltaActions`, and `IgnoredActions`
  (explicit exclusion list — default this to exclude look/camera actions, since camera movement should
  never count as a wrong answer).
- MatchInput playback mode on the component: given a loaded recording and its extracted cue list, play
  a virtual clock forward. At each cue's timestamp, **pause the clock** and wait for the live player to
  produce a matching input (same action, same rough direction/magnitude within a tolerance for
  analog values) before resuming. Broadcast delegates for: cue presented (index, total, expected input
  description), input matched (index, total), input mismatched (expected description, what was
  actually received), and match finished (bool: completed all cues vs. cancelled early).
- Provide a description formatter — one function, used everywhere something needs to show
  `"IA_Jump [Pressed]"` or `"IA_Move [Fwd-Right | X=+0.71 Y=+0.71]"` as text, so every log line and
  every UI label agree on the exact same phrasing.

## 5. Module C — synchronized video capture

This is the part with the most subtle bugs. Read the gotchas section (§9) before implementing.

- A screen/viewport capture pipeline using `UMediaCapture` (via a custom `UMediaOutput` subclass) to
  grab frames, feeding them into a software H.264 encoder through Media Foundation's sink writer
  (Windows). Wrap the raw sink-writer plumbing in its own backend class, separate from the
  Unreal-facing capture class — you will want to unit-test/inspect the encoder in isolation.
- **Capture at native viewport resolution.** Do not add a scale-factor knob "for performance" — it
  just adds a variable that makes the orientation bug (§9.1) harder to diagnose, and 1× is the correct
  default for a review video anyone will actually want to read text in.
- Support an **explicit off-by-default resolution override** (`bOverrideResolution` +
  `ForcedResolution`) as an escape hatch, distinct from the removed "always downscale" idea above.
- Start capture only once input recording has *definitely* started (see §2.8) and stop it as part of
  the same stop-and-save call that finalizes the `.ghost` file, so the two halves of a take are always
  written and finalized together.
- A lightweight video *player* class wrapping `UMediaPlayer` + `UMediaTexture`, exposing:
  `OpenVideo(path)`, `GetMediaTexture()`, and a "sync to clock" call that seeks/scrubs the player to
  match the MatchInput virtual clock (§4) every tick, so paused-for-input moments actually pause the
  video too.
- A pure-presentation widget (`UVideoSurfaceWidget`) that is just one `UImage` bound to whatever
  `UMediaTexture` the subsystem currently has open, with an optional material-routing mode for
  colour-grading/letterboxing. Keep this widget dumb and reusable — it should have zero knowledge of
  recording, MatchInput, or anything else; any screen that needs to show the live video embeds one.
- Add a debug console command that dumps the encoder's first captured frame to a PNG next to the
  output `.mp4`, from inside the encoder thread, containing the *exact bytes handed to the encoder*.
  You will need this — see §9.1.

## 6. Module D — the session store (`URecordingStore`)

- Owns the whole `<RecordingRoot>` directory: scans it, migrates any pre-existing flat-layout
  recordings (bare `Name.ghost`/`Name.mp4` pairs with no folder) into `Recording_N` folders on first
  run, and enforces the quota (§2.4).
- `<RecordingRoot>` must resolve differently per platform:
  | Platform | Root |
  |---|---|
  | Editor / Windows / Mac / Linux | `<ProjectSavedDir>/Recordings` |
  | Console | `<ProjectPersistentDownloadDir>/Recordings` |
  | Any, override | `-RecordingRoot=<path>` on the command line |

  This split matters: `ProjectSavedDir()` is not reliably writable on a packaged console build, and
  hundreds of megabytes of video does not belong mixed in with small user save-game data even where
  it would technically work.
- API surface: `Initialize(quotaBytes)`, `Rescan()`, `BeginSession(displayName, mapName,
  reserveBytes) -> FRecordingSessionInfo`, `CommitSession(index, durationSeconds, cueCount)`,
  `AbortSession(index)` (deletes the folder — for a cancelled take), `TouchSession(index)` (protects
  against LRU eviction, called when a session is opened for review), `TrimToQuota(headroomBytes)`,
  `GetMostRecentSession()`, `FindSessionByFolder(name)`, `GetTotalBytes()`, `LogInventory()`.
- `FRecordingSessionInfo` is a lightweight value struct (folder name, index, absolute path, size,
  timestamps, duration, cue count) — cheap to pass around and expose to Blueprint, backed by
  `Session.json` as the on-disk source of truth for the metadata fields, but every field should be
  re-derivable from the folder alone if `Session.json` is ever missing or corrupt (never make it the
  *only* source of truth — a folder without a manifest should still be usable, just with unknown
  duration/cue-count until rebuilt).

## 7. Module E — the subsystem (`UInputRecordingSubsystem`)

`UGameInstanceSubsystem`. Owns:

- Replay component discovery/caching (search order: cached → local PlayerController → its pawn → any
  actor in world carrying the component → auto-create on the PlayerController if project settings
  allow it).
- `StartRecording(displayName)`, `StopRecording()`, `StopRecordingAndSave(name, bJson)`,
  `StopRecordingWithoutSaving()`, `CancelRecording()` (abandon + delete the session folder — distinct
  from "stop without saving," which still leaves the folder and in-memory data around),
  `RunControlRecapTest()` (stop+save, then travel to the review map — see §8).
- `StartMatchInputFromSession(FRecordingSessionInfo)`: touches the session (LRU protection) and starts
  MatchInput playback from it.
- Owns and lazily creates the corner overlay widget and a toast/confirmation widget (see §2.6 — both
  built from a Blueprint class read out of project settings, not hardcoded).
- A quota guard that polls the store roughly once a second **while actively recording** (piggyback on
  whatever tick you already have — do not add a second ticker for this) and calls `StopRecording()`
  itself if the live file size plus already-committed session sizes would exceed quota. Flag the
  resulting save as "stopped early due to storage" so the UI can say so truthfully rather than
  claiming a normal stop.
- Delegates: mode changed, sync-point recorded (relayed from the component), video saved, recording
  saved (bool success, session path, bool wasQuotaStopped), match cue presented/matched/mismatched/
  finished (relayed from the component).

## 8. Module F — the standalone review map

- `AControlRecapGameMode : AGameModeBase` (Blueprintable): sets its own `PlayerControllerClass` and
  `DefaultPawnClass` (a plain non-interactive pawn is fine — the player never actually controls
  anything, they only answer input prompts). No `HUDClass`. Exposes `TargetOnCancelMap`
  (`FSoftObjectPath`, editable) and an optional `ForcedSessionFolder` (pins the level to one specific
  take instead of "most recent").
- `AControlRecapPlayerController : APlayerController` (Blueprintable): on `BeginPlay`, resolves which
  session to review — resolution order: (1) the game mode's `ForcedSessionFolder`, (2) a
  `-ControlRecap=<name>` command-line argument, (3) the store's most-recently-updated *playable*
  session (one with a viable `.ghost` — a folder that's still being written is newer than everything
  and must not win). Then creates the review widget (Blueprint class from project settings, §2.6),
  calls `BeginReview(session)` on it, and freezes the pawn — **this is the gotcha in §9.3, read it
  before implementing input lockout.** `LeaveRecap()` resolves a destination in order: game mode's
  `TargetOnCancelMap` → a project-setting fallback gameplay map → the engine's own default map — and
  travels there. Never leave a "nowhere to go" dead end.
- The review widget itself (§2.6: no C++-built tree, all `BindWidgetOptional`): calls
  `StartMatchInputFromSession` on the subsystem, builds one marker widget per cue on a canvas panel
  (fractional-anchor positioned along a progress bar so a marker's X position and the bar's fill share
  one coordinate space and can never visually drift apart), shows the live video, and drives:
  - a cue counter ("cue 3 / 12")
  - the progress bar + per-cue markers (three visual states: pending / active-now / completed)
  - a large "what to press now" label + icon
  - a smaller, differently-colored line showing what was actually pressed on a mismatch, that clears
    itself either after a timeout or the moment the cue is answered correctly (never leave a stale
    "you got it wrong" line sitting next to a cue the player just got right — it reads as though *that*
    one was wrong too)
  - a Cancel button
  Fire a delegate on close (`bool bCompletedAllCues`) that the player controller uses to call
  `LeaveRecap()`.

## 9. Gotchas — get these right the first time

1. **Video orientation is genuinely ambiguous and machine-dependent.** `UMediaCapture`'s CPU readback
   delivers frames top-down; Media Foundation's raw RGB surface convention is bottom-up (legacy DIB
   order). Whether the final `.mp4` actually ends up inverted depends on which conversion the sink
   writer happens to insert, which is not something you can determine by reading documentation or
   reasoning about the pipeline — **build the debug frame-dump command from §5 and use it to check
   empirically on your actual target machine**, then set the orientation flag accordingly. Do not ship
   a guess.

2. **`UMediaTexture` is a `UTexture` but not a `UTexture2D`.** Assigning it to a `UImage` widget's
   brush must go through `SetBrushResourceObject()`, not the typed `SetBrushFromTexture()` — the typed
   setter will silently reject it or refuse to compile depending on how you call it.

3. **Input lockout on the review map must NOT use `FInputModeUIOnly`.** MatchInput needs to keep
   reading live Enhanced Input action values to judge whether the player pressed the right thing;
   UI-only input mode stops gameplay input from reaching the input stack entirely, and every match cue
   will simply hang forever waiting for an input that can never arrive. Use `FInputModeGameAndUI`, and
   freeze the pawn itself with `SetIgnoreMoveInput(true)` / `SetIgnoreLookInput(true)` instead. This is
   easy to get backwards because "lock the UI to input-only" sounds like the right instinct and is
   exactly wrong here.

4. **Slate widget-focus navigation and Enhanced Input UI actions are two different systems — do not
   conflate them.** Moving focus between buttons/widgets is Slate's job (`FNavigationConfig` +
   `UWidgetNavigation`) and works with zero Enhanced Input involvement. Semantic verbs — Accept, Back,
   toggle-record, jump-to-test — are Enhanced Input's job. The common mistake is binding "navigate up/
   down" as an Enhanced Input action and calling `SetFocus` by hand: this fights Slate's own
   navigation, breaks the instant a widget is added or reordered, and never quite matches how mouse
   focus already behaves. Build a small data asset (e.g. `URecordingUIInputConfig`) that holds a
   dedicated `UInputMappingContext` for just the semantic verbs plus a push priority (above gameplay,
   so it wins on shared keys), and separately turn on **analog-stick navigation** in a custom
   `FNavigationConfig` subclass for the full-screen map (stock Slate navigation only responds to d-pad/
   arrow keys, and a menu that ignores the stick on a full-screen quiz reads as broken). Keep analog
   navigation off for the small corner overlay — a stick that also moves UI focus while the player is
   mid-gameplay is a nuisance, not a feature.

5. **Boot-time map override must happen before the engine picks a map, not after.** If you want
   `-ControlRecap` to boot straight into the review level with zero frames of the gameplay map ever
   loading, you must rewrite `UGameMapsSettings::SetGameDefaultMap()` from your game module's
   `StartupModule()` — that is the last point in the boot sequence where it's still possible. Doing it
   from a subsystem `Initialize()` or later is already too late; the engine has committed to a map
   choice by then, and you'll see a flash of the wrong level. Register a one-shot fallback on
   `FCoreUObjectDelegates::PostLoadMapWithWorld` as a safety net in case something ever causes module
   startup to run after map resolution (it should log loudly and self-unregister after firing once, so
   it never fights a legitimate later level change).

6. **`BindWidget` vs `BindWidgetOptional` — use optional, and say why in a comment.** Strict
   `BindWidget` fails Blueprint *compilation* the instant any single name doesn't match what's in the
   tree, one problem at a time, which makes it painful to iterate on a layout — you cannot save a
   partially-built Blueprint. Use `BindWidgetOptional` everywhere, null-check every one of them before
   use in C++, and write your own `ValidateBindings()` pass called once on construct that logs every
   missing hook in a single message. This gets you fast iteration with the same eventual safety net.

7. **A widget field like `TObjectPtr<USomeDataAsset> IconMapping` that only gets filled in by a
   Blueprint subclass is a trap for any C++-only instance of that widget.** If any code path ever
   creates the widget straight from its C++ class (e.g., a full-screen map widget instantiated by a
   player controller, with no placed Blueprint asset assigning the field), that field is silently
   null, and every lookup through it silently returns nothing. The symptom looks identical to "the
   data asset is misconfigured" — it is not. Give every such "designer content" reference two paths: a
   per-widget override field *and* a project-settings fallback loaded through
   `UDeveloperSettings::Get()`, and resolve it as "override if set, else the setting" everywhere it's
   used, so a widget with no manually-assigned override still works.

8. **Building the Editor target requires the editor closed.** UnrealBuildTool will refuse a Live
   Coding–active editor build with `"Unable to build while Live Coding is active."` New `UCLASS`es
   cannot be hot-reloaded in through Live Coding anyway — you need a real editor-target rebuild before
   any new C++ class is visible to Create Blueprint / Create Data Asset dialogs. Close the editor,
   build `<ProjectName>Editor Win64 Development`, then relaunch. A modular editor DLL also needs
   dependencies declared in the `.Build.cs` that a monolithic game-target build can get away with
   omitting — e.g. `UGameMapsSettings` lives in the `EngineSettings` module; that dependency is
   required for `<ProjectName>Editor` even if `<ProjectName>` (the standalone game target) links fine
   without it. If a symbol resolves in one target and link-fails in the other, check the `.Build.cs`
   module list before assuming the code itself is wrong.

## 10. Project settings (one `UDeveloperSettings` asset)

Put everything a designer might want to tune in one config class (`Project Settings > Game > <Your
System Name>`), `UPROPERTY(config, EditAnywhere)`, backed by `DefaultGame.ini`. At minimum:

- Recorded mapping contexts, additional/frame-delta actions, default recording name
- `TimeMode`, logical-tick rate
- MatchInput cue-build options (press threshold, spacing, ignored actions), match direction tolerance
- Whitelist filter mode + whitelist array (§3)
- Quota megabytes, per-take reserve megabytes, whether to hard-stop on quota (§2.4)
- Control Recap map soft path, a fallback "gameplay map" soft path (for Cancel), the UI input config
  data asset (§9.4)
- The icon-mapping data asset (action → sprite, for prompts and history rows), as a project-level
  fallback per gotcha #7
- Widget classes for the corner overlay, the toast, and the review-map widget, as `FSoftClassPath`
  (a `UGameInstanceSubsystem` and a player controller have no details panel to assign a widget class
  in the normal way — this settings asset is where that assignment has to live)
- Video capture options: target frame rate, bitrate, resolution override toggle, orientation (§9.1)

## 11. Console command surface

Register (module-level `FAutoConsoleCommand`/`FAutoConsoleCommandWithWorldAndArgs`, §2.7):
`<prefix>.record.start [name]`, `<prefix>.record.stop`, `<prefix>.record.test`,
`<prefix>.record.cancel`, `<prefix>.store.list`, `<prefix>.store.trim`, `<prefix>.ui.show`,
`<prefix>.ui.hide`, `<prefix>.video.dumpframe` (§5). Log everything storage/quota-related through one
dedicated log category so `log <Category> Verbose` gives a complete picture of disk activity with zero
input/render noise mixed in.

## 12. Deliverables checklist

Work through this in order — each layer depends on the one before it compiling and working:

1. `FInputRecording` + `UInputReplayComponent` recording and save/load (binary + JSON), verified by
   recording a short session and inspecting the JSON output.
2. `FMatchInputCue` extraction + MatchInput playback mode on the component, verified against a
   recorded file with console logging of each cue.
3. Video capture pipeline + orientation debug command, verified with the frame-dump PNG before writing
   any UI around it.
4. `URecordingStore` (folders, index, quota, migration, LRU eviction), verified via
   `<prefix>.store.list` / `<prefix>.store.trim` before any UI depends on it.
5. `UInputRecordingSubsystem` tying components 1–4 together, verified via console commands only, no UI
   yet.
6. Project settings asset (§10), with every module above reading its config from it.
7. Corner overlay widget (C++ logic class + `BindWidgetOptional` + a Blueprint child built in UMG).
8. Review map: game mode, player controller, review widget, boot flag — verified by both the in-game
   Test button path and a direct `-ControlRecap` command-line launch.
9. Controller/gamepad support (§9.4): UI input config data asset, mapping context, analog nav config.
10. Full pass: record a take with a gamepad connected, click Test, complete or fail a few cues with the
    pad, confirm Cancel returns to the configured map, confirm the corner overlay never exceeds its
    screen-area cap at multiple aspect ratios.

Build the editor target and confirm a clean compile after every deliverable above lands — don't let
compile errors stack across multiple modules; §9.8 explains why building itself has one non-obvious
requirement (editor closed).

---

*End of prompt.*
