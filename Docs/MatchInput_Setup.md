# Match Input — Setup & Test Guide

Interactive, tutorial-style playback for the `UnrealInputRecording` module: the system replays the
*timing* of a recording but makes the live player supply the *inputs*.

---

## 1. What was added

| File | Purpose |
| --- | --- |
| `InputReplay/InputMatchCue.h/.cpp` | `FMatchInputCue`, `FMatchInputCueBuildOptions`, `UInputMatchLibrary` — turns a recording into a cue list and formats inputs for logs/UI. |
| `InputReplay/InputReplayComponent.h/.cpp` | New `MatchInput` mode: interval clock, live-input listening, match/mismatch logic, `UE_LOG` error reporting. |
| `InputReplay/InputRecordingDataAsset.h/.cpp` | `UInputRecordingDataAsset` — editor-inspectable timeline + derived cue list. |
| `InputReplay/InputRecordingAssetTools.h/.cpp` | Generates/updates those assets in the Content Browser from a `.ghost` / `.ghost.json`. |
| `InputRecordingSubsystem.h/.cpp` | Global manager: component discovery, Blueprint API, event relays for the UI. |
| `InputRecordingSettings.h/.cpp` | `Project Settings → Game → Input Recording`. |
| `UI/InputRecordingHudWidget.h/.cpp` | Record / Stop / Match Input HUD. |
| `InputReplay/ReplayPlayerController.h/.cpp` | Added the `ReplayMatchInput` console command. |
| `UnrealInputRecording.Build.cs` | Added `UMG`, `DeveloperSettings`, `Slate`, `SlateCore`, and editor-only `UnrealEd`, `AssetRegistry`. |

`EInputReplayMode` gained a fourth value, `MatchInput`. It is runtime-only state and is not
serialised, so existing `.ghost` files remain valid.

---

## 2. How MatchInput works

```
StartMatchInput()
        │
        ├─ ResolveActionRegistry()      resolve the recording's UInputAction soft paths
        ├─ BuildMatchInputCues()        recording  ──►  ordered list of press onsets
        └─ BuildMatchListenList()       the actions we sample from the live controller
                │
                ▼
   ┌──────────────────────────────────────────────────────────────┐
   │  every frame, from PostProcessInput                          │
   │                                                              │
   │  SampleMatchListenStates()   live values + press edges        │
   │                                                              │
   │  Phase 1  MatchClockSeconds += DeltaSeconds                  │
   │           clock < cue.TimeSeconds  ──►  keep waiting         │
   │           clock ≥ cue.TimeSeconds  ──►  present the cue      │
   │                                                              │
   │  Phase 2  clock FROZEN. For each fresh press this frame:     │
   │             right action + right direction ──► match, advance │
   │             anything else                 ──► UE_LOG(Error)  │
   └──────────────────────────────────────────────────────────────┘
```

Three details worth knowing:

**A cue is a press onset, not a sample.** A recording is a dense delta-compressed stream that
includes analog noise; you cannot ask a player to reproduce `IA_Look = (0.0134, -0.0021)`. Cue
extraction keeps only the frames where an action crossed *from below to above*
`MatchCueOptions.PressThreshold` (default `0.35`).

**The clock, not the game, is what pauses.** `MatchClockSeconds` freezes while waiting. The world is
deliberately *not* paused: while paused, Enhanced Input only evaluates actions flagged
`bTriggerWhenPaused`, so nearly every action would read as zero and no input could ever be matched.

**Intervals are measured cue-to-cue.** On a successful match the clock snaps to the cue's own
timestamp, discarding overshoot. If the recording had presses at 3.0 s and 5.5 s, the second wait is
2.5 s no matter how long the player took to answer the first.

Press edges are tracked *every* frame, not just while blocked. A key the player is already holding
when a cue arrives will not satisfy it — they have to release and press again.

---

## 3. Project Settings (do this once)

`Edit → Project Settings → Game → Input Recording`

| Setting | Third Person template value |
| --- | --- |
| Recorded Contexts | `IMC_Default` (add `IMC_MouseLook` if you use it) |
| Frame Delta Actions | `IA_MouseLook` |
| Match Cue Options → Ignored Actions | `IA_Look`, `IA_MouseLook` *(pre-filled)* |
| Match Cue Options → Press Threshold | `0.35` |
| Default Recording Name | `MatchTutorial` |
| Auto Create Replay Component | ✔ |
| Data Asset Package Path | `/Game/InputRecordings` |

