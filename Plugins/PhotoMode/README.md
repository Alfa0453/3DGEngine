# Photo Mode plugin

This Plugin API 1 example adds a script-driven runtime photo mode to Editor
Play and packaged games.

## Enable and build

1. Open **Panels > Plugin Manager**.
2. Enable **Photo Mode**, then choose **Apply Changes**.
3. Reconfigure and rebuild when prompted.
4. Attach registered script `PM_PhotoModeController` to one persistent object.

Default controls: **F8** toggles Photo Mode, **W/A/S/D** flies, **Q/E** moves
down/up, mouse looks, **Left Shift** accelerates, and **F9** saves a clean BMP
to the project's `Screenshots` folder.

## Optional script fields

| Field | Type | Default | Meaning |
|---|---|---:|---|
| `toggleKey` | Int | 297 (F8) | GLFW toggle key. |
| `captureKey` | Int | 298 (F9) | GLFW capture key. |
| `freezeGameplay` | Bool | true | Freezes gameplay with global time dilation. |
| `fieldOfView` | Float | 45 | Camera FOV (10-120 degrees). |
| `moveSpeed` | Float | 6 | Units per unscaled second. |
| `lookSensitivity` | Float | 0.10 | Degrees per mouse pixel. |
| `exposureEV` | Float | 0 | Exposure compensation. |
| `saturation` | Float | 1 | Color saturation. |
| `contrast` | Float | 1 | Color contrast. |
| `depthOfField` | Bool | false | Enables focus blur. |
| `focusDistance` | Float | 5 | Focus distance in world units. |
| `focusRange` | Float | 2 | In-focus range. |
| `blurStrength` | Float | 0.6 | Maximum blur (0-1). |

Other scripts can call `Enable`, `Disable`, `Toggle`, and `Capture` through a
script handle. `Capture` accepts an optional string argument named `filename`.
Native code can include `PhotoMode/PhotoMode.h` and use the same operations.
