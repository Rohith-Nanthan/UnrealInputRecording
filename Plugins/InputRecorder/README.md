# Input Recorder

Records Enhanced Input to a `.ghost` file, captures the viewport to an `.mp4` alongside it, and
replays the pair in a standalone review map that quizzes you on the inputs you made.

## Installing it in another project

1. Copy the whole `InputRecorder` folder into `<YourProject>/Plugins/`.
2. Open the project. The plugin is `EnabledByDefault`, so there is no `.uproject` edit to make, and
   it turns on `EnhancedInput`, `MediaIOFramework` and `WmfMedia` (Win64) by itself.

Two things worth knowing before you copy it:

- **The host project must be a C++ project.** This is a code plugin, so it has to compile. Dropping
  it into a Blueprint-only project leaves the editor unable to build it. Adding any C++ class to the
  host project converts it, after which the plugin builds with everything else.
- **Ship the source, not the `Binaries`/`Intermediate` folders.** They are specific to one engine
  version and one project; let the host project build its own.

There is nothing else to configure. Every setting defaults from C++ to content this plugin ships, so
a project that has never heard of the recorder still gets a working one.

## Recording

Play the game and type into the console:

| Command | What it does |
| --- | --- |
| `ir.record.start [name]` | Start a take. The name is optional: `ir.record.start "Jump tutorial"` |
| `ir.record.stop` | Stop and save |
| `ir.record.cancel` | Abandon the take and delete its folder |
| `ir.record.test [session]` | Save any take in progress, then open the review map |
| `ir.store.list` | Print every session as a table, with quota totals |
| `ir.store.list.ui` | The same list in game; rows are clickable and open that session |
| `ir.store.trim` | Evict least-recently-used sessions until back under quota |
| `ir.ui.show` / `ir.ui.hide` | Show or hide the corner overlay without starting a take |

`ir.record.test` with no argument reviews the most recently updated playable session. Otherwise pass
a folder name (`Recording_5`), a bare index (`5`), or a display name.

A take that is still running when the game closes is saved rather than lost.

## Reviewing from a shortcut

| Switch | Effect |
| --- | --- |
| `-IR=1` | Boot straight into the review map with the most recent take |
| `-IR=0` | Boot normally; overrides `-ControlRecap` on the same command line |
| `-ControlRecap[=Recording_5]` | Boot into the review map, optionally pinned to one session |
| `-RecordingRoot=<path>` | Write recordings somewhere other than `Saved/Recordings` |

## Where recordings go

`<YourProject>/Saved/Recordings/Recording_<n>/`, one folder per take:

```
Recording_7.ghost        binary input stream - the take itself
Recording_7.ghost.json   readable copy, for diffing; never read back
Recording_7.mp4          viewport capture
Session.json             metadata; rebuilt from the folder if it goes missing
```

Storage is capped (900 MB by default). When it fills, least-recently-*used* sessions are evicted -
reviewing a take counts as using it, so one you keep coming back to outlives one you recorded later
and never watched. A take in progress and a take under review are never evicted.

## Settings

`Project Settings > Game > Input Recorder`. Nothing is required. Two are worth setting per project:

- **Gameplay Map** - where Cancel returns to after a review. Left empty, the review map returns to
  whatever map the game would have booted into.
- **Frame Delta Actions** - actions whose value is a per-frame delta rather than a rate, such as
  mouse look. These are never turned into cues and can never fail a match.

**Recorded Mapping Contexts** is best left empty. Empty means "record whatever contexts are applied
to the player at the time", which is what lets a take be recorded and reviewed in a project the
plugin knows nothing about. Each take stores the contexts it was recorded under, and the review map
restores exactly those.

Camera and look actions are excluded from cues by default via the wildcard patterns `*Look*` and
`*Camera*` in **Cue Build Options > Ignored Actions**. Entries there accept a full path, a bare asset
name, or a `*`/`?` pattern.

## Extending it

Blueprints should bind to `UInputRecordingSubsystem`, never to `UInputReplayComponent` directly - the
component lives on a controller and dies on every respawn and level travel, while the subsystem
outlives both and re-resolves it. Every widget class in the settings can be pointed at your own
`WBP_` child to restyle the UI without touching C++.
