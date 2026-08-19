# Recording store, quotas, and the control recap map

Covers the storage layer, the capture orientation harness, input filters, the console surface, and
the standalone review map. For the matching/playback system itself see `MatchInput_Setup.md`.

---

## 1. Where recordings live

```
<RecordingRoot>/
├─ RecordingIndex.json          authoritative next-index counter
├─ Recording_1/
│   ├─ Recording_1.ghost        binary timeline
│   ├─ Recording_1.ghost.json   optional, debug only
│   ├─ Recording_1.mp4          H.264, dominates the size
│   └─ Session.json             metadata, timestamps, byte count
└─ Recording_7/                 indices go sparse after eviction and are never reused
```

`<RecordingRoot>` resolves per platform:

| Platform | Root |
| --- | --- |
| Windows / Mac / Linux, and the editor | `<ProjectSaved>/Recordings` |
| Console | `<ProjectPersistentDownloadDir>/Recordings` |
| Any, overridden | `-RecordingRoot=D:/Takes` |

The console split is not cosmetic. `FPaths::ProjectSavedDir()` is not reliably writable in a packaged
console title, and 900 MB of video does not belong in user save data even where it is — save data is
small, profile-bound, and user-visible. The persistent download area is the platform's answer for
large, app-owned, disposable files.

Everything that touches this directory goes through `URecordingStore`. That is what makes the quota
enforceable: there is exactly one object that knows what is on disk.

### Migration from the old flat layout

The pre-folder layout (`<ProjectSaved>/InputRecordings/<Name>.ghost` + `.mp4`) is migrated into
session folders on the first scan, once. The original bare name is kept as the session's display name
rather than discarded. After that the legacy directory is never read again.

---

## 2. The 900 MB quota

Eviction is least-recently-updated first, keyed on `Session.json`'s `UpdatedUtc` with filesystem
mtime as the fallback. Replaying a session touches it, so a take you keep testing against outlives
one you recorded later and never looked at.

Two sessions are pinned and can never be evicted: the one currently being written, and the one loaded
in the control recap map.

| Moment | What happens |
| --- | --- |
| Boot | Scan, migrate, reconcile the index, log the inventory, trim if already over. |
| Before a take | Evict until `ReserveMegabytesPerTake` fits. If it cannot, recording refuses to start. |
| During a take | Polled once a second. On reaching the quota the take **stops** — see below. |
| After a take | Commit the manifest, then trim. |

Mid-take the store only reports; it does not evict. Deleting another session while one is still being
written trades a finished recording for an unfinished one, and the eviction is not instant while the
encoder keeps running. So the take is stopped instead. What was captured up to that point is still
saved and still valid — the toast says the recording ended early.

> **Budget arithmetic.** At the default 12000 kbit/s and native capture resolution, 900 MB is roughly
> ten minutes of video *across every session combined*. Bitrate is the lever if you need longer takes;
> capture resolution is deliberately fixed at 1× (see §3).

---

## 3. Capture resolution and orientation

### Resolution is always 1×

There is no scale factor. Capture happens at the viewport's native resolution. `bOverrideResolution` /
`ForcedResolution` remain as an explicit, off-by-default escape hatch for pinning a fixed output size.

### Orientation

`FInputRecordingVideoOptions::Orientation` — `Auto`, `Top-down`, or `Bottom-up`.

This is an explicit setting rather than a constant because the two halves of the pipeline disagree
about the convention and neither announces it. `UMediaCapture`'s CPU readback delivers the viewport
top-down; Media Foundation's uncompressed RGB surfaces follow the legacy DIB convention where row 0 is
the *bottom*. Whether that mismatch actually inverts the file depends on which conversion the sink
writer inserts between RGB32 and the encoder's native format, and that varies by machine.

`Auto` is what the pipeline does today: a straight row-for-row copy.

**Do not resolve this by reasoning about the convention.** Use the harness:

```
ir.video.dumpframe
```

The next take writes its first frame to a PNG beside the `.mp4`, from the encoder thread, containing
exactly the bytes the encoder was handed. PNG specifically — BMP's own row order is bottom-up by
convention and would reproduce the ambiguity.

| PNG looks | Meaning | Fix |
| --- | --- | --- |
| Upright | The encoder is fed upright frames; any inversion is the encoder's doing | Set `Orientation` to `Bottom-up` |
| Inverted | The capture side is at fault | Investigate `UMediaCapture`, not the encoder |

The flip itself is free — the frame is already copied row by row to strip readback padding, so
reversing the destination index costs nothing.

---

## 4. Recording filters

`Project Settings → Game → Input Recording → Recording Filter Mode`, or per-component on
`UInputReplayComponent`.

