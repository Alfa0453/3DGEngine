# Localization Editor Guide

The Localization Editor creates `.3dgloc` assets that hold translated text, language
fonts, localized media, and subtitle timing in one engine-owned table.

## Create a table

1. Open **Panels > Content Editors > Localization Editor**.
2. Press **New** and set the table name, source language, and fallback language.
3. Add each language using a BCP-47-style code such as `en`, `fr`, or `pt-BR`.
4. Add text entries. Keys should be stable names such as `UI.Menu.Play` or
   `Dialogue.Wizard.Greeting`; Source Text is the safe fallback shown when a
   translation is missing.
5. Add translator context where wording depends on screen, character, or action.
6. Save. The default location is `Content/GameAssets/Localization`.

The language list displays how many text entries are missing for each language.
Search filters by key, source text, and translator context. Pseudo-localization expands
and brackets the preview so clipped or fixed-width UI can be found before translation.

## CSV translator workflow

**Export CSV** writes a file beside the `.3dgloc` table. Its first columns are `key`,
`source`, and `context`, followed by one column for every language. Translators can
edit the CSV without the engine. **Import CSV** updates matching keys, adds new keys,
and adds new language columns without deleting entries that are absent from the CSV.

## Fonts, assets, and subtitles

Under **Language Fonts**, assign a `.3dgfont` that contains the glyphs required by
that language. **Localized Assets** maps one stable key to per-language textures,
audio, movies, or other content, with a fallback path. **Subtitle Timeline** stores a
text key, speaker, start time, duration, and optional localized voice path for each cue.

## HUD and dialogue

HUD Text and Button widgets now expose **Localization Key**. When it is set, the
localized string is resolved first and live placeholders such as `{}` are substituted
afterward. Dialogue speakers, lines, and choices already expose localization keys;
`DialogueText()` now returns the translated dialogue line when a table is active.

## Script API

Native C++ scripts inherit these calls from `engine::Script`:

```cpp
LoadLocalization("GameAssets/Localization/GameLocalization.3dgloc");
SetLanguage("fr");
const std::string title = Localize("UI.Menu.Play", "Play");
const std::string voice = LocalizedAsset("Voice.Wizard.Greeting");
```

Lua scripts use the same functions through `Engine`:

```lua
Engine.LoadLocalization("GameAssets/Localization/GameLocalization.3dgloc")
Engine.SetLanguage("fr")
local title = Engine.Localize("UI.Menu.Play", "Play")
local voice = Engine.LocalizedAsset("Voice.Wizard.Greeting", "")
```

Load the table once when the game starts. Save the chosen language in the player's
settings, then call `SetLanguage` after loading those settings. Missing keys return the
provided fallback; if no fallback exists they return the key itself, making mistakes
visible during development.
