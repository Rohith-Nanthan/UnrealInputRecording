# Input Recorder — Setup & Handover

Living document. Updated as the build progresses.

- **Engine:** Unreal Engine 5.8 (`F:\Programs\Epic Games\UE_5.8`)
- **Project:** existing Third Person template project, `UnrealInputRecording`
- **Module:** `UnrealInputRecording` (primary game module, flat layout)

---

## 1. Plugins

All four required plugins were **already enabled** in `UnrealInputRecording.uproject` before this
build started. Nothing for you to do here.

| Plugin | Status | Why |
| --- | --- | --- |
| `EnhancedInput` | on by default | The whole system is built on it. |
| `MediaIOFramework` | already enabled | `UMediaOutput` / `UMediaCapture`. |
| `WmfMedia` | already enabled | `UMediaPlayer` playback of the captured `.mp4`. |
| `ModelContextProtocol` + `AllToolsets` | already enabled | The MCP editor bridge. |

`Config/DefaultEditorPerProjectUserSettings.ini` already sets `bAutoStartServer=True` and
`ServerPortNumber=8000`, so the MCP bridge comes up with the editor.

---

## 2. Build commands

The engine is **not in the registry** (`HKLM\SOFTWARE\EpicGames\Unreal Engine` lists 4.24–5.4 but
has no 5.8 key), so the engine path has to be spelled out. Run these from PowerShell.

**Editor target** — needed before any new C++ class appears in the editor:

```bash
& "F:\Programs\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" UnrealInputRecordingEditor Win64 Development -Project="F:\Projects\Unreal Engine\Development\UnrealInputRecording\UnrealInputRecording.uproject" -WaitMutex
```

**Game target** — builds `UnrealInputRecording.exe`, and works with the editor open:

```bash
& "F:\Programs\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" UnrealInputRecording Win64 Development -Project="F:\Projects\Unreal Engine\Development\UnrealInputRecording\UnrealInputRecording.uproject" -WaitMutex
```

### Launch commands

Normal boot:

```bash
& "F:\Projects\Unreal Engine\Development\UnrealInputRecording\Binaries\Win64\UnrealInputRecording.exe" -IR=0
```

Boot straight into the review map on the most recent take:

```bash
& "F:\Projects\Unreal Engine\Development\UnrealInputRecording\Binaries\Win64\UnrealInputRecording.exe" -IR=1
```

Review one specific take:

```bash
& "F:\Projects\Unreal Engine\Development\UnrealInputRecording\Binaries\Win64\UnrealInputRecording.exe" -ControlRecap=Recording_5
```

Read takes from somewhere else:

```bash
& "F:\Projects\Unreal Engine\Development\UnrealInputRecording\Binaries\Win64\UnrealInputRecording.exe" -IR=1 -RecordingRoot=D:/Takes
```

---

## 3. Editor open / editor closed — the running list

This is the exhaustive list of moments where I need you to do something. Each is flagged in
conversation at the point it comes up, not batched here.

| # | What | State needed | Status |
| --- | --- | --- | --- |
| 1 | Build the editor target so the new `UCLASS`es exist | **editor CLOSED** | ✅ done |
| 2 | Relaunch the editor so the MCP bridge is up | **editor OPEN** | ✅ done |
| 3a | Review map, GameMode/PlayerController Blueprints, IMC/IA assets, data assets via MCP | **editor OPEN** | ✅ done |
| 3b | Create every `WBP_*` via MCP | **editor OPEN** | ✅ done |
| 3c | Write settings into `DefaultGame.ini` via MCP | **editor OPEN** | ✅ done |
| 4 | Import icon sprites (I cannot author textures) | **editor OPEN** | ⬜ pending — optional, UI falls back to text |
| 5 | Verify Project Settings values by eye | **editor OPEN** | ⬜ pending — see §8 |
| 6 | **Free space on C:** — currently 0 bytes, blocks running the game at all | — | 🔴 **blocked, needs you** |

New `UCLASS`es cannot be hot-reloaded in, and UBT refuses to build while Live Coding is active —
verified on this machine: the editor-target build failed with *"Unable to build while Live Coding
is active"*.

---

## 4. C++ that now exists

All of it compiles clean against the **game** target. Nothing has been link-checked against the
modular editor DLL yet — that is blocked on item 1 above.