- **Record all inputs** — everything reachable from the recorded contexts, plus `AdditionalActions`.
- **Record only whitelisted actions** — only what `RecordedActionWhitelist` names.

The filter is subtractive on top of the recorded contexts, not a replacement for them: an action still
has to be reachable to be recorded, and the whitelist narrows that set. Turning the filter on can
therefore never *add* an action you had not already configured.

It is applied when the tracked-action list is built, so a filtered-out action costs nothing during a
take — never sampled, never in the header, never a cue. Changing it mid-recording does nothing; it
takes effect on the next `StartRecording`.

A whitelist that excludes every reachable action logs a warning rather than silently recording
nothing.

---

## 5. Console commands

Registered by the module, so they work regardless of which `PlayerController` class the project uses.
The old `Replay*` exec functions on `AReplayPlayerController` still work and forward here.

| Command | Effect |
| --- | --- |
| `ir.record.start [Name]` | Start a take and raise the recording controller |
| `ir.record.stop` | Stop, save, commit, hide the overlay, show the confirmation |
| `ir.record.test` | Stop and save, then open the control recap map |
| `ir.record.cancel` | Abandon the take and delete its session folder |
| `ir.store.list` | Print every session, its size, and the quota headroom |
| `ir.store.trim` | Evict until back under quota |
| `ir.ui.show` / `ir.ui.hide` | Show or hide the overlay without recording |
| `ir.video.dumpframe` | Arm the orientation harness for the next take |

All file and quota operations log to `LogRecordingStore`, and nothing else does — so
`log LogRecordingStore Verbose` gives the complete story of what touched the disk with no input or
rendering noise, and `log LogRecordingStore Off` silences file chatter without hiding recording errors.

---

## 6. Widget 1 — the recording controller

A pop-up, not a HUD element. The subsystem owns it and raises it from `StartRecording`, whichever
route that came in by. `StopRecording` takes it away and replaces it with the save confirmation.

Pinned bottom-right, capped by **screen area** rather than by a width and a height independently —
those two constraints disagree at unusual aspect ratios. `MaxWidthFraction` × `MaxHeightFraction` is
scaled down uniformly if their product would exceed `MaxScreenAreaFraction` (0.2 by default), so the
panel's proportions do not change with the display's.

The confirmation is a real UMG widget (`URecordingToastWidget`), not `AddOnScreenDebugMessage`. Debug
messages are gated behind `GAreScreenMessagesEnabled`, which is off in Shipping — a confirmation that
silently stops appearing in the console build is worse than none.

The **Test** button no longer pushes a full-screen widget over the gameplay map. It saves the take and
travels to `ControlRecapLevel`.

---

## 7. Widget 2 — the control recap map

A whole map, not an overlay: `ControlRecapLevel` with `AControlRecapGameMode` and
`AControlRecapPlayerController`, both Blueprint-subclassable.

Layout: padded full screen; header with `cue n / total` and Cancel; video at 80% of the available
height with the waiting-for prompt as a pill over it; timeline with a progress bar, one marker column
per cue, elapsed/total, and a legend.

Each sync point renders twice on one shared X — the action icon above the bar and a state dot on it —
and both halves are the same `UMatchCueMarkerWidget` on the same canvas anchor. There is no second
rail doing the same arithmetic and rounding it differently, so they cannot drift.

Mismatches flash the prompt pill rather than incrementing a counter in the corner: the feedback lands
where the player is already looking. The running total stays in the log and on the subsystem.

### Input lockout

The pawn is frozen with `SetIgnoreMoveInput` / `SetIgnoreLookInput` and the input mode is
**Game-and-UI**, not UI-only. This is deliberate and the opposite of the obvious choice: MatchInput
reads live Enhanced Input action values to decide whether the player pressed the right thing, and
UI-only stops gameplay input reaching the input stack at all — every cue would hang forever.

### Cancel

Resolution order: the game mode's `TargetOnCancelMap`, then `Project Settings → Gameplay Map`, then
the engine default map. It always goes somewhere.

---

## 8. Controller support

Two layers, routinely confused, and confusing them produces UI that looks controller-ready and is
completely dead on a pad:

- **Focus movement between controls is Slate's job** — `FNavigationConfig` plus `UWidgetNavigation`
  links. It works whether or not Enhanced Input exists.
- **Semantic actions** — accept, back, scrub, toggle record — are Enhanced Input's job, configured on
  the `URecordingUIInputConfig` data asset.

