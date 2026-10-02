# EdgeFlow

<img src="assets/edgeflow-icon.png" alt="EdgeFlow icon" width="128">

EdgeFlow is a lightweight native Windows utility that lets ordinary windows dock to screen edges, auto-hide, and reveal with smooth high-refresh-rate animations.

Drag any normal window to the left, right, top, or bottom edge of a monitor, release, and it snaps into place. When the window loses focus and the mouse moves away, it slides off-screen leaving a few pixels visible. Move the mouse back to that edge and it slides back.

No pre-registration, no hotkey required to start using it.

Inspired by classic edge auto-hide window interactions.

## Features

- Global automatic edge docking — no need to add windows manually
- Left / Right / Top / Bottom docking
- Classic edge auto-hide behavior
- Smooth high-refresh-rate animations
- Per-monitor refresh-rate detection
- Fullscreen guard for games and video
- Chromium / Electron window support (works without `WS_CAPTION`)
- Ignore / blacklist support
- Lightweight native Win32 implementation
- No DLL injection
- No driver
- No shell replacement

## Usage

```text
1. Run EdgeFlow
2. Drag an ordinary window near a screen edge
3. Release to dock
4. Move focus and mouse away to auto-hide
5. Move the mouse back to that edge to reveal
```

Hotkeys:

```text
Ctrl+Alt+H
Toggle ignore for current window

Ctrl+Alt+S
Show all hidden windows
```

The tray icon provides the same actions as a right-click menu, plus a master enable/disable toggle and exit.

## Configuration

Runtime configuration is stored in `edgehide.ini` next to `EdgeFlow.exe`. The file is created and rewritten by the application; a documented starting point is provided as `edgehide.example.ini`.

Key settings:

| Section | Key | Default | Meaning |
|---|---|---|---|
| Global | Enabled | true | Master switch |
| Global | AutoDock | true | Dock on drag release near an edge |
| Global | AutoHide | true | Hide after focus and mouse leave |
| Edge | Left / Right / Top / Bottom | true | Which edges are active |
| Edge | DockDistanceDip | 20 | Distance counted as "near the edge" |
| Edge | MagnetDistanceDip | 40 | Magnetic capture range on release |
| Edge | RevealZonePx | 15 | Hot zone width that reveals a hidden window |
| Edge | VisiblePixels | 5 | Pixels left on screen when hidden |
| Animation | HideDelayMs | 280 | Delay before hiding starts |
| Animation | AnimationTimeMs | 230 | Duration of one hide/reveal animation |
| Behavior | FullscreenGuard | true | Suppress reveal over fullscreen apps |
| Debug | DebugLog | false | Write `debug.log` next to the EXE |

Changes take effect after restarting EdgeFlow.

## Architecture

- **C++ / Win32**, no frameworks, no runtime dependencies
- `SetWinEventHook` for `MOVESIZESTART` / `MOVESIZEEND` / `FOREGROUND` / `OBJECT_DESTROY`
- **QueryPerformanceCounter** as the animation clock; position is derived from real elapsed time, not tick count
- **High-resolution waitable timer** (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`) on a dedicated worker thread drives the animation frame rate; `timeBeginPeriod` is never called
- **SetWindowPos** with `SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE` — never steals focus or changes Z-order
- **Per-monitor refresh-rate detection** via `MonitorFromWindow` + `EnumDisplaySettingsW`
- Idle polling at 80 ms; the high-resolution timer only runs while an animation is in flight

```text
src/
  main.cpp                message window, hotkeys, tray menu, main loop
  manager.h/.cpp          global event hooks, eligibility checks, dock scheduling, fullscreen guard
  managed_window.h/.cpp   per-window state machine, edge geometry cache, animation
  anim_pump.h/.cpp        high-resolution animation scheduler
  config.h/.cpp           settings load/save
  debuglog.h/.cpp         optional diagnostics log
  tray.h/.cpp             tray icon
  EdgeFlow.rc             icon + version resource
CMakeLists.txt
build.bat
```

## Performance

Measured on one machine:

```text
Test monitor: 180Hz
Animation duration: 230ms
Measured frames: ~36–37
Average frame interval: ~6.2–6.4ms
Dropped frames: 0–3
```

For comparison, a fixed 16 ms timer would produce roughly 14 frames over the same 230 ms animation.

> Measurements are from one tested machine and are not guaranteed on all systems.

## Known Limitations

- Forced process termination cannot restore off-screen windows
- Elevated windows may not be controllable from a non-elevated EdgeFlow process
- DPI/display hot-plug behavior is not fully live-recomputed
- Only 180Hz has been directly performance-tested so far
- Windows native Snap behavior may overlap with EdgeFlow edge docking
- Docking two windows side by side against opposite edges will cause both to be managed; use `Ctrl+Alt+H` or `[Blacklist]` to opt out

## Build

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Requires Visual Studio 2022 Build Tools (MSVC v143 + Windows 10/11 SDK) and CMake 3.20+.

Output: `build\Release\EdgeFlow.exe`

## License

MIT — see [LICENSE](LICENSE).
