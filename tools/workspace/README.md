# Local workspace build and launch helpers

These are the scripts used for the tested R8 package. Copy these helper files
into the parent of the source checkout, keeping the checkout named `Source`.
The expected sibling directories are `Client`, `Artifacts`, and `Tools`.
Ubuntu/Docker build scripts currently contain this development machine's
workspace path and `/opt/hazeron/Source`; adjust those paths on another machine.
Do not run Hazeron.ps1 Start on a production server without configuring its environment.

The Filament SDK, portable compiler, generated material binaries, game executables,
server data and local environment credentials are not included in the repository.
See ../../experiments/filament/WORLD.md and LIVE.md for build prerequisites and
validation. Run Launch-Filament-World.cmd from the parent workspace after packaging.
