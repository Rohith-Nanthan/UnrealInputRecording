# C++-Driven UI — Recording Controller & Match Video Player

Supersedes the Blueprint-authored HUD. The UI is now built in C++ (`RebuildWidget`) so the structure
is robust and the same in every project; Blueprint subclasses exist only to restyle. Companion to
[VideoMatch_Setup.md](VideoMatch_Setup.md) (sections 5–8 of that file are replaced by this one).

---

## 1. What changed

| Removed | Replaced by |
| --- | --- |
| `UI/InputRecordingHudWidget.*` | `UI/RecordingControllerWidget.*` |
| `UI/VideoMatchPlayerWidget.*` | `UI/MatchVideoPlayerWidget.*` + `UI/VideoSurfaceWidget.*` |

New leaves: `UI/SyncPointRowWidget.*` (one history row) and a rewritten
`UI/MatchCueMarkerWidget.*` (now C++-composed, carrying both the above-bar icon and the on-bar dot).

Every widget builds its own tree in `RebuildWidget()` with `WidgetTree->ConstructWidget<T>()` — **no
`BindWidget`, no designer canvas required.** A mistyped widget name can no longer silently break the UI.

---

## 2. The widgets

### `URecordingControllerWidget`
The always-on panel.

```
RootBorder
└ VerticalBox
  ├ HeaderRow      title + status pill (rec MM:SS / idle)
  ├ ControlsRow    RecordToggleButton  +  TestButton
  ├ CurrentInputCard   icon + action name + value   ← Debug area 1
  └ HistoryCard
    ├ header       "Sync point history"  +  "N total" badge
    └ HistoryScroll (UScrollBox)                     ← Debug area 2
      └ USyncPointRowWidget ×N   auto-scrolls, last 5 stay bright
```

- **Record toggle** — Start/Stop; Stop saves under `RecordingFileName`. History clears on each start.
- **Test** — enabled only when idle. Starts MatchInput and pushes the full-screen player.
- **Current input** — polls `Subsystem->GetLiveInputSnapshot()` each tick.
- **Sync history** — one row per `Subsystem->OnInputSyncPointRecorded`; auto-scrolls to newest; the
  most recent `HistoryHighlightCount` (default 5) rows stay highlighted while older ones fade.

### `UMatchVideoPlayerWidget`
Full-screen, opened by Test.

```
ScreenBorder (padding)
└ Overlay
  ├ VerticalBox
  │ ├ VideoContainer → UVideoSurfaceWidget   (~80% — fills above the timeline)
  │ └ TimelineSizeBox → Overlay
  │     ├ ProgressBar (bottom strip)
  │     └ MarkerCanvas → UMatchCueMarkerWidget ×N
  ├ PromptBorder    "waiting for [icon] IA_Jump"  (only while blocked)
  ├ CueCountText    "cue 3 / 12"  (top-left)
  └ CancelButton    (top-right, always available)
```

Each marker is one full-height column anchored at `cue.TimeSeconds / duration`: the **icon rides at the
top**, the **dot sits on the bar** at the bottom — same widget, same anchor, so the two layers can never
drift apart. Dot state tracks the cue (passed / next / upcoming).

**Input lockout** (on open, balanced restore on close): `SetIgnoreMoveInput(true)` +
`SetIgnoreLookInput(true)` freeze the pawn, while the input mode is Game-and-UI with the cursor shown.
The Enhanced Input contexts stay live on purpose — matching reads action values straight from the EI
subsystem, so presses still register while the pawn can't wander the level.

**Close** — Cancel and natural completion both route through `OnMatchInputFinished`, so there is one
teardown path: unbind, restore input, broadcast `OnClosed`, remove from viewport. The controller shows
itself again on `OnClosed`.

### `UVideoSurfaceWidget`
Just the video — a `UImage` bound to the subsystem's `UMediaTexture`, direct or through a UI material
(`bUseMaterial` + `VideoMaterial` + `MaterialTextureParameter`). Reusable anywhere.

---

## 3. Getting it on screen

Minimal, from your HUD/PlayerController Blueprint or C++:

```
Create Widget (class = Recording Controller Widget)  →  Add to Viewport
```

Assign **Icon Mapping** (`DA_InputIcons`, see [VideoMatch_Setup.md](VideoMatch_Setup.md) §5) on the
controller so the current-input read-out, the history rows, and the timeline all draw icons. That is the
only required field. The controller opens the match player itself when Test is pressed.

---

## 4. Styling in Blueprint

Reparent a Widget Blueprint to any of these classes and edit the **Style** category — you do **not**
rebuild the tree:

| Widget | Style knobs |
| --- | --- |
| `RecordingControllerWidget` | `PanelColor`, `CardColor`, `AccentColor`, `DangerColor`, `MutedColor`, `PanelTitle`, `HistoryHighlightCount` |
| `MatchVideoPlayerWidget` | `BorderPadding`, `ScreenColor`, `ProgressFillColor`, `PromptBackground`, `CancelButtonColor`, `TimelineHeight`, `MarkerColumnWidth` |
| `MatchCueMarkerWidget` | icon tints + dot colours per state, `IconSize`, `DotSize`, `ActiveDotScale` |
| `SyncPointRowWidget` | `HighlightBackground`, `FadedText`, `IconSize`, `FontSize`, `RowPadding` |
| `VideoSurfaceWidget` | `bUseMaterial`, `VideoMaterial`, `MaterialTextureParameter`, `Tint` |

To swap a whole sub-widget for your own subclass, set `MatchPlayerClass`, `CueMarkerClass`,
`SyncRowClass`, or `VideoSurfaceClass`. Each built panel is also exposed `BlueprintReadOnly`, and every
widget fires an `On…Constructed` event for extra styling after the tree is up.

---

## 5. Backend feeds added for the debug areas

The recording side previously exposed nothing live. Two additive hooks now feed the controller (both
also relayed on the subsystem):

- `UInputReplayComponent::OnInputSyncPointRecorded(FName, float TimeSeconds, FVector Value)` — fired
  from `SampleRecording` when a non-delta action crosses `LiveOnsetThreshold` (0.35, mirroring the cue
  extractor so the live list matches the cues the take will yield).
- `UInputReplayComponent::GetLiveInputSnapshot(FString& Name, FVector& Value)` — the action pressed
  hardest this frame.

Neither changes the `.ghost` format or any existing behaviour.

---

## 6. Video orientation fix

`VideoEncoderBackend.cpp` used to flip rows by hand **and** declare a positive Media Foundation stride —
a double inversion that shipped the `.mp4` upside down. It now copies straight (correct for the top-down
capture the encoder MFT expects). `FInputRecordingVideoOptions::bFlipVerticallyOnCapture` (Project
Settings → Video, advanced) is an escape hatch for an encoder that reads bottom-up.
