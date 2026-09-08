# UI Resolution and Localization Preview

Open **Panels > Content Editors > UI Resolution & Localization Preview** to test
the active HUD without entering Play mode. The preview is read-only: it does not
change the HUD document, localization table, runtime language, or scene.

## Preview a HUD

The tool displays the HUD currently loaded by the HUD Editor or referenced by the
scene. Choose a common desktop, ultrawide, portrait, or phone resolution, or enter a
custom size. The canvas uses the same anchor and widget-rectangle calculation as the
runtime HUD.

Use **DPI Scale** to simulate larger or smaller interface scaling. Configure horizontal
and vertical safe areas to represent television overscan, rounded screens, or camera
cutouts. The green rectangle is the safe area. Widgets outlined in red extend beyond
the screen; amber widgets need review.

The resolution matrix evaluates the HUD at all standard presets simultaneously. Click
a row to switch the main preview to that resolution.

## Test localization

Select an engine-owned `.3dgloc` table and language. The preview resolves each widget's
localization key without changing the global runtime localization state. Bound health,
score, and named values are substituted after translation, matching runtime order.

- **Text Expansion** pads translated text to test layouts before translations arrive.
- **Pseudo-localize** expands vowels and adds visible markers around the text.
- **Auto RTL** enables mirrored anchors and right alignment for Arabic, Hebrew, Persian,
  Urdu, Pashto, Divehi, and Kurdish language codes.
- **Force RTL** tests mirrored layout with any language.

The canvas uses the editor's display font as a visual proxy. Diagnostics load the
actual `.3dgfont` assigned to the widget or language and inspect its cooked atlas, so
missing font files and missing glyphs are still reported accurately.

## Diagnostics

The diagnostics panel reports:

- widgets outside the screen or configured safe area;
- localized text wider than a non-wrapping widget;
- missing localization keys and language translations;
- missing or unreadable engine font assets;
- characters absent from the selected font atlas;
- non-ASCII text falling back to the built-in bitmap font.

Resolve red errors before packaging. Amber warnings may be intentional, but should be
reviewed at every target resolution and shipping language.