> **Ignored Actions matters.** `IA_Look` is an analog *rate*, not a frame delta, so
> `bIgnoreFrameDeltaActions` does not catch it. Left in, every camera nudge past the threshold
> registers as a wrong input and floods the log.

`Frame Delta Actions` is the single most important recording setting: a *delta* (mouse) is summed and
cleared, a *rate* (stick, WASD) is sampled and held. Getting it wrong is the classic "the ghost turns
further than I did" bug.

---

## 4. Player Controller

Pick one:

**Option A — recommended.** Set your Game Mode's *Player Controller Class* to `ReplayPlayerController`,
or reparent your existing PlayerController Blueprint to it
(*Class Settings → Parent Class → ReplayPlayerController*). This forwards
`PreProcessInput`/`PostProcessInput` to the component, which is required for frame-accurate ghost
playback and gives the most responsive MatchInput.

**Option B — zero setup.** Leave your controller as-is. With *Auto Create Replay Component* enabled,
the subsystem adds a component at runtime and logs a warning that it is running from
`TickComponent`. Recording and MatchInput both work; ghost playback gains a frame of latency.

If you use Option A, select the `InputReplayComponent` on the controller and set **Recorded Contexts**
there too — a hand-configured component always wins over the project defaults.

---

## 5. The HUD widget

1. **Content Browser → right-click → User Interface → Widget Blueprint.**
2. When asked for a parent class, choose **All Classes → `InputRecordingHudWidget`**. Name it
   `WBP_InputRecordingHUD`.
3. Add a `Vertical Box` to the canvas and put these inside. **The names must match exactly** —
   this is what `BindWidgetOptional` looks for:

   | Widget type | Name | Shows |
   | --- | --- | --- |
   | Button (+ Text child "Record") | `RecordButton` | starts recording |
   | Button (+ Text child "Stop") | `StopButton` | stops **and saves** |
   | Button (+ Text child "Match Input") | `MatchInputButton` | starts the interactive session |
   | Text Block | `StatusText` | one-line state |
   | Text Block | `ExpectedInputText` | `Press: IA_Jump [pressed]` |
   | Text Block | `CountdownText` | `Next cue in 2.4s` |
   | Text Block | `MismatchText` | `Wrong input — expected …, got …` |
   | Progress Bar | `ProgressBar` | cue progress |

   Every binding is optional. Three buttons alone is a working HUD; anything else present gets
   driven automatically. A typo means that widget is silently not driven — check the
   `NativeConstruct` log line, which names any missing button.

4. Select the root widget and set the class defaults in the **Input Recording** category:
   *Recording File Name* = `MatchTutorial`, *Recording Display Name* = `Tutorial Take`.

No Blueprint wiring is needed — `NativeConstruct` binds the clicks and subscribes to the subsystem.
For visual polish, override the `NotifyCuePresented`, `NotifyCueMatched`, `NotifyMismatch` and
`NotifyMatchInputFinished` events in the Graph and drive animations from them.

### If you prefer to wire buttons yourself

Any Blueprint can skip the C++ widget entirely:

```
Get Game Instance ─► Get Game Instance Subsystem (Input Recording Subsystem) ─┬─► Start Recording
                                                                              ├─► Stop Recording
                                                                              └─► Start Match Input Mode
```

The subsystem also exposes `Start Match Input Mode From Asset`, `Stop All`, `Get Status Text`,
`Get Expected Input Description`, `Get Time Until Next Cue`, `Get Match Progress`,
`Get Mismatch Count`, and the `On Mode Changed` / `On Match Cue Presented` /
`On Match Input Mismatch` / `On Match Input Matched` / `On Match Input Finished` events.

**Bind to the subsystem's events, never the component's** — the component dies with its
PlayerController; the subsystem outlives level changes and re-attaches itself.

---

## 6. Showing the HUD in a test level

1. Open `Content/ThirdPerson/Lvl_ThirdPerson.umap` (or duplicate it as `Lvl_MatchInputTest`).
2. **Blueprints → Open Level Blueprint.**
3. From `Event BeginPlay`: `Create Widget` (Class = `WBP_InputRecordingHUD`, Owning Player =
   `Get Player Controller 0`) → `Add to Viewport`.
