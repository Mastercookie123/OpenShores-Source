# Campfire lighting and anatomical silhouette revision

Run the current Windows build by double-clicking `Launch-OpenShores.cmd` in the
workspace, or use `Hazeron.ps1 ModularClient`. The latter now selects
`Client/AuSceneClient-modular-r4.exe`; the old binary remains available.
The batch launcher allows its own PowerShell process only and does not change
the system's execution policy. Restart an old game window to load the revision.

The 385-component modular pack now uses authored, continuous radial profiles for
human arms and legs. Deltoid, upper-arm, elbow, forearm, thigh, knee and calf
sections are blended into the source cage instead of retaining stacked bulges.
Procedural vertex groups, exact attachment/scale markers, connection rings,
palette UVs and measured hand/foot extents remain under the existing contracts.
DNA, skeletons, clothing, collisions and server data remain on their original path.

Six production body fragment shader variants now approximate a small reflected
local-light contribution on skin. This reveals some back-facing skin near fires
and lamps. It is a legacy-renderer indirect-light approximation, not ray-traced
bounce lighting. It follows the source color and attenuation and contributes
nothing without a local light. No fixed emissive brightness floor was added.

Validation: 48 production body/hair shader variants compile; four component
contract tests pass. Sixteen native RTX 5080 fixtures cover portraits, full body,
sunlight, night, backlit campfire, clothing, walking, attacks and unusual anatomy.
Original/candidate run in fresh processes with a fixed preview clock. Grip and
camera deltas remain zero. Repeated walk/strike images are deterministic. The
fully unlit control keeps skin black, and missing-pack/checksum controls reject
invalid inputs. The campfire fixture includes a chest luminance target so merely
loading a new pack is no longer enough to pass that particular visual check.

Report: `Artifacts/visual-tests/latest/index.html` in the workspace.
The earlier revision's images remain in `Artifacts/visual-tests/pre-campfire-pass`.
The previous asset pack is retained in `Artifacts/rollback-face-pass/body-parts-modular`.

Limits: face and hair palette artwork remain legacy assets. The character lab
approximates the screenshot's light placement but does not reproduce terrain,
flora, exact sky/exposure or the held hammer mesh. The captures are not proof of
live-world visual acceptance. This revision targets readable fire lighting and
limb silhouettes; it does not complete a new character-art library.