Binding "navigate up" as an Enhanced Input action and calling `SetFocus` by hand is the usual mistake:
it fights Slate's own navigation, breaks whenever a widget is added, and never matches mouse focus
behaviour. `URecordingUIInputConfig::ApplyTo` sets up both halves, including turning on analog-stick
navigation for the full-screen recap (and leaving it off for the overlay, where a stick that moves
focus while the player is still driving is a nuisance).

`UButton` is focusable by default in UE 5.8, so no per-button setup is needed.

---

## 9. Boot flags

```
UnrealInputRecording.exe                            normal boot, gameplay map
UnrealInputRecording.exe -IR=0                      normal boot, said explicitly
UnrealInputRecording.exe -IR=1                      recap map, most recent session
UnrealInputRecording.exe -ControlRecap              recap map, most recent session
UnrealInputRecording.exe -ControlRecap=Recording_5  that session (a bare "5" also works)
UnrealInputRecording.exe -IR=1 -ControlRecap=5      recap map, that session
UnrealInputRecording.exe -RecordingRoot=D:/Takes    read sessions from elsewhere
```

`-IR=<n>` is the short form and `-ControlRecap` the explicit one; either alone boots the recap map.
`-IR` exists so a shortcut or a CI job flips between the two boot paths by changing one character,
which is also why `-IR=0` is spelled out as a value rather than left as "just omit the flag": a
launcher that always appends `-IR=<n>` needs a value meaning *boot the game normally*. An explicit
`-IR=0` also **suppresses** `-ControlRecap`, so that launcher can override a shortcut with the long
flag baked in. A bare `-IR` reads as `-IR=1`.

The flag rewrites `GameDefaultMap` during module startup, before the engine picks a map to load, so
the recap map is simply the map the game boots — nothing loads twice and no frame of the gameplay
level is shown. A fallback travel is registered on `PostLoadMapWithWorld` in case module startup ever
runs after the engine has already resolved its startup map; it does nothing when the override worked.

Session resolution in the recap map, in order:

1. `-IR=1` on its own — forces the most recently updated session and skips everything below it.
2. `-ControlRecap=<folder>`.
3. The game mode's `ForcedSessionFolder`, if the level pins one.
4. The most recently updated *playable* session.

The command line outranks the level's own pin on purpose: the pin is a design-time choice baked into
a map, and someone typing a flag into a terminal is deliberately overriding it for this run. That is
also why `-IR=1` skips the pin outright — the flag means "review what I just recorded", and a level
pinned to an older take would make it do nothing visible.

"Playable" matters at step 4 — a folder whose `.ghost` never finished writing is newer than
everything and would otherwise win every time.

Blueprint gets the same answers through the **Recording Boot Flags** function library (`Get Boot
Mode`, `Is Control Recap Boot`, `Should Force Most Recent Session`, `Get Requested Session Folder`,
`Describe Boot Flags`) — a main menu that shows a "Review last recording" button only in recap
builds, or a HUD that hides itself there, cannot reach a C++ namespace.

### Running uncooked

The monolithic `UnrealInputRecording.exe` in `Binaries/Win64` needs cooked shaders, so before the
project is packaged, drive the same boot path through the editor executable in game mode:

```
UnrealEditor.exe <path>\UnrealInputRecording.uproject -game -IR=1 -windowed
```

---

## 9a. Blueprint-owned UI

Both widget C++ classes build **no widget tree**. Every visual element is a `BindWidgetOptional` hook
filled in by a Blueprint child, so layout and styling are edited in UMG while the logic stays in C++.

`BindWidgetOptional`, not `BindWidget`, on purpose: strict binding fails Blueprint compilation on the
first name mismatch, which blocks rearranging a tree mid-design and reports one problem at a time.
`ValidateBindings()` logs every missing hook in a single message on construct instead, and each is
null-checked. Once a layout is final, changing a line to `BindWidget` makes it a hard requirement.

Widget classes are configured at **Project Settings → Game → Input Recording → UI**, because a
`UGameInstanceSubsystem` has no details panel and these widgets are created from a class rather than
placed by hand.

### WBP_ControlRecap hierarchy