4. Compile and save.

The widget switches the player to **Game and UI** input mode with the cursor shown, so the buttons
are clickable while gameplay input still reaches the game. That is required: UI-only mode would stop
every action from evaluating and the player could never satisfy a cue. Set
*Manage Player Input Mode* to false if you handle input mode yourself.

Because the cursor is visible, mouse-look will feel odd. Keep your tutorial cues on buttons and WASD
(which is why `IA_Look` is in Ignored Actions), or drive the whole flow from the console instead.

---

## 7. Test the flow

### Record

1. Press **Play**.
2. Click **Record**.
3. Perform something deliberate and easy to repeat, e.g. wait 2 s → **W**, wait 2 s → **Space**,
   wait 2 s → **A**.
4. Click **Stop**. The log shows:

   ```
   LogInputReplay: Recording stopped: 480 ticks, 26 samples, 8.00s.
   LogInputReplayIO: Saved recording: .../Saved/InputRecordings/MatchTutorial.ghost (26 frames, 1184 bytes)
   LogInputReplayIO: Saved JSON recording: .../Saved/InputRecordings/MatchTutorial.ghost.json
   ```

   Both formats are written because *Also Export Json On Save* defaults to on: the binary stays
   authoritative for playback (JSON floats do not round-trip bit-exactly), the JSON is for reading.

### Match

5. Click **Match Input**. The log lists the cue list, then:

   ```
   LogInputMatch: MatchInput started on 'Tutorial Take': 3 cue(s) over 8.00s, listening to 3 action(s).
   LogInputMatch: MatchInput cue 1/3 due at 2.00s (+2.00s since the previous cue): waiting for 'IA_Move [Fwd | X=+0.00 Y=+1.00]'.
   ```

6. Press the **wrong** key — `S` instead of `W`:

   ```
   LogInputMatch: Error: MatchInput MISMATCH on cue 1/3 (recorded at 2.00s): expected
                  'IA_Move [Fwd | X=+0.00 Y=+1.00]' but the player pressed 'IA_Move [Back | X=+0.00 Y=-1.00]'
   ```

   Note it names the direction, not just the action — pressing `S` when `W` was recorded is caught
   because axis matching compares direction (dot product ≥ `MatchDirectionTolerance`, default `0.7`,
   about 45°).

7. Press a key bound to a different action entirely:

   ```
   LogInputMatch: Error: MatchInput MISMATCH on cue 1/3 (recorded at 2.00s): expected
                  'IA_Move [Fwd | X=+0.00 Y=+1.00]' but the player pressed 'IA_Jump [pressed]'
   ```

8. Press the right key. The clock resumes and the next interval begins:

   ```
   LogInputMatch: MatchInput cue 1/3 matched: 'IA_Move [Fwd | X=+0.00 Y=+1.00]'.
   LogInputMatch: MatchInput cue 2/3 due at 4.00s (+2.00s since the previous cue): waiting for 'IA_Jump [pressed]'.
   ```

9. Finish the sequence:

   ```
   LogInputMatch: MatchInput completed after 3/3 cue(s).
   ```

Filter the Output Log to `LogInputMatch` to see only this. `LogInputReplay` covers recording and
ghost playback, `LogInputReplayIO` covers file I/O.

### Console-only alternative

No UI needed — type these into the console (`~`) during PIE:

```
ReplayRecord
ReplayStopAndSave MatchTutorial
ReplayMatchInput MatchTutorial
ReplayStop
```

---

## 8. Inspecting a recording as a Data Asset

### Fastest: one console command

Open the **Output Log**, type into the `Cmd` box (works outside PIE too):

```
InputReplay.GenerateDataAsset MatchTutorial
```

This parses the file, creates `/Game/InputRecordings/DA_MatchTutorial`, saves it and opens it.
Related commands:

```
InputReplay.ListRecordings
InputReplay.GenerateDataAsset MatchTutorial json
InputReplay.GenerateDataAsset MatchTutorial json /Game/Tutorials
InputReplay.GenerateAllDataAssets
```

Re-running on an existing asset **updates it in place**, so references from level Blueprints or data
tables survive a re-record.

### By hand