```
Source/UnrealInputRecording/
├─ UnrealInputRecording.{h,cpp}          module; -IR map override in StartupModule
├─ InputRecordingLog.{h,cpp}             LogInputRecording / LogRecordingStore /
│                                        LogMatchInput / LogRecordingVideo
├─ Boot/RecordingBootFlags.{h,cpp}       whole-token command-line parsing, URecordingBootLibrary
├─ Console/InputRecordingConsoleCommands.cpp   every ir.* command
├─ ControlRecap/
│   ├─ ControlRecapGameMode.{h,cpp}
│   ├─ ControlRecapPlayerController.{h,cpp}
│   ├─ ControlRecapPawn.{h,cpp}          non-interactive pawn
│   └─ ControlRecapNavigationConfig.h    analog-stick Slate navigation, this map only
├─ InputReplay/
│   ├─ InputReplayTypes.{h,cpp}          FRecordedInputSample / FInputRecordingHeader / FInputRecording
│   ├─ InputReplayComponent.{h,cpp}      recording + MatchInput state machine
│   ├─ InputRecordingSerializer.{h,cpp}  .ghost binary + .ghost.json
│   └─ RecordingPlayerController.{h,cpp} optional gameplay PC base that forwards input hooks
├─ Library/InputRecordingFormatLibrary.{h,cpp}  FormatRelativeTime, byte size, duration
├─ MatchInput/
│   ├─ MatchInputTypes.h                 FMatchInputCue, FMatchInputCueBuildOptions
│   └─ MatchInputCueBuilder.{h,cpp}      cue extraction + the one description formatter
├─ Settings/
│   ├─ InputRecordingSettings.{h,cpp}    the UDeveloperSettings
│   ├─ RecordingUIInputConfig.h          UI verbs data asset
│   └─ InputIconMapping.{h,cpp}          action → sprite
├─ Store/
│   ├─ RecordingSessionTypes.{h,cpp}     FRecordingSessionInfo / Stats / ListEntry
│   └─ RecordingStore.{h,cpp}            folders, index, quota, LRU eviction
├─ Subsystem/InputRecordingSubsystem.{h,cpp}
├─ UI/  (10 widget classes, none of which build a widget tree)
└─ Video/
    ├─ InputRecordingVideoTypes.h        EInputRecordingVideoOrientation, FInputRecordingVideoOptions
    ├─ IVideoEncoderBackend.h            plain C++ interface + factory
    ├─ WindowsMediaFoundationEncoder.{h,cpp}   H.264 sink writer
    ├─ VideoEncoderPipeline.{h,cpp}      bounded queue + encoder thread
    ├─ InputRecordingMediaOutput.{h,cpp} UMediaOutput / UMediaCapture subclasses
    ├─ InputRecordingVideoCapture.{h,cpp}
    └─ InputRecordingVideoPlayer.{h,cpp}
```

### One deviation from the brief, and why

§4.3 asks for `PreProcessInput` / `PostProcessInput` to be forwarded from the owning
PlayerController. That is implemented, and `AControlRecapPlayerController` does exactly it. But the
gameplay map uses `BP_ThirdPersonPlayerController`, which does not derive from anything of ours, so
in the gameplay map there was nothing to forward *from*.

Rather than force a class hierarchy on the project — which §3.2 explicitly says this system must
not do — `UInputReplayComponent` also enables a component tick with a **tick prerequisite on the
PlayerController actor**. `TickPlayerInput` runs inside the controller's `TickActor`, so a
prerequisite on that actor puts the component after input evaluation *in the same frame*: the same
freshness `PostProcessInput` gives. When a controller does forward, the frame-counter guard makes
the tick a no-op, so the two paths cannot double-step the clock.

`ARecordingPlayerController` is provided for projects that would rather reparent. I intend to
reparent `BP_ThirdPersonPlayerController` to it during the MCP pass, which makes the gameplay map
use the explicit path too — the tick then stays purely as the fallback for other projects.

---

## 4a. Assets created so far

