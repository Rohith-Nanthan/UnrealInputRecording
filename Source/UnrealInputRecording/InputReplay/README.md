# Enhanced Input Recorder / Ghost Playback for UE5

A self-contained input recording and replay system. Records post-modifier Enhanced Input values on a
fixed logical tick, serialises them to a custom binary (or JSON) file under `Saved/InputRecordings/`,
and replays them by injecting values back through the Enhanced Input subsystem so the pawn's existing
bindings fire exactly as they did live.

Written against UE 5.3–5.5 APIs. Nothing here has been through a compiler — treat it as a
production-shaped reference implementation, not a drop-in binary.

| File | Purpose |
|---|---|
| `InputReplayTypes.h/.cpp` | `FRecordedInputFrame`, `FReplaySyncPoint`, `FInputRecordingHeader`, `FInputRecording`, archive operators |
| `InputReplaySerializer.h/.cpp` | Binary (`FMemoryWriter`/`FMemoryReader`) and JSON file I/O |
| `InputReplayComponent.h/.cpp` | The manager: record, save/load, playback, injection, drift detection |
| `ReplayPlayerController.h/.cpp` | Hook points + console commands |

---

## 1. Setup

### 1.1 Build.cs

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "InputCore",
    "EnhancedInput"
});

PrivateDependencyModuleNames.AddRange(new string[] {
    "Json", "JsonUtilities"     // only needed for the JSON debug path
});
```

Then replace `INPUTREPLAY_API` in the headers with your own module's API macro (e.g. `MYGAME_API`),
or leave the classes unexported if everything lives in one module.

### 1.2 Editor steps

1. **Copy the files** into `Source/<YourModule>/InputReplay/` and regenerate project files.
2. **Set the PlayerController class.** In your GameMode (BP or C++), set *Player Controller Class* to
   `ReplayPlayerController`, or reparent your existing BP controller to it. If you already have a
   custom C++ controller, don't use `AReplayPlayerController` — just add the component and forward
   the two hooks:

   ```cpp
   void AMyPlayerController::PreProcessInput(const float DeltaTime, const bool bGamePaused)
   {
       Super::PreProcessInput(DeltaTime, bGamePaused);
       ReplayComponent->TickPreInput(DeltaTime, bGamePaused);
   }

   void AMyPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
   {
       Super::PostProcessInput(DeltaTime, bGamePaused);
       ReplayComponent->TickPostInput(DeltaTime, bGamePaused);
   }
   ```

3. **Assign the mapping contexts.** Select the controller BP → `InputReplayComponent` →
   *Input Replay | Setup* → add every `UInputMappingContext` you want recorded (`IMC_Default`, etc.).
   Every unique `UInputAction` referenced by those contexts is tracked automatically; you don't bind
   anything by hand.

4. **Tag your delta actions.** Add `IA_Look` (or whatever is bound to `Mouse XY 2D-Axis`) to
   **Frame Delta Actions**. Leave gamepad-driven look actions out of it. Read §4.2 before deciding —
   this is the single most important checkbox in the component.

5. **Set `Restore Context Priority`** to whatever priority your pawn adds `IMC_Default` with
   (usually 0). Enhanced Input has no public getter for it, so it can't be recovered automatically.

6. **Pick a time mode:**
   - `Fixed Logical Step` @ 60 Hz — default. Ship this.
   - `Replay Recorded Deltas` — use for automated tests and regression capture.
   - `Free Run` — debug only.

7. **Test.** PIE, then in the console:

   ```
   ReplayRecord
   ... drive around ...
   ReplayStopAndSave Lap01
   ReplayLoadAndPlay Lap01
   ```

   The file appears at `<Project>/Saved/InputRecordings/Lap01.ghost`.
   `ReplayExportJson Lap01` writes a readable `Lap01.ghost.json` next to it.

8. **Optional: bind keys.** Make an `IMC_ReplayDebug` with `IA_ReplayRecord` / `IA_ReplayPlay` on F9
   and F10, apply it at a high priority, and call the same functions from
   `SetupInputComponent()`. Keep it in a separate context so playback suppression (§3.3) doesn't
   remove your own control keys.

---

## 2. Data model

```cpp
USTRUCT() struct FRecordedInputFrame
{
    int32     FrameIndex;   // authoritative logical tick
    float     TimeSeconds;  // derived, for tooling/scrubbing only
    int32     ActionIndex;  // index into Header.ActionPaths
    uint8     TriggerEvent; // ETriggerEvent, debug/tooling
    uint8     ValueType;    // EInputActionValueType
    FVector3f Value;        // raw value; bool packs into X as 0/1
};
```

Three deliberate choices:

**Integer frame index, not a float timestamp.** A `float` timestamp accumulated at 60 Hz has already
lost meaningful precision by the ten-minute mark, and comparing `ElapsedTime >= Frame.TimeSeconds`
means every dispatch decision inherits that error. Integer tick indices cannot drift. `TimeSeconds`
is kept because you asked for a timestamp and because scrubbing UI needs one — it is never used to
drive playback.

**Action registry, not per-frame paths.** You can't serialise a `UInputAction*`. Storing a soft
object path per sample would be enormous. The header holds one `TArray<FString>` of paths and each
sample carries a 4-byte index — which also gives you a single place to validate assets on load.

**Our own value fields, not `FInputActionValue`.** `FInputActionValue`'s members are private and not
`UPROPERTY`-reflected, so neither `FArchive`'s reflection path nor `FJsonObjectConverter` can
round-trip it. Storing `FVector3f` + `ValueType` is lossless — that *is* the internal representation
— and `ToActionValue()` reconstructs it. Float32 instead of the engine's `FVector3d` halves the file
and normalises precision across platforms.

Frames are **delta-compressed**: a sample is written only when the value or trigger event changed.
Playback reconstructs the dense stream by holding the last value. A minute of typical FPS movement at
60 Hz lands around 40–80 KB.

---

## 3. Injection: how the recorded input actually reaches the pawn

### 3.1 The recommended method

`UEnhancedInputLocalPlayerSubsystem::InjectInputForAction()`. This is the engine's own supported
entry point and it feeds the value into the normal evaluation pipeline:

```cpp
Subsystem->InjectInputForAction(Action, FInputActionValue(Action->ValueType, Value), {}, {});
```

Why it beats the alternatives:

| Approach | Verdict |
|---|---|
| **`InjectInputForAction`** | ✅ Triggers regenerate `Started`/`Ongoing`/`Triggered`/`Completed` naturally. Existing bindings fire untouched. Works with no mapping context applied, so replay and live input can't collide. Survives rebinding. |
| Calling the bound delegates on `UEnhancedInputComponent` directly | ❌ The delegate arrays aren't public API, and you'd bypass triggers entirely — no hold, no tap, no chorded actions. |
| `PlayerController->InputKey(...)` at the raw key level | ⚠️ Legitimate, and higher fidelity in one sense (records hardware, not actions). But it couples the recording to the exact mapping context, and mouse deltas recorded as raw axis values are still framerate-sensitive. Good choice if you need to replay *the hardware*; overkill if you need to replay *the intent*. |
| Calling gameplay functions directly (`AddMovementInput`) | ⚠️ Robust and simple, but you lose the trigger layer and any input-driven UI/ability code that listens to actions rather than movement. Reasonable fallback for a pure movement ghost. |
| Mock PlayerController | ❌ Duplicates a huge amount of engine state for no benefit over injection. |

### 3.2 Injected values live for exactly one evaluation

This trips everyone up. `InjectInputForAction` is not "set and hold" — the value is consumed by the
next `ProcessInputStack` and then forgotten. A held key must be **re-injected every single engine
frame** for as long as it's held. That's why `InjectCurrentState()` iterates the entire reconstructed
state every frame rather than dispatching one-shot events at timestamps, and why the recording stores
a *state stream* rather than an *event log*.

The one-frame-of-zero flush (`bNeedsZeroFlush`) matters too: if you simply stop injecting, the
action's triggers never see the release and never fire `Completed`. Injecting a single explicit zero
gives them the transition.

### 3.3 Stopping the human from fighting the ghost

Remove the mapping contexts during playback. Do **not** use `DisableInput()` or
`SetIgnoreMoveInput()` — those suppress the replayed input as well. Removing the contexts means
hardware keys map to nothing, while bindings stay intact and injection (which bypasses key mappings
entirely) keeps working.

### 3.4 Driving a separate ghost pawn

The code above drives the *possessed* pawn, because `InjectInputForAction` goes through the local
player's `UEnhancedPlayerInput`. For a side-by-side ghost racing your live run, use
`UEnhancedInputWorldSubsystem` (UE 5.3+), which can push input into any actor carrying a
`UEnhancedInputComponent` — spawn the ghost pawn, give it an input component with the same bindings,
and target injection at it instead. Same recording format, same playback loop, different sink.

---

## 4. Determinism

### 4.1 The core problem

Input replay reproduces *causes*, not *effects*. Movement integrates as roughly
`Velocity * DeltaTime`, so replaying identical inputs against a different delta-time sequence
produces a different trajectory. At 144 fps the recorded run and the replayed run diverge slowly; at
a 12 ms hitch they diverge immediately. Errors compound: a 2 cm difference becomes a missed ledge
becomes a completely different run.

This is precisely why Unreal's built-in Replay system (Demo Net Driver) records *state* rather than
input. Input replay is the right tool for ghosts, automated regression tests, and input-driven bug
repro — but it needs help.

### 4.2 Rates vs. deltas — the highest-value fix in this whole system

Two kinds of input values look identical in an `FInputActionValue` and behave completely differently
under a framerate change:

- **Rates.** A gamepad stick at 0.8 means "0.8 units per second-ish"; gameplay code multiplies it by
  `DeltaTime`. Sample it and hold it. Framerate-independent by construction.
- **Deltas.** `Mouse XY 2D-Axis` reports *how far the mouse moved during this frame*. It is already
  integrated over the frame. Gameplay code applies it directly, without `DeltaTime`.

If you sample-and-hold a mouse delta, you get an input that scales with framerate: hold a 5-unit
delta for 4 replayed ticks and you've turned 20 units instead of 5. If you time-average it, you lose
total rotation.

The fix is in the format itself. `FrameDeltaActions` marks delta actions, and they are handled with
different arithmetic end to end:

- Recording: `AccumulatedDelta` sums across engine frames, then is divided evenly across the logical
  ticks that frame covered.
- Playback: `PendingDelta` **sums** every consumed tick, injects the total once, then clears.

Sum-in / sum-out means total mouse rotation is preserved bit-for-bit no matter what framerate either
side ran at. Tag your look action correctly and a whole category of "the ghost overshoots the corner"
bugs disappears.

### 4.3 Fixed logical tick (default mode)

Recording quantises onto a fixed accumulator, typically 60 Hz, decoupling the recording from whatever
the display framerate happened to be:

```cpp
TimeAccumulator += DeltaSeconds;
Steps = FloorToInt(TimeAccumulator / FixedStep);
TimeAccumulator -= Steps * FixedStep;
```

Playback runs the same accumulator. Three cases:

- **Playback framerate ≈ logical rate.** One tick per frame. Exact.
- **Playback faster than the logical rate** (`Steps == 0`). No tick is consumed this frame; the held
  state is simply re-injected. Correct — the input genuinely hasn't changed yet.
- **Playback slower** (`Steps > 1`). Multiple ticks collapse into one engine frame. Rates take the
  latest value; deltas sum (§4.2). Tap preservation (`SpanPeakValue` / `bReleasePending`) holds a
  press that started *and* ended inside the collapsed span open for one frame so a single-tick jump
  isn't silently dropped.

You cannot feed more input evaluations than there are engine frames — that's a hard engine
constraint. Aggregation is the correct response to it: it preserves the *integral* of the input,
which is what movement code consumes.

**Keep `LogicalTicksPerSecond` at or below your realistic minimum framerate.** 120 Hz logical on a
game that dips to 40 fps means permanent aggregation.

### 4.4 Recorded deltas (`RecordedDeltas` mode) — the deterministic option

The only way to get near-bitwise reproduction is to make gameplay code observe the *same delta time
sequence* it saw during recording. So record it, then force it:

```cpp
FApp::SetUseFixedTimeStep(true);
FApp::SetFixedDeltaTime(Recording.FrameDeltaSeconds[NextFrame]);
```

One logical tick per engine frame, each frame's exact delta replayed in order. Every
`Velocity * DeltaTime`, every timer, every animation advance sees identical numbers.

Two things to know:

- The engine samples the fixed delta at the *start* of a frame, so setting it during frame N affects
  frame N+1. `TickPreInput` pre-arms the *next* frame's value; getting this off by one is a subtle
  and very annoying bug.
- `SetUseFixedTimeStep(true)` decouples the sim from the wall clock — the engine runs frames as fast
  as it can. Deltas sum to the recorded duration, but pacing is not real-time unless you also cap
  with `t.MaxFPS`. Perfect for headless CI validation; not what you want for a player-facing replay
  viewer.

For CI, pair it with `-FixedTimeStep=0.016667 -NoVSync -Unattended -NullRHI`.

### 4.5 Sync points

Even with all of the above, a general-purpose engine will drift. So don't pretend otherwise — measure
it. Every `SyncPointIntervalFrames` ticks the recorder captures pawn location, rotation, velocity and
control rotation. Playback compares and either:

- broadcasts `OnDesyncDetected(FrameIndex, PositionErrorCm, RotationErrorDeg)` — turn silent drift
  into a test assertion or an on-screen debug number, or
- hard-snaps the pawn back (`bCorrectDriftAtSyncPoints`) — right for a cosmetic ghost, wrong for a
  validation run where the drift *is* the result you care about.

A regression test that fails when positional error at any sync point exceeds 5 cm is worth more than
any amount of hoping.

### 4.6 Randomness

`Header.RandomSeed` is generated at `StartRecording`, applied immediately via `FMath::RandInit` /
`SRandInit`, and reapplied at `StartPlayback`. Seeding only on playback is a common mistake — the
recording session must start from the same seed or gameplay RNG diverges regardless of input
fidelity. For anything you truly need reproducible, prefer an explicit `FRandomStream` seeded from
the recording over the global RNG.

---

## 5. Remaining pitfalls

| Pitfall | Effect | Mitigation |
|---|---|---|
| **Character movement sub-stepping** | `UCharacterMovementComponent::MaxSimulationTimeStep` (default 0.0166) splits large deltas into a *different number* of sub-steps at different framerates | Set `MaxSimulationTimeStep` to match your logical tick and `MaxSimulationIterations` high enough to never clamp |
| **Physics / Chaos** | Not deterministic across substep counts or thread counts | Enable substepping with a fixed `MaxSubstepDeltaTime`; keep replay-critical objects off simulated physics; consider `p.Chaos.Solver.Deterministic` where available |
| **Root motion & anim notifies** | Root motion extraction is delta-dependent; notifies fire on different frames | Keep replay-critical movement out of root motion, or validate via sync points |
| **Frame-rate-dependent input polling** | At 20 fps a 30 ms tap may never be visible to the engine at all | Nothing recovers input the engine never saw. Recording `ETriggerEvent` and preserving `SpanPeakValue` saves taps that *were* seen but would have been quantised away |
| **Real time vs game time** | `FPlatformTime::Seconds()`, `GetRealTimeSeconds()`, `FApp::GetCurrentTime()` all break under fixed timestep | Audit gameplay code for real-time reads; use `GetWorld()->GetTimeSeconds()` |
| **Time dilation / pause** | `DeltaSeconds` is dilated; a pause during recording injects a huge delta | Hooks receive `bGamePaused` and skip; if you use `TimeDilation`, record it and reapply it |
| **Level streaming** | Volumes load at different times, changing when collision appears | Force-load relevant levels before playback, or record on non-streamed test maps |
| **Cross-platform float** | FMA, SIMD width and compiler flags make bitwise parity across platforms unachievable | Treat determinism as per-platform. Validate with sync points, not equality |
| **Asset changes between sessions** | A retuned `UInputAction` modifier changes what a recorded value means | `Header.EngineVersion` + `ActionPaths` are logged; add an asset hash if recordings must survive content churn |
| **Network / listen server** | `ServerMove` corrections and smoothing inject non-determinism | Record and replay in standalone; for networked validation, replay on the server and treat the client as cosmetic |

---

## 6. Easy extensions

- **Compression** — pipe the buffer through `FArchiveSaveCompressedProxy` / `FArchiveLoadCompressedProxy`.
  Recordings are highly repetitive; expect 5–10×.
- **Bit-packing** — `ActionIndex` rarely needs 32 bits and booleans need 1. An `FBitWriter` pass cuts
  the file substantially if size matters.
- **Streaming** — for very long sessions, flush `Frames` to disk in chunks instead of holding the
  whole array. The format already supports it (frames are strictly ascending by index).
- **Scrubbing** — replay from an arbitrary tick by resetting to the nearest earlier sync point,
  restoring the pawn from it, and fast-forwarding the frame cursor.
