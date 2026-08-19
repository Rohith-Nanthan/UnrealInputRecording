# Synchronised Video — Setup & Test Guide

MP4 screen capture paired with every `.ghost`, played back with its playhead owned by the MatchInput
clock, plus a timeline widget that lays cue icons out along a progress bar.

Companion to [MatchInput_Setup.md](MatchInput_Setup.md), which covers the recording system itself.

> **UI update:** the Blueprint-authored HUD has been replaced by C++-driven widgets. Sections 5-8
> below (the `WBP_VideoMatchPlayer` / `BindWidget` workflow) are superseded by
> [UI_CppWidgets.md](UI_CppWidgets.md). Sections 1-4 (plugins, project settings, the icon mapping
> asset) and 9-11 still apply. The video is no longer upside down - see UI_CppWidgets.md section 6.

---

## 1. What was added

| File | Purpose |
| --- | --- |
| `Video/InputRecordingVideoTypes.h/.cpp` | `FInputRecordingVideoOptions`, `EInputRecordingVideoState`, path helpers. `<Name>.mp4` sits next to `<Name>.ghost`. |
| `Video/VideoEncoderBackend.h/.cpp` | `IInputRecordingVideoEncoder` + the Windows Media Foundation sink-writer backend (H.264 encode **and** MP4 mux) on its own worker thread. |
| `Video/InputRecordingMediaOutput.h/.cpp` | `UInputRecordingMediaOutput` / `UInputRecordingMediaCapture` — the MediaIO pair that reads back the viewport and feeds the encoder. |
| `Video/InputRecordingScreenRecorder.h/.cpp` | `UInputRecordingScreenRecorder` — one take's capture session, owned by the subsystem. |
| `Video/InputRecordingVideoPlayer.h/.cpp` | `UInputRecordingVideoPlayer` — `UMediaPlayer` + `UMediaTexture`, playhead bound to `MatchClockSeconds`. |
| `UI/InputActionIconMappingDataAsset.h/.cpp` | `UInputActionIconMappingDataAsset` — `UInputAction` → `FSlateBrush`, keyed by soft path *and* short name. |
| `UI/*` (widgets) | Now C++-driven — see [UI_CppWidgets.md](UI_CppWidgets.md). |
| `InputRecordingSubsystem.h/.cpp` | Owns the recorder and the player; starts/stops both alongside the `.ghost`; relays pause/resume. |
| `InputReplayComponent.h` | Added `GetMatchClockSeconds()`. |
| `InputRecordingSettings.h` | New **Video** section in Project Settings. |
| `UnrealInputRecording.Build.cs` | Added `MediaAssets`, `MediaIOCore`, `RenderCore`, `RHI`, and the Windows MF system libs. |
| `UnrealInputRecording.uproject` | Enabled the **Media IO Framework** and **WmfMedia** plugins. |

The `.ghost` format is unchanged. Recordings made before this feature still load; they simply have no
paired `.mp4`, and MatchInput runs without a video panel.

---

## 2. How it fits together

```
RECORD
  Subsystem::StartRecording()
        ├─ Component::StartRecording()            .ghost buffer starts filling
        └─ ScreenRecorder::StartCapture()
                 UMediaCapture (viewport, BGRA readback, render thread)
                        │  straight copy, ~8 MB at 1080p
                        ▼
                 bounded queue (drops rather than stalling the render thread)
                        │
                        ▼
                 encoder thread ── IMFSinkWriter ── H.264 + mp4 mux ──► <Name>.mp4

  Subsystem::StopRecording()
        ├─ ScreenRecorder::StopCapture()          blocks until the moov box is written
        ├─ RenameCapturedVideo(FinalName)         keeps the pair intact
        └─ Component::SaveRecordingToFile()       <Name>.ghost


MATCH INPUT
  Subsystem::StartMatchInputMode("MatchTutorial")
        ├─ Component::LoadRecordingFromFile()     MatchTutorial.ghost
        └─ VideoPlayer::OpenRecordingVideo()      MatchTutorial.mp4, paused on frame 0

  ┌─ Phase 1: interval counting down ───────────────────────────────┐
  │  MatchClockSeconds advances       video PLAYS                    │
  └──────────────────────────────────────────────────────────────────┘
        │ cue becomes due
        ▼
  ┌─ Phase 2: blocked on the player ────────────────────────────────┐
  │  MatchClockSeconds FROZEN         video PAUSED                   │
  │    wrong input  ──► still paused (nothing resumes it)            │
  │    right input  ──► OnMatchInputMatched ──► video RESUMES        │
  └──────────────────────────────────────────────────────────────────┘
```

