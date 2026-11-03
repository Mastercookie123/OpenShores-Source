# Community fork development plan

Prepared October 31, 2026. Owner: Mastercookie123. Initial model: free community game.

## Decision and current status

Fork created: https://github.com/Mastercookie123/OpenShores-Source

Use Celarious/OpenShores-Source as the engineering baseline; retain Hazeron-Freehold-LLC/Shores-of-Hazeron as the original upstream reference. OpenShores already has CMake, Docker toolchains, Windows cross-compilation, server deployment and resource packaging. Recreating those from the original layout would duplicate work.

Inspected baseline: 778b450a8870ad89996edb47ec0468ef157a71ed (October 31, 2026).

Verified locally: Git and GitHub CLI exist; GitHub authentication works outside the sandbox. WSL reports that it is not installed. Docker, CMake and Ninja were not found on PATH. The WSL install command did not install it. No server/client build or gameplay test has passed yet. Source downloads are being attempted; do not mistake a partial .git folder for a usable checkout.

## What the system contains

- C++11, Qt 5 (Core/Gui/Network/OpenGL/Sql/Widgets), SDL2, OpenAL and OpenGL/GLU.
- Linux toolchain container based on Ubuntu 22.04; Windows cross-toolchain based on Debian 13 and pinned MXE revisions. First Windows toolchain build compiles its dependencies and can be substantial.
- PostgreSQL persistence, with separate login and universe databases.
- Login master/worker, mail master/worker, scene master/manager/workers. These are roles, not necessarily separate physical machines.
- Apache/PHP/CGI web account and status tools, plus scheduled avatar and empire reports.
- A native scene client and optional launcher, chat/mail clients and administrative monitor.

The current deployment supports one Docker host and multiple worker containers. It is not a ready-made multi-host orchestrator.

## First milestone: reproducible private local game

1. Install WSL 2 with Ubuntu from an Administrator PowerShell, restart if Windows requests it, then complete Ubuntu's initial username/password setup. Use Microsoft's instructions if `wsl --install -d Ubuntu` still returns only the missing-WSL notice: https://learn.microsoft.com/windows/wsl/install
2. Install Docker Desktop, enable its WSL 2 engine and Ubuntu integration. Alternative: install Docker Engine plus Compose inside Ubuntu following https://docs.docker.com/engine/install/ubuntu/. Choose one Docker daemon for this workflow.
3. In Ubuntu install Git and Make; verify `docker version`, `docker compose version` and `docker run --rm hello-world`. These must work for the regular development user.
4. Keep the Linux development checkout in the WSL filesystem for build performance:

   ```bash
   sudo apt update
   sudo apt install git make
   mkdir -p ~/src
   cd ~/src
   git clone https://github.com/Mastercookie123/OpenShores-Source.git
   cd OpenShores-Source
   git remote add upstream https://github.com/Celarious/OpenShores-Source.git
   git remote add original https://github.com/Hazeron-Freehold-LLC/Shores-of-Hazeron.git
   make server-up
   make server-status
   make win-client CONFIG=debug
   ```

5. Launch the Windows client from its complete `build/windows-debug/bin` directory (Windows can browse the WSL checkout through `\\wsl.localhost\Ubuntu\home\<user>\src\OpenShores-Source`). Keep all bundled DLLs/resources beside it:

   ```powershell
   .\AuSceneClient.exe -alocalhost -log
   ```

   Initial local test account: `local` / `local`. These defaults are for isolated development only. The web status page defaults to http://localhost/.

6. If Docker Engine is inside WSL NAT networking, TCP localhost forwarding is insufficient for game UDP. Get Ubuntu's actual IPv4 address with `hostname -I`, copy `deploy/server/environments/local.env.example` to the ignored `local.env`, set both `BIND_ADDRESS` and `PUBLIC_HOST` to that IP, rerun `make server-up` and launch with `-a<WSL-IP>`. Preserve other overrides. Recheck after WSL restarts. Docker Desktop networking should be tested independently rather than assuming the NAT workaround applies.
7. Acceptance: all expected containers healthy; log in; create an avatar; enter the world; test movement, attack and held-item switching (UDP); create persistent state; stop with `make server-down`, restart and confirm state survives. Then connect a second client and confirm shared state.

Capture exact commit, tool versions, build logs and connection results. `make server-logs SERVICE=scene-worker-1` follows a worker log. `make server-down` preserves data; `make server-reset` deletes the local universe and reports. Do not reset a world you need.

## Repository setup