```
[Canvas Panel] RootCanvas
└─ [Vertical Box] ScreenStack              anchors full, offsets 40/32/40/28
   ├─ [Horizontal Box] HeaderRow           Auto
   │  ├─ [Text] SessionLabel *             Fill
   │  └─ [Button] CancelButton *           Auto
   │     └─ [Text] CancelLabel
   ├─ [Size Box] VideoSizeBox *            Auto — height driven by VideoScreenFraction (0.62)
   │  └─ [Overlay] VideoOverlay
   │     ├─ [Image] VideoImage *           Fill/Fill — media texture assigned at runtime
   │     └─ [Text] EmptyStateText *        Center/Center
   ├─ [Text] CueCounterText *              Auto, centred   <- below video, above bar
   ├─ [Size Box] TrackBox                  Auto, height 56
   │  └─ [Overlay] TrackLayer
   │     ├─ [Progress Bar] ProgressBar *   Fill/Bottom
   │     └─ [Canvas Panel] MarkerCanvas *  Fill/Fill — cue markers spawn here
   ├─ [Horizontal Box] TimeRow             Auto
   │  ├─ [Text] ElapsedText *              Fill
   │  └─ [Text] TotalText *                Auto
   ├─ [Vertical Box] PromptPanel *         Auto, centred
   │  ├─ [Horizontal Box] ExpectedRow
   │  │  ├─ [Image] ExpectedInputIcon *    52x52
   │  │  └─ [Text] ExpectedInputText *     size 42 Bold   <- large
   │  └─ [Text] WrongInputText *           size 18, red   <- smaller
   └─ [Horizontal Box] LegendPanel *       Auto, centred
      ├─ [Image] LegendPassedDot   + [Text] LegendPassedText      8px dot, size 10 text
      ├─ [Image] LegendNextDot     + [Text] LegendNextText
      └─ [Image] LegendUpcomingDot + [Text] LegendUpcomingText
```

### WBP_RecordingController hierarchy

```
[Canvas Panel] RootCanvas
└─ [Size Box] ClampBox *                   anchored bottom-right, AutoSize
   └─ [Border] RootBorder *                padding 16
      └─ [Vertical Box] PanelStack
         ├─ [Horizontal Box] HeaderRow
         │  ├─ [Text] TitleText *          Fill
         │  └─ [Text] StatusPillText *     Auto
         ├─ [Horizontal Box] ControlsRow
         │  ├─ [Button] RecordToggleButton *  Fill
         │  │  └─ [Text] RecordButtonLabel *
         │  └─ [Button] TestButton *          Auto
         │     └─ [Text] TestButtonLabel *
         ├─ [Horizontal Box] CurrentInputRow
         │  ├─ [Image] CurrentInputIcon *     34x34
         │  └─ [Vertical Box] CurrentInputTextCol
         │     ├─ [Text] CurrentInputName *
         │     └─ [Text] CurrentInputSub *
         ├─ [Horizontal Box] HistoryHeaderRow
         │  ├─ [Text] HistoryLabel
         │  └─ [Text] HistoryCountBadge *
         └─ [Scroll Box] HistoryScroll *      Fill
```

`*` = bound to C++. Rename one and it silently stops updating — the construct log names it.

## 9b. Icon sprites

`Project Settings → Game → Input Recording → UI → Input Action Icon Mapping` points at
`DA_InputIcons`. Every recording UI resolves icons as: the widget's own `IconMapping` if set,
otherwise this setting.

The setting exists because a widget's `IconMapping` is a per-Blueprint field, and any widget created
straight from its C++ class — which is what the control recap map does — has nobody to fill it in.
The failure was silent: every icon lookup was skipped and markers drew empty brushes, which looks
exactly like a missing sprite rather than an unassigned asset.

## 10. Assets

Already created and wired:

| Asset | Purpose |
| --- | --- |
| `/Game/Recording/ControlRecapLevel` | The review map. World Settings → GameMode Override is set. |
| `/Game/Recording/BP_ControlRecapGameMode` | `PlayerControllerClass` and `TargetOnCancelMap` set. |
| `/Game/Recording/BP_ControlRecapPlayerController` | Styling hook; `RecapWidgetClass` falls back to the C++ class when null. |
| `/Game/Recording/DA_RecordingUIInput` | Points at the context and all four actions. |
| `/Game/Input/IMC_RecordingUI` | Nine key mappings, gamepad and keyboard. |
| `/Game/Input/Actions/IA_UI_{Accept,Back,ToggleRecord,Test}` | The UI actions. |

Default key bindings:

| Action | Gamepad | Keyboard |
| --- | --- | --- |
| Accept | Face button bottom (A / ✕) | Enter, Space |
| Back | Face button right (B / ○) | Escape |
| Toggle record | Special right (Start / Options) | F9 |
| Test | Face button top (Y / △) | F10 |

`ControlRecapLevel` is a copy of the engine's Basic template. Its contents are never visible — the
recap widget's backing colour is opaque — but it keeps the level lit and valid, and gives the pawn a
`PlayerStart` to spawn at.

### Remaining

- Record a take and run `ir.video.dumpframe` first, then check the PNG for orientation (§3).
- Optionally make Blueprint subclasses of `URecordingControllerWidget` / `UControlRecapWidget` for
  styling and set them on the subsystem and the player controller.
