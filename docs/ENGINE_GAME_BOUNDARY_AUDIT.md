# Engine/game boundary audit

## Verdict

The build boundary is now one-way and enforced: game code depends on Core;
Core does not depend on game code or authored UI. The executable desktop
client is a game-layer composition target rather than part of `core_runtime`.

## Ownership

| Layer | Owns |
| --- | --- |
| `src/core` / `core_runtime` | simulation stores, rendering backend, UTF-8/MSDF fonts, generic scripted GUI compiler/runtime/painter, content loading primitives |
| `src/game` / `core_game_ui` | desktop shell, game-to-UI data adapter, game page commands, legacy game-client HUD fixtures |
| `content/base` | start date, main UI path, language text, page layout, game definitions, history and authored behavior |

The configure step scans every engine source and fails if a file under
`src/core` includes `game/...`. The engine target contains no game-layer
source file.

## Scripted startup and UI

`content/base/game.coreproject` selects the start date, default language and
main UI script. `content/base/ui/main.coregui` owns the current top resource
strip, left navigation, contextual drawer, map workspace and right inspector.
Changing labels, panel sizes, order, visibility and page composition therefore
does not require an engine rebuild.

Core's painter only implements reusable widget behavior and layout. The game
adapter publishes typed data properties and commands to the script schema; it
does not author page geometry in C++.

## Naming

No engine identifier contains `Victoria` or `Victorian`. Neutral capability
names such as `ScriptedGuiPainter`, `MapDecorationRenderer` and
`StrategyHudSystem` are used. The Victoria 3 references in design discussion
are research references only and are not product identifiers or copied assets.

## Acceptance checks

- configure-time reverse-dependency guard;
- `core_content_check` for authored definitions;
- scripted GUI compiler/runtime/painter regression coverage;
- UTF-8 and CJK font-atlas regression coverage;
- real Vulkan desktop validation and screenshot review.
