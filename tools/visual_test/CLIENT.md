# Unattended real-client testing

Run `Test-Client.cmd` from the parent Hazeron workspace, or:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Test-Client.ps1
```

Use `-Build` after changing client code. Add `-Fixtures` to run both the color-only
and depth-export visual suites after the real-client smoke test. Workspace
copies of these helpers are versioned under `tools/workspace`; copy them into
the parent workspace alongside `Source`, `Client`, `Artifacts` and `Tools`.
Building requires the existing Ubuntu/Docker environment. Running a packaged
smoke client requires the Filament R8 runtime and the Client/GD game-data file.

The separate `Client/AuSceneClient-smoke.exe` runs the actual client, creates a
fresh single-player world and DNA avatar, enters it through the normal game
initialization, and drives the production control API. It needs no account,
server, login UI, keyboard input or window focus. It uses the installed GPU and
Qt/OpenGL/Vulkan runtime; this is not a software-only headless renderer.
The explicit `-client-smoke-dir` option requires `-ini`. Settings, caches and
saved worlds are isolated below the unique run directory. A render window may
appear, but it requires no input and is configured not to activate on launch.

The scripted run warms up, moves forward/backward, submits attack controls,
cycles equipment, captures stages, checks nonblank framebuffers, records live
shader coverage and render timings, and exits with a structured result.
The runner bounds total runtime, terminates only its own test process on timeout,
and preserves logs on failure. Attack screenshots are inspection evidence;
control submission alone does not prove combat damage or grip alignment.
Use the procedural pose/anchor fixtures for those additional regression checks.

Results live in `Artifacts/client-smoke/<run>/`:

- `result.json`: assertions, actions, coverage and timing samples.
- `stage-*.png`, `final.png`: framebuffer captures from the game viewport.
- `index.html`: screenshots and links to results and logs.
- `stderr.log`, `stdout.log`: client diagnostics.
- `profile/` and `settings/`: disposable test data, separate from normal play.

`Artifacts/client-smoke/latest.json` points to the latest run. Tests retain their
artifacts for diagnosis. This single-player test does not validate multiplayer
login, TCP/UDP delivery or server-side simulation; it complements those tests.

The new UI adds native interaction checks and ui-*.png captures. Use -ClassicUI
to exercise the original interface in the same build. See ../../docs/UI-OVERHAUL.md.
