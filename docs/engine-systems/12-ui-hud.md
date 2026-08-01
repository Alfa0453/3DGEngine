# UI and HUD

## Purpose

The UI layer covers immediate editor UI, runtime game HUD assets, text
rendering, bound gameplay values, buttons, menus, and world-space health bars.

## UI layers

| Layer | Use |
|---|---|
| `ImGuiLayer` | Editor panels, docking, controls, debug tools |
| `UI` | Lightweight engine UI drawing helpers |
| `Hud` | Authored runtime HUD assets and binding/action evaluation |
| `TextRenderer` | Bitmap and TrueType text rendering |

Editor ImGui windows are not the runtime game HUD. A HUD must be saved as an
asset and selected by the scene or game-mode configuration.

## HUD widgets

The HUD asset supports:

- Panel
- Text
- Bar
- Button
- Image

Widgets store visibility, anchor, pixel offset, size, color, and type-specific
properties. Anchors make a layout adapt to changes in the game viewport.

## Data bindings

Text and bars can read named runtime values. A text pattern containing `{}` is
formatted with the bound value.

Typical values include:

- `health` and `max_health`;
- `score`;
- `time`;
- `gamestate`;
- ammunition, mana, objective progress, or custom script values.

The binding key is case-sensitive. The source kind must match the value type:
for example, a time value should use a float source and a game-state label
should use a string source.

The runtime host builds the HUD context each frame. Scripts publish or update
named values through the script API; merely typing a key into the HUD editor
does not create a live gameplay value.

## Actions and buttons

Buttons emit named actions. Gameplay or menu logic consumes those actions to:

- start or restart a level;
- resume or pause;
- open another menu;
- change settings;
- quit or return to a title screen.

Keep button layout in the HUD asset and game-state changes in gameplay code.

## Health display

Player health bars usually bind to the active player values in the HUD context.
World-space enemy health bars use the entity’s health component, project the
world position into screen coordinates, and hide when the entity is not
visible or no longer alive.

Damage should update the health component first. HUD code reads that state; it
should not own combat rules.

## HUD Editor

The HUD Editor provides:

- widget creation and deletion;
- a layout preview;
- anchors, offsets, size, colors, and text;
- binding source and key;
- button actions;
- save, load, new, and Use in Scene operations.

Double-clicking a `.hud` asset in the Content browser opens this panel.

## Main-menu workflow

1. Create a new HUD asset.
2. Add a background panel or image.
3. Add title text.
4. Add Play, Options, and Quit buttons.
5. assign named actions to the buttons.
6. Save the asset.
7. Select it as the menu HUD in game-mode or scene settings.
8. Handle the actions in gameplay code.

## Gameplay HUD workflow

1. Add a player health bar.
2. Bind current and maximum values.
3. Add score and time text using `{}`.
4. Publish matching named values from the gameplay scripts.
5. Set the HUD as the gameplay HUD.
6. Enter Play and inspect the runtime version, not only the editor preview.

## Troubleshooting stale values

- Confirm the active HUD is the edited asset.
- Confirm the runtime context publishes the exact key.
- Confirm source types match.
- Confirm the script is attached, enabled, compiled, and running.
- Confirm the value is updated during runtime rather than only initialized.
- Confirm a saved HUD was reloaded after changes.
- Check that editor preview placeholders are not being mistaken for live data.

## Important source files

- `engine/include/engine/ui/Hud.h`
- `engine/include/engine/ui/UI.h`
- `engine/include/engine/ui/ImGuiLayer.h`
- `engine/include/engine/graphics/TextRenderer.h`
- `engine/include/engine/graphics/TrueType.h`
- `editor/include/HudEditorPanel.h`