**Two mechanisms keep the video in step, and they do different jobs.**

*Events* give the exact frame. `OnMatchCuePresented` → `PauseVideo()`, `OnMatchInputMatched` →
`ResumeVideo()`. There is deliberately **no** handler on `OnMatchInputMismatch`: a wrong press leaves
the video exactly where it is, which is what makes "the tutorial waits for you" work.

*Drift correction* keeps it there. A media player's clock is its own, and it will not stay in step
with a clock that keeps stopping and starting. `SyncToMatchClock` seeks once the error passes
`ResyncThresholdSeconds` (default 0.25 s). It runs from an `FTSTicker` on the subsystem, so sync is
correct whether or not the UI widget is on screen.

---

## 3. Why Media Foundation and not an engine plugin

UE 5.8 ships everything needed to *acquire* frames (`UMediaCapture`) and to *play an MP4 back*
(WmfMedia / Electra), but **nothing that encodes H.264 and muxes it to a file**:

* `MP4Utilities`' `IMP4RawMuxer` is a raw muxer — it wants an already-encoded, already-framed
  elementary stream plus a hand-built `avcC` configuration record.
* `AVCodecs` is still flagged Experimental and its API moves between engine versions.

`IMFSinkWriter` does encode *and* mux in one object, picks up a hardware encoder (NVENC / QuickSync /
AMF) when the machine has one, and needs no plugin — just `mfplat.lib`, `mfreadwrite.lib`,
`mfuuid.lib`. It is the same API the engine's own `GameplayMediaEncoder` used.

Everything above `IInputRecordingVideoEncoder` is platform-agnostic. On a platform with no backend,
`Create()` returns null, capture is skipped with a warning, and the `.ghost` is unaffected.

---

## 4. One-time project setup

### 4.1 Plugins

Already enabled in `UnrealInputRecording.uproject`:

* **Media IO Framework** — supplies `MediaIOCore` (`UMediaOutput` / `UMediaCapture`). Not enabled by
  default in a fresh project; without it the module will not link.
* **WmfMedia** — plays the resulting MP4 back. This one *is* enabled by default; it is listed
  explicitly so the dependency is visible. Electra Player works too if you prefer it.

Restart the editor after the first build so the new plugin modules load.

### 4.2 Project Settings → Game → Input Recording → Video

| Setting | Default | Notes |
| --- | --- | --- |
| Capture Video With Recording | ✅ | Turn off to go back to `.ghost`-only recording. |
| Play Video During Match Input | ✅ | |
| Capture Video Including UI | ❌ | On = capture the composited back buffer, so the HUD appears in the video. Usually what you want for a tutorial the player watches back. |
| Target Frame Rate | 30 | Advertised rate. Samples carry real timestamps, so the file is effectively VFR. |
| Bit Rate Kbps | 12000 | ~clean 1080p. Drop to 6000 for smaller files. |
| Resolution Scale | 1.0 | The cheapest lever if capture costs you frames. 0.5 quarters the pixel count. |
| Max Queued Frames | 6 | Each slot is `Width × Height × 4` bytes — about 8 MB at 1080p. |

### 4.3 The one thing that will bite you

`UMediaCapture::CaptureActiveSceneViewport` can only resolve a viewport when the game owns its own
window:

> **Editor Preferences → Level Editor → Play → Play In → New Editor Window**, or launch Standalone.

Playing docked in the level viewport, capture is skipped and you get this in the log:

```
LogInputRecordingVideo: Warning: Could not resolve an active scene viewport. [...]
Continuing without video capture; the .ghost recording is unaffected.
```

The input recording is completely unaffected — you just get no `.mp4` for that take.

---

## 5. Create the icon mapping asset

1. **Content Browser → Add → Miscellaneous → Data Asset → Input Action Icon Mapping.**
   Name it `DA_InputIcons`.
2. Add one **Entry** per action you want a prompt for:
   * **Action** — pick `IA_Jump`, `IA_Move`, etc. **Action Name** fills itself in when you save.
   * **Icon** — a `FSlateBrush`. Drop a texture into **Image** and set **Image Size** (e.g. 48 × 48).
     If you set an image size here it wins over the widget's `Cue Marker Size`.
   * **Display Name** — the player-facing label ("Jump"). Falls back to the action name if empty.
3. Set **Default Icon** so unmapped actions show a placeholder rather than a hole.

Lookup tries the **soft object path first, the short name second**. That second key is what keeps old
recordings working after you move or rename an Input Action asset — the path in the `.ghost` goes
stale, the name does not.

---

## 6. Build `WBP_VideoMatchPlayer`