1. **Content Browser → Add → Miscellaneous → Data Asset.**
2. Pick **`InputRecordingDataAsset`**. Name it `DA_MatchTutorial`.
3. Open it, set **Source File Name** = `MatchTutorial` (tick *Source Is Json* for the `.json`).
4. Click **Reimport From Source File**.

### What you get

| Section | Contents |
| --- | --- |
| **01 – Source** | source path, import timestamp, last import error |
| **02 – Summary** | full header (id, level, engine version, tick rate, RNG seed, action registry), duration, sample count, and a per-action rollup with sample/cue counts and whether each soft path still resolves |
| **03 – Timeline** | one readable row per sample: time, tick, action name, clickable asset ref, trigger event, value type, value, and a formatted description |
| **04 – Match Input** | `CueOptions` plus the derived cue list, each with its timestamp and interval |
| **05 – Raw Data** *(advanced)* | verbatim `RawFrames`, `SyncPoints`, `FrameDeltaSeconds` |

Editing anything under **CueOptions** re-derives the cue list immediately — no disk access — so you
can tune *Press Threshold* and watch cues appear and disappear. **Rebuild Match Input Cues** forces
it; **Export To JSON** writes the cached data back out as `<AssetName>_Export.ghost.json` for
diffing two takes.

Cue extraction runs through the same `UInputMatchLibrary` the runtime uses, so the preview cannot
drift from what the player is actually asked to press. To run a session straight from an asset, call
`Start Match Input Mode From Asset` — it also adopts the asset's `CueOptions`.

---

## 9. Tuning

All on the component (`Input Replay | Match Input`), or project-wide in Project Settings.

| Setting | Effect |
| --- | --- |
| `MatchCueOptions.PressThreshold` | Value magnitude counting as a press, and the live dead zone. Raise to ignore light stick pressure; lower to catch gentle analog input. |
| `MatchCueOptions.MinimumCueSpacing` | Collapses two onsets of the same action closer than this into one cue. Guards against the recorder's tap-preservation pass turning one tap into two cues. |
| `MatchCueOptions.bIgnoreFrameDeltaActions` | Skip mouse-delta actions entirely. Leave on. |
| `MatchCueOptions.IgnoredActions` | Actions to exclude, by asset name (`IA_Look`) or full path. |
| `MatchDirectionTolerance` | Axis direction agreement, as a dot product. `1.0` exact, `0.7` ≈ 45°, `0.0` any same-half-plane direction. Ignored for Boolean actions. |
| `bMatchListenToUnrecordedActions` | Also watch actions the recording never contained, so mismatches can name them. Costs nothing. |
| `bLoopMatchInput` | Restart from cue 1 instead of finishing. For a kiosk demo. |
| `bVerboseMatchLogging` | Log cues and successful matches, not just mismatches. Mismatches are always logged as errors. |

---

## 10. Troubleshooting

**"No UInputReplayComponent found."**
Use `ReplayPlayerController`, add the component to your pawn, or enable *Auto Create Replay
Component*.

**"StartRecording failed — the component has no tracked actions."**
*Recorded Contexts* is empty on both the component and the project settings. Add `IMC_Default`.

**"contains no discrete presses above the 0.35 threshold."**
The recording only holds analog look/move noise. Record actual button presses, or lower
*Press Threshold*.

**"could not load recording 'X'."**
Run `InputReplay.ListRecordings`. The subsystem tries binary then JSON before giving up, so this
means neither exists under that name.

**Mismatch errors fire constantly without any input.**
An analog action is drifting past the threshold. Add it to *Ignored Actions* (`IA_Look` is the usual
culprit) or raise *Press Threshold*.

**The cue never becomes due.**
Cue timestamps come from the recording. Check `DurationSeconds` on the Data Asset — a recording made
with a very low `LogicalTicksPerSecond` stretches the timeline.

**Nothing registers when I press the right key.**
Confirm the action is in the recording's registry (**02 – Summary**, `bResolved` must be true). An
action watched only via *Listen To Unrecorded Actions* can never satisfy a cue — the mismatch log
labels those "(not present in this recording)".

**The buttons do nothing / the character does not move.**
Input mode. The widget uses Game-and-UI on purpose; a UI-only mode elsewhere in your project will
starve both the game and MatchInput.
