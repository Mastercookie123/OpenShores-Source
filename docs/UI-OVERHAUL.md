# OpenShores command interface

The scene client now defaults to the new native Qt interface. It keeps the
original game icons, navy panels, gold accents, dockable tools and game systems.

## What changed

- A labeled navigation rail replaces the old hover-only icon strip during play.
  Compact mode reclaims space and expands on demand; narrow windows start compact.
- Ctrl+K searches available panels and common actions, including less frequently
  used ship and empire tools. Enter opens a result; arrows navigate and Escape closes.
- Ship overview uses the existing role-dependent panel selection. Governance
  explains its empire requirement instead of opening an unavailable panel.
- Newly opened panels dock beside the world view. Multiple right-side panels use
  tabs instead of covering the world with overlapping floating windows.
- Gear has a name filter that keeps matching nested items and their containers.
  Filtering clears selection so a hidden item cannot remain the target of an action.
  Funds, armor and load have labels; common actions include readable text.
- Text-entry keys are kept out of the game's global control stream. Key releases
  still propagate so held inputs can be released after focus changes.
- The Game menu exposes settings, camera reset, fullscreen, single-player save,
  panel layout reset and a concise getting-started guide.
- Larger controls, clearer focus/selection states and consistent dark panel chrome
  improve readability. Existing function-key panel shortcuts continue to work in play.

`Launch-Modern-UI.cmd` opens the latest Filament client. `Launch-Classic-UI.cmd`
uses the same build with `-ui-classic`. The renderer and procedural game systems
are shared; the classic option skips the new navigation, theme and inventory filter.

## Implementation

`src/app/clients/scene/modern_ui.h` owns the command interface and theme, without
new runtime dependencies. It routes to existing frame slots and dock widgets.
Inventory filtering works on the existing gear model and does not replace item
or equipment logic. The original HUD, contextual item actions, specialist panel
contents and designer tools remain available. The design editor retains its
original view controls.

## Verification

`Test-Client.ps1` creates an isolated single-player profile and exercises the real
client. Modern mode additionally checks navigation-button routing, inventory
matching and empty results, text-input isolation, command search and opening,
compact-mode toggling, narrow-window expansion and control bounds. It saves
whole-window screenshots after layout and rendering settle. `-ClassicUI` runs
the same gameplay smoke test with the old interface; `-Fixtures` adds both visual
regression suites. Results are under `Artifacts/client-smoke`, with curated UI
captures under `Artifacts/modern-ui/latest`.

## Status and discovery improvements

The in-world HUD reads health, stamina, food and the selected item from
active character data. Meters have numerical tooltips and accessible names;
selecting the equipped-item button opens Gear. Disconnected values are unavailable.
The single-player save action is disabled in network sessions.

Panel search matches multiple words, remembers the last six opened panels in
`UI/RecentPanels`, and explains why locked governance tools are unavailable.
Inventory search reports item counts and gives clear guidance for empty results.

The automated UI checks modify and restore food on the disposable test character
to verify the live meter, exercise the selected-item shortcut, recent ordering,
multi-word search and locked commands, and capture their actual native UI.

## Task guidance

The quick guide now offers direct routes to equipment, handcrafting, the star map,
and control settings. It explains panel behavior and how to return to playing.
The command bar includes Return to world, which closes panels and restores game
focus. Search accepts inventory synonyms such as backpack and exposes the guide
and return action alongside panels. Automated checks exercise the guide route,
return action and synonym search in the isolated gameplay client.

## Spatial inventory and game HUD

Modern Gear defaults to a spatial Grid tab; List keeps the original tree view.
Tiles use the commodity's real PackSize. Open a bag by double-clicking it or
choosing it in the container selector. Each bag reports used / maximum pack units
and remaining units, with a capacity meter. Container tiles show mini fullness
meters. Search dims nonmatching tiles without concealing occupied space.

Click a tile for the existing item actions and size; drag to rearrange tiles or
onto a container to store the item. Move to offers destinations across carried
containers. Auto arrange packs the current diagram. Rearrangement is a local,
session-only presentation; item transfers use MoveGearItemToItem and the existing
capacity/type rules. This does not introduce a network/save format or new geometric
storage restrictions. The numeric fullness meter is authoritative; the diagram
may have spare layout cells around item rectangles.

Health, stamina and food use the custom-painted game_hud.h widget over the world:
a beveled dark housing, a red HP gauge with current/max values, a green stamina
gauge, amber food pips and vector heart/lightning/ration crests. Low health shows
INJURED/CRITICAL and low food shows HUNGRY. Tooltips provide raw values. The held
item is a separate illustrated Gear shortcut. Overlays anchor to the viewport and
resize around panels; no bottom toolbar consumes world space. There is no
continuous animation timer or added GPU renderer dependency.

The original self bars, springs and mini-me are suppressed only while the modern
HUD is active. Movement and jump power feed the new HUD caption; vehicle/target
HUD and the classic UI remain available. The grid uses shaded tile surfaces,
hover feedback, gold selection and restrained icon scaling.

The disposable gameplay fixture includes a backpack containing a hammer and a
knife. UI verification checks actual capacity/occupancy, tile selection, drag
rearrangement, overlap rejection and transfers through the game. Separate grid
fixtures cover full and empty containers. Captures include ui-container.png and
ui-container-transfer.png.

HUD verification also captures a critical-state fixture, checks raw character-data binding, and asserts narrow-window overlay bounds and non-overlap.

The compact HUD is capped at 344 x 101 pixels (80% of the original painted size). The held-item panel is reduced to 170 x 70 pixels, with a 28-pixel icon; both remain adaptive in narrow viewports.