**Create it:** Content Browser → **User Interface → Widget Blueprint → Video Match Player Widget**.

### 6.1 Hierarchy

Add widgets with **exactly** these names. Every binding is optional — start with three and add the
rest as you need them.

```
[Vertical Box]
├── VideoImage            (Image)            the video surface
├── [Overlay]                                ◄── the timeline. See 6.2.
│   ├── TimelineBar       (Progress Bar)     Horizontal + Vertical Alignment = Fill
│   └── TimelineCanvas    (Canvas Panel)     Horizontal + Vertical Alignment = Fill
├── [Horizontal Box]
│   ├── ExpectedInputIcon (Image)            icon of the cue being waited on
│   └── ExpectedInputLabel(Text Block)       "Jump"
├── TimeLabel             (Text Block)       "0:04 / 0:31"
└── WaitingIndicator      (any widget)       shown only while blocked on the player
```

A mistyped name is silent — `BindWidgetOptional` just leaves the pointer null. `NativeConstruct` logs
a warning specifically for a missing `TimelineCanvas`, since that is the one that makes the whole
timeline do nothing.

### 6.2 Why the Overlay matters

Markers are positioned with **normalised anchors**, not pixels:

```cpp
const float Fraction = Cue.TimeSeconds / TotalDurationSeconds;
CanvasSlot->SetAnchors(FAnchors(Fraction, 0.5f, Fraction, 0.5f));
CanvasSlot->SetAlignment(FVector2D(0.5, 0.5));   // centre on the anchor
CanvasSlot->SetSize(BrushSize.IsNearlyZero() ? CueMarkerSize : BrushSize);
CanvasSlot->SetPosition(CueMarkerOffset);
```

A marker at 0.42 stays at 42% of the bar's width at every resolution and through every resize, with no
pixel arithmetic and no re-layout on window change — **but only if `TimelineCanvas` spans exactly the
same rectangle as `TimelineBar`.** Putting both in an Overlay set to Fill is what guarantees that. Any
other arrangement and the icons will drift away from the fill they are supposed to annotate.

### 6.3 Class Defaults

| Property | Set it to |
| --- | --- |
| **Icon Mapping** | `DA_InputIcons` |
| **Cue Marker Size** | `48, 48` |
| **Cue Marker Offset** | `0, -32` to float the icons above the bar; `0, 0` to sit them on it |
| **Cue Marker Class** | leave **empty** to start — see 6.4 |
| **Preview Recording Asset** | optional `UInputRecordingDataAsset`, so the track is populated before any session starts |
| **Drive Video Playhead** | ✅ |

### 6.4 Optional: a custom marker

Leave **Cue Marker Class** empty and the widget spawns a plain `UImage` per cue — the timeline works
with nothing but C++ and the icon mapping, including the three-state tint.

For animation or a key-cap frame:

1. **Widget Blueprint → Match Cue Marker Widget**, name it `WBP_CueMarker`.
2. Add an **Image named `IconImage`**. The base class fills its brush in.
3. Override **Set Marker State** to animate (Pending / Active / Completed), or **On Marker
   Initialised** for one-time setup. `Cue` and `CueIndex` are already populated when either fires.
4. Assign it to **Cue Marker Class**.

---

## 7. The video surface

You do **not** need to create a Media Player or Media Texture asset. `UInputRecordingVideoPlayer`
constructs both as transient objects at runtime; `GetMediaTexture()` hands the widget the live one.

### 7.1 Direct (default)

Leave **Use Material For Video** off. `BindVideoSurface()` calls
`VideoImage->SetBrushResourceObject(MediaTexture)`.

> `SetBrushResourceObject`, not `SetBrushFromTexture` — a `UMediaTexture` is a `UTexture` but not a
> `UTexture2D`, so the typed setter refuses it.

Good enough for a debug HUD or a small inset player. The image stretches to its slot, so set the
slot's aspect ratio yourself if that matters.

### 7.2 Material (letterboxing, grading, rounded corners)

1. **Create a Media Texture asset** — `MT_VideoPreview`. It is used only as the *parameter default* so
   the material compiles with the right sampler; the runtime texture is swapped in over it.
2. **Create a Material** — `M_VideoSurface`:
   * **Material Domain** = `User Interface`
   * **Blend Mode** = `Opaque`
   * Add a **Texture Sample Parameter 2D**, name it **`MediaTexture`**, and set its default texture to
     `MT_VideoPreview`. Leave **Sampler Type** at whatever it auto-selects for a media texture.
   * Wire **RGB → Final Color**.