```
/Game/RecordingFolder/
├─ Maps/ControlRecapLevel                 GameMode override → BP_ControlRecapGameMode
├─ Blueprints/
│   ├─ BP_ControlRecapGameMode            PlayerControllerClass → BP_ControlRecapPlayerController
│   │                                     TargetOnCancelMap → /Game/ThirdPerson/Lvl_ThirdPerson
│   └─ BP_ControlRecapPlayerController
├─ Input/
│   ├─ IMC_RecordingUI                    5 mappings, see below
│   ├─ IA_UI_Accept
│   ├─ IA_UI_Back
│   └─ IA_UI_ToggleRecord
└─ DataAssets/
    ├─ DA_RecordingUIInput                context + priority 100 + all three actions wired
    └─ DA_InputIcons                       4 entries with labels, no textures yet
```

`ControlRecapLevel` was duplicated from `/Engine/Maps/Templates/Template_Default` because no MCP
toolset exposes level creation. Its scenery was then stripped — sky atmosphere, volumetric cloud,
height fog, floor, the static mesh and the default brush are all gone. `PlayerStart`,
`DirectionalLight` and `SkyLight` remain. With no sky atmosphere the background renders black,
which is what you want behind a review video rather than a bright sky competing with it.

`BP_ThirdPersonPlayerController` has been **reparented** from `APlayerController` to
`ARecordingPlayerController` and recompiled, so the gameplay map now uses the explicit
`PostProcessInput` forwarding path. The component's tick fallback stays in place for any project
that does not reparent.

### IMC_RecordingUI key mappings — and the conflict they avoid

| Action | Keys |
| --- | --- |
| `IA_UI_Accept` | `Enter`, `Gamepad_Special_Right` (Start / Menu) |
| `IA_UI_Back` | `Escape`, `Gamepad_Special_Left` (Back / View) |
| `IA_UI_ToggleRecord` | `F9` |

The obvious choices — Space and the gamepad A button for Accept — are deliberately **not** used.
`IMC_Default` maps `IA_Jump` to exactly those two keys, and `DA_RecordingUIInput` pushes the UI
context at priority 100, *above* gameplay. Enhanced Input would let the higher-priority context
consume the key, `IA_Jump` would never fire in the review map, and every Jump cue would be
unanswerable — the precise failure mode §13.1 warns about, arriving through the mapping table
instead of through the input mode.

Slate still activates a focused button with the A button through its own navigation config, which
is the separate system and needs no Enhanced Input mapping at all.

**Verify by eye:** if you later add a gameplay action bound to `Enter`, `Escape`,
`Gamepad_Special_Left/Right` or `F9`, the same collision returns and those cues become
unanswerable.

---

## 5. Console commands

| Command | Behaviour |
| --- | --- |
| `ir.record.start [name]` | Start a take, show the overlay. |
| `ir.record.stop` | Stop, save the session, hide the overlay. |
| `ir.record.cancel` | Abandon the take and delete its folder. |
| `ir.record.test [session]` | Review. No argument = most recent. Accepts `Recording_5`, `5`, or a display name. |
| `ir.store.list` | Aligned table of every session plus a quota totals line. |
| `ir.store.list.ui` | Same data in game; rows are clickable. |
| `ir.store.trim` | Evict LRU sessions until back under quota. |
| `ir.ui.show` / `ir.ui.hide` | Show/hide the corner overlay without starting a take. |
| `ir.video.dumpframe` | Arm the one-shot first-frame PNG dump. |

---

## 5a. Widgets created

All nine `WBP_*` Blueprints exist under `/Game/RecordingFolder/Widgets/`, compile clean, and every
`BindWidgetOptional` property resolved — verified through `GetWidgets`, where a bound widget reports
`bInherited: true`. `WBP_ControlRecap` reports `inheritedWidgetCount: 12`, matching the twelve
bindings on `UControlRecapWidget` exactly.

Layout notes worth knowing:

- `WBP_ControlRecap` root is a full-bleed `Border` over a `VerticalBox`: header → video → cue
  counter → track → prompt → wrong-input list → mismatch count → empty state.
- The track is a `SizeBox` (52px) holding an `Overlay`: `TrackProgressBar` padded 38px down so the
  bar sits at the bottom, with `MarkerCanvas` filling the whole box on top. Markers anchor
  fractionally on that canvas, so a marker's X and the bar's fill share one coordinate space.
- `VideoSizeBox` has a design-time height of 420 purely so the Blueprint previews sensibly; C++
  overwrites it every tick from `VideoScreenFraction`.