- Preserve history, LICENSE and attribution; add the fork's own README explaining ownership, goals and upstream links. Choose new game branding before publishing player-facing packages.
- Keep `main` releasable; use short feature branches and pull requests. Tag the first working baseline before gameplay changes.
- Start with one milestone board: reproducible build, local gameplay, persistence/restore, private alpha, public alpha.
- Add a build-only CI workflow for Linux server and Windows client. The inherited workflow is production publishing, not ordinary PR validation. It requires GHCR_USERNAME, GHCR_TOKEN and SERVER_PRODUCTION_ENV; do not add production credentials just to get a build check running.
- Build release artifacts from tagged commits, keep checksums and matching server/client versions, and retain a previous working release for rollback.
- Audit dependencies/assets and retain third-party license notices. The existing RunTests target uses doctest and AuUtil; inspect coverage and wiring before treating it as a game integration test suite.
- Add focused regression coverage for bugs fixed, protocol serialization, configuration validation and database compatibility. Add an automated fresh-database startup smoke test and a restart/persistence test.

## License issue to resolve

Both repositories carry a custom Shores of Hazeron license requiring copyright/permission notices and stating a 5% royalty when fees are charged. The original repository's current text includes a first-$100,000 exemption; the OpenShores copy inspected does not. Preserve the inherited license rather than silently replacing it. Record provenance and get clarification from the rights holder before introducing paid access, bundles or other monetization. Free community operation is the initial plan. This document is a record of the source text, not a legal determination about specific revenue arrangements.

## Private alpha hosting

Use a Linux Docker host with persistent SSD storage. Begin with one scene worker and measure memory, CPU, storage growth and tick latency before choosing a long-term machine or adding workers. Provisional evaluation targets, not upstream requirements: a development PC with 16–32 GB RAM and generous SSD space; a small alpha host with 4–8 CPU threads and 16 GB RAM. Actual capacity is unmeasured. Do not purchase a long contract until load measurements exist.

- Keep development, staging and production databases/secrets separate. Use a dedicated production hostname and fresh generated passwords in ignored environment files.
- Validate `make server-prod-config` before `make server-prod-up`. Use Release builds for the hosted server.
- Public defaults: TCP masters 16757–16759, login worker 16767, mail worker 16768, scene manager 16769; scene worker TCP/UDP and chat ports are generated from the worker configuration. Confirm exact generated port mappings before opening a firewall. Expose only necessary game ports; keep database and management interfaces on loopback/private access.
- Put the web service behind HTTPS; HTTPS for the website does not secure the game's legacy protocol. Assess authentication/password storage, transport, CGI/account abuse and admin privileges before accepting public users. Use private access for early testing.
- The inherited production image workflow creates a deployment artifact containing production credentials. Restrict access and retention; preferably redesign packaging so secrets are supplied on the host rather than included in downloadable artifacts.
- Back up both PostgreSQL databases consistently and the generated reports volume. Encrypt off-host copies, define retention and perform a restore drill on staging before inviting players. A Docker named volume alone is not a backup.
- Record metrics for process restarts, database failures, resource saturation and worker load; bound logs and alert on failures. Define maintenance and incident ownership.
- Account registration exists; password reset is intentionally not publicly wired because mail delivery is unconfigured. Plan mail delivery and safe reset semantics before enabling it.

## Development sequence

1. Baseline: clean build, local login, UDP input and persistence verified; document reproducible setup.
2. Reliability: backup/restore rehearsal, staging deployment, versioned releases and rollback, basic CI.
3. Private alpha: a few invited testers, measured load, crash collection, protocol/auth assessment and account tooling.
4. Public alpha: independent branding/endpoints, public issue tracker and support policy, operations runbook and monitored hosting.
5. Gameplay work: prioritize a small set of improvements, one subsystem at a time. Track upstream fixes; avoid a wholesale engine rewrite before the existing game is reproducible.

## Sources

- Original: https://github.com/Hazeron-Freehold-LLC/Shores-of-Hazeron
- Engineering baseline: https://github.com/Celarious/OpenShores-Source
- Build instructions: https://github.com/Celarious/OpenShores-Source/blob/main/README.md
- Server operations: https://github.com/Celarious/OpenShores-Source/blob/main/deploy/server/README.md
- Original license: https://github.com/Hazeron-Freehold-LLC/Shores-of-Hazeron/blob/main/LICENSE
- Inherited license: https://github.com/Celarious/OpenShores-Source/blob/main/LICENSE

## Decisions still needed

Final project/game name; whether GitHub fork networking or an independent repository is preferred; available build-PC resources; eventual hosting budget and region; expected alpha player count; intended gameplay changes and contributor availability. None prevents validating the local baseline.

## Setup progress — November 1, 2026

Docker Desktop installed successfully through winget; the installed CLI reports Docker 29.9.2. WSL is now present, but `wsl --list --verbose` reports no registered distributions. `wsl --install -d Ubuntu --no-launch` reported success with an explicit requirement to reboot Windows before changes become effective. Complete that restart before attempting the Linux build. A fresh checkout is downloading to `CommunityShores`; the earlier `OpenShores` folder is incomplete and must not be used as a build baseline. The server and client have not yet been built or launched.