3. On the widget's Class Defaults:
   * **Use Material For Video** = ✅
   * **Video Material** = `M_VideoSurface`
   * **Video Material Texture Parameter** = `MediaTexture` (must match the parameter name exactly)

The widget builds a `UMaterialInstanceDynamic` and pushes the live media texture into that parameter.

---

## 8. Embed it in the HUD

Open `WBP_InputRecordingHUD`, drag `WBP_VideoMatchPlayer` in as a child, and you are done — the panel
binds to the subsystem itself in `NativeConstruct` and needs no wiring from the parent.

The two widgets do not conflict: the HUD *drives* the subsystem (Record / Stop / Match Input buttons),
the video panel only *reflects* it.

For Blueprint-side polish, the panel exposes:

| Event | Fires when |
| --- | --- |
| `On Cue Presented` | a cue is due and the system is now blocked |
| `On Cue Matched` | the player got it right |
| `On Cue Mismatched` | the player pressed the wrong thing |
| `On Session Finished` | the sequence ended |
| `On Video Ready` | the `.mp4` finished opening (or failed) |
| `On Cue Marker Created` | per marker, at timeline build time |

---

## 9. Test it

1. Set **Play In → New Editor Window**.
2. Play, open the HUD, press **Record**. Perform a short sequence — a few jumps and direction changes.
3. Press **Stop**. Check the log:

```
LogInputRecordingVideo: Screen capture started: 1920x1080 -> '.../Saved/InputRecordings/MatchTutorial.mp4'
LogInputRecordingVideo: Encoding 1920x1080 H.264 @ 30 fps, 12000 kbit/s -> '...'
LogInputRecordingVideo: Encoder finished: 412 submitted, 412 encoded, 0 dropped -> '...'
LogInputRecordingVideo: Screen capture finished: '...' (18.4 MB).
LogInputReplay: Recording saved as 'MatchTutorial' in .../Saved/InputRecordings
```

4. Confirm `MatchTutorial.ghost` and `MatchTutorial.mp4` sit side by side in
   `<Project>/Saved/InputRecordings/`. The `.mp4` should open in any player.
5. Press **Match Input**. The video plays through each interval, freezes on each cue, ignores wrong
   presses, and resumes the instant you get one right.

---

## 10. Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `Could not resolve an active scene viewport` | Playing docked in the level viewport. Use **New Editor Window** or Standalone. |
| No `.mp4`, no warning either | **Capture Video With Recording** is off in Project Settings. |
| `SetInputMediaType failed` | No H.264 encoder accepted the input. On Windows **N/KN** editions, install the Media Feature Pack. |
| `Dropped N of M captured frames` | The encoder cannot keep up. Lower **Resolution Scale** or **Bit Rate Kbps**, or raise **Max Queued Frames**. |
| Video plays but is upside down | Only possible if the flip in `SubmitFrame_AnyThread` was changed. MF's `MFVideoFormat_RGB32` with a positive `MF_MT_DEFAULT_STRIDE` means bottom-up; the encoder writes bottom-up to match. |
| `.mp4` exists but will not open | The take was abandoned without finalising, so there is no `moov` box. `Deinitialize` and `BeginDestroy` both stop capture to prevent this — check the log for an encoder error. |
| Video consistently lags or leads the cue by a fraction of a second | Capture timestamps come from the render thread, which trails the game thread by a frame or two. Nudge `VideoTimeOffsetSeconds` on the video player. |
| Video visibly stutters as it is yanked around | `ResyncThresholdSeconds` is too small. Raise it, or set `bResyncToMatchClock = false` and rely on the events alone. |
| Icons all bunched at the left of the bar | `TotalDurationSeconds` is zero — the recording has no duration, or `BuildTimeline` was called with an explicit 0. It falls back to the last cue's timestamp; check the recording actually loaded. |
| Icons drift away from the progress fill | `TimelineCanvas` does not span the same rectangle as `TimelineBar`. See 6.2. |

---

## 11. Known limitations

* **Video is captured, audio is not.** The sink writer is configured with a single video stream. Adding
  an AAC stream would mean tapping the audio device — a separate piece of work.
* **Windows only.** `IInputRecordingVideoEncoder::Create()` returns null elsewhere and recording
  continues without video. Add a backend there and nothing above it changes.
* **`.ghost` and `.mp4` clocks start a frame or two apart.** The recording clock starts on the game
  thread; capture timestamps are taken on the render thread. `VideoTimeOffsetSeconds` exists to
  correct it, and the offset is well below `ResyncThresholdSeconds` so it never triggers a seek.
* **The pair is by file name only.** Nothing is embedded in the `.ghost` header, which is what keeps
  old recordings loading — but it also means renaming one file by hand breaks the pair.