- Wrong-input rows are 13pt against the prompt's 26pt, per your instruction to reduce crowding.
  The row cap is still `MaxWrongInputRows = 5` on `UControlRecapWidget` (per §13.3's "last ~5") —
  it is an editable property on the widget if you want it lower.

## 5b. Running the game — use `-game`, not the packaged exe

`Binaries/Win64/UnrealInputRecording.exe` **will not run** against this project as it stands: it
exits immediately with *"Failed to initialize ShaderCodeLibrary … part of the Global shader library
is missing"*, because the project has never been cooked. That is expected, not a defect.

To run uncooked content, launch through the editor binary in `-game` mode:

```bash
& "F:\Programs\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "F:\Projects\Unreal Engine\Development\UnrealInputRecording\UnrealInputRecording.uproject" -game -IR=1 -windowed -ResX=1280 -ResY=720
```

The `-IR` / `-ControlRecap` / `-RecordingRoot` flags all work the same way there. The packaged exe
becomes usable once the project is cooked.

---

## 6. §15 gotchas — status on this machine

| # | Gotcha | Status |
| --- | --- | --- |
| 1 | Video orientation | ✅ **Resolved empirically on this machine, 2026-08-20 — see §10.** `Auto` (top-down, no flip) is correct. Both the encoder-input PNG and a decoded MP4 frame come out the right way up. |
| 2 | `UMediaTexture` is not a `UTexture2D` | ✅ `UVideoSurfaceWidget` uses `SetBrushResourceObject`. |
| 3 | `FInputModeUIOnly` breaks the review map | ✅ `FInputModeGameAndUI`, no `SetIgnoreMoveInput` / `SetIgnoreLookInput`. |
| 4 | Slate navigation vs Enhanced Input | ✅ `FControlRecapNavigationConfig` for focus; `URecordingUIInputConfig` for verbs. |
| 5 | Boot-time map override must precede map selection | ✅ `StartupModule` + one-shot `PostLoadMapWithWorld` net. |
| 6 | `BindWidgetOptional`, never `BindWidget` | ✅ every binding; `ValidateBindings()` reports all misses in one message. |
| 7 | Blueprint-only fields are null in C++-created instances | ✅ every designer-content reference has a per-widget override *and* a project-settings fallback. |
| 8 | Editor must be closed to build | ✅ verified: Live Coding blocks it. |
| 9 | Modular editor DLL needs `EngineSettings` | ✅ in `PrivateDependencyModuleNames` for `UGameMapsSettings`. |
| 10 | Flat layout needs `PublicIncludePaths.Add(ModuleDirectory)` | ✅ in `.Build.cs`. |

---

## 11. End-to-end verification — what has actually been run

All of the following was observed on this machine on 2026-08-20, not inferred.

### Recording (`Recording_4`, 24.65 s, 1479 logical frames, 84 samples)

- Store resolved its root to F:, logged a clean boot inventory, claimed the session folder **after**
  recording started (§3.8), and committed it with `ghost=yes video=yes json=yes`.
- `bAutoCreateReplayComponent` created the component on `BP_ThirdPersonPlayerController_C_0`.
- Header is correct: 4 action paths in sorted order, `frameDeltaActionIndices [1,2]` = `IA_Look` and
  `IA_MouseLook`, `FixedLogicalStep` at 60 Hz.
- Samples are genuinely delta-compressed — 84 samples across 1479 frames — and store the full
  vector plus the type tag, e.g. `IA_Move` forward as `{x:0, y:1, z:0}` with `valueType: 2`.
- Video: 613–1479 frames encoded across runs, **zero dropped**.

### Cue extraction

The take contained 3 `IA_Move` press onsets and 1 `IA_Jump` onset. The builder produced **exactly 4
cues**, silently discarding all **71** `IA_MouseLook` samples as frame-delta noise and ignoring
`IA_Look`. Re-running extraction at review time produced the same 4 cues — it is deterministic.

### Review (`-IR=1`)

- The boot flag rewrote the default map during `StartupModule`: *"Boot flags: mode=ControlRecap,
  default map rewritten to …ControlRecapLevel"*. The gameplay map never loaded (§14.1).
- Session resolution skipped `Recording_1/2/3` (no `.ghost`, so not playable) and picked
  `Recording_4` — the "most recently updated **playable**" rule in §7.3 working as intended.
- Widgets, cue markers and video all came up; the `.mp4` opened for synchronised playback.
- The virtual clock held: cue 1 was presented ~9.7 s in, against a recorded timestamp of 9.05 s.
- **Direction matching is correct.** Pressing `W` against a `[Right]` cue was rejected; pressing `D`
  matched it. The dot-product test is doing real work, not just presence-checking.
- **Wrong-input reporting names what was actually pressed**, through the shared formatter:
  `expected IA_Move [Right | X=+1.00 Y=+0.00], received IA_Jump [Pressed]`.
- Finished cleanly: *"4 of 4 cue(s) answered, 4 mismatch(es)"*, then `LeaveRecap` travelled to
  `/Game/ThirdPerson/Lvl_ThirdPerson` and the subsystem re-resolved a fresh component on the
  gameplay controller — §3.1's "outlives level travel" claim, demonstrated.

### `ir.store.list`

Table and totals render per §12.3, and the relative times follow §12.4 exactly — only non-zero
components, correctly pluralised: `1 minute 51 seconds ago`, `16 minutes 5 seconds ago`.
Manifest-less folders degrade gracefully to their folder name with `Playable: no`.

### Still unverified

Gamepad (§13.4), quota eviction under real load (§16.1 item 8), the overlay's area cap at 21:9 and
4:3, `ir.store.list.ui` row clicking, and `ir.record.test <name>` with a bogus name. These need a
human at the controls or a much larger corpus of takes.

`Recording_1`, `_2` and `_3` are failed-experiment leftovers from this session — video only, no
ghost. They are harmless (and useful as "not playable" test data) but `ir.store.trim` or a manual
delete will clear ~84 MB.

---

## 10. Video orientation — resolved empirically

**Answer: leave `VideoOptions.Orientation` on `Auto`. No flip is needed on this machine.**

Run on 2026-08-20, 1280x720, Media Foundation H.264 sink writer, 362 frames encoded, none dropped.

| Artifact | What it shows | Result |
| --- | --- | --- |
| `Recording_1.firstframe.png` — the exact bytes handed to the encoder | sky top, floor bottom, mannequin upright | **top-down** |
| A frame decoded back out of `Recording_1.mp4` | sky top, floor bottom, mannequin upright mid-jump | **top-down** |

So `UMediaCapture`'s CPU readback delivers top-down here, and Media Foundation honours the positive
`MF_MT_DEFAULT_STRIDE` the encoder sets. The two conventions agree and nothing needs inverting.

This was measured, not reasoned about, exactly as §15.1 demands. It is a **per-machine** result: if
the video ever comes out inverted on different hardware, re-run the same check rather than assuming,
and set `Orientation = BottomUp` only if the PNG is upright and the MP4 is not.

How to reproduce (no ffmpeg needed — the MP4 frame came out through the Windows thumbnail handler,
which decodes the file itself):

```bash
& "F:\Programs\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "F:\Projects\Unreal Engine\Development\UnrealInputRecording\UnrealInputRecording.uproject" -game -IR=0 -windowed -ResX=1280 -ResY=720 -ExecCmds="ir.record.start OrientationTest,ir.video.dumpframe"
```

### What else that run proved

- The store resolved its root to `<ProjectSavedDir>/Recordings` on **F:**, logged a clean boot
  inventory, and claimed `Recording_1` with 150 MB reserved *after* recording had started (§3.8).
- `bAutoCreateReplayComponent` worked: the subsystem auto-created the component on
  `BP_ThirdPersonPlayerController_C_0`.
- Video capture reported `supported: yes`, encoded 362 frames and **dropped none**.
- The double-start guard fired correctly and said so: *"Cannot record: the replay component is
  already busy."* — `-ExecCmds` re-runs on map load, and the guard caught the second start.

### The gap it exposed, and the fix

The `.mp4` survived the exit but the `.ghost` did not: the capture pipeline finalises on teardown
while the take itself was only saved by `ir.record.stop`. That is exactly backwards — §3.9's whole
argument is that a missing video is an inconvenience and a missing ghost is a re-performance.

`UInputRecordingSubsystem::Deinitialize` now saves any take still running, and `ShowToast` bails out
when `IsEngineExitRequested()` so shutdown does not try to build widgets.

✅ **Fixed and verified**, but it took two attempts and the reason is worth keeping:

1. Putting the save in `Deinitialize` did nothing. The world is torn down — and the replay component
   destroyed with it — *before* the game instance shuts down, so by then the samples were already
   gone. The take was silently lost with no error.
2. Hooking `FWorldDelegates::OnWorldBeginTearDown` also did nothing at first, because the ownership
   guard compared against `GetGameInstance()->GetWorld()`, which is **already null** during teardown.
   It now tests `World->GetGameInstance() == GetGameInstance()` instead.

The path that actually fires in practice is the third one: `UInputReplayComponent::EndPlay` calls
`SaveInProgressTake`. `EndPlay` is the only notification guaranteed to arrive while the component
still holds its data, which makes it the real backstop rather than a redundancy. All three routes
are idempotent, so whichever arrives first does the save.

Verified: closing the game mid-take now writes `.ghost`, `.ghost.json` and `Session.json` alongside
the `.mp4`, and logs *"Saving in-progress take (replay component EndPlay)."*

---

## 9. Disk space on C: — watch it

`C:` hit **0 bytes free** on 2026-08-20 and again on 2026-08-17. It is currently around 0.6 GB free,
which was enough to run, but there is no margin.

Unreal's shader working directory is `C:\Users\Rohith\AppData\Local\Temp\UnrealShaderWorkingDir`.
When C: is full, `ShaderCompilerWorker` fails and the game dies before loading a map, writing **no
log at all** — it looks like a silent no-op rather than a disk problem. `Get-PSDrive C` first when a
launch mysteriously produces nothing.

Two Unreal-owned candidates that regenerate safely if you need room: `AppData\Local\UnrealEngine`
(2.6 GB) and `AppData\Local\Temp` (533 MB).

The recordings root is on **F:** (~50 GB free), so the 900 MB quota is unaffected by any of this.

### A separate trap that cost two launches

`Start-Process -ArgumentList` does **not** quote array elements. The project path contains a space
(`F:\Projects\Unreal Engine\...`), so the engine received `F:\Projects\Unreal` as its first argument
and exited with *"Unreal Engine games require a project file as the first parameter"* — before the
log system was up, so it produced no log and looked like the disk problem. Quote the path explicitly:

```powershell
$proj = '"F:\Projects\Unreal Engine\Development\UnrealInputRecording\UnrealInputRecording.uproject"'
```

---

## 7. Still to do

- [x] Editor-target build + editor restart
- [x] Create `WBP_*` widgets, review map, Blueprints, IMC/IA, data assets via MCP
- [x] Reparent `BP_ThirdPersonPlayerController` to `ARecordingPlayerController`
- [x] Write settings into `DefaultGame.ini` via MCP `ConfigSettingsToolset`
- [x] Resolve the video orientation question empirically (§15.1) — **Auto / top-down, see §10**
- [x] Full session round trip: `.ghost` + `.ghost.json` + `Session.json` + `.mp4` (§11)
- [x] Review flow via `-IR=1`: cue matching, direction rejection, wrong-input naming, leave-map (§11)
- [x] `ir.store.list` table and §12.4 relative-time formatting (§11)
- [ ] Quota + LRU eviction under real load (§16.1 item 8)
- [ ] Gamepad pass (§13.4)
- [ ] Overlay area cap at 21:9 and 4:3 (§16.1 item 1)
- [ ] `ir.store.list.ui` row clicking; `ir.record.test` with a bogus name
- [ ] Final acceptance pass (§16.1)

Both halves now work end to end. What remains needs a human at the controls (gamepad, aspect
ratios, clicking rows) or a large corpus of takes (eviction).

### Assets I cannot create — I will need these from you

- **Input icon sprites** for `DA_InputIcons`. I can create the data asset and wire the mapping, but
  not author textures. Exact requirement will be listed when the data asset is created; expect
  one square texture per action (`IA_Jump`, `IA_Move`, `IA_Look`, `IA_MouseLook`), 128×128 PNG,
  imported to `/Game/RecordingFolder/Textures/`. The system runs without them — missing icons fall
  back to text labels and are not an error.
