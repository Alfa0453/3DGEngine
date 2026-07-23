# GMTK Game Jam — 3DGEngine readiness & submission checklist

Jam window: **July 22–26, 2026** (starts 18:00 BST July 22, ends 18:00 BST July 26).
Theme is revealed at the start — scope tiny.

The single biggest risk with a from-scratch engine isn't features (you have plenty)
— it's **stability + packaging under time pressure**. Prove the end-to-end path
BEFORE the theme drops.

---

## A. Do this NOW (before the jam starts)

- [ ] **Clean rebuild.** Delete `build/`, then reconfigure and build BOTH the editor
      and the player. This confirms all recent work compiles (fonts, project system,
      native dialogs, animation refactor).
      ```
      cmake -S . -B build -DGAMEENGINE_STATIC_RUNTIME=ON -DCMAKE_BUILD_TYPE=Release
      cmake --build build --config Release
      ```
- [ ] **Smoke-test the editor.** Open it, create/open a project, drop a player start +
      a floor + a prop, enter Play, move around, exit Play. No crashes.
- [ ] **Build a throwaway microgame** (20–30 min of work): player start → move →
      one trigger/interaction → a win or lose condition on the HUD. This flushes out
      the real authoring workflow so you're not learning it during the clock.
- [ ] **Package the microgame** with `package_game.ps1` (see section C) and
      **run the zip on a clean folder — ideally a second PC.** Confirm the on-screen
      **"Asset errors" is 0** and there are no missing-DLL popups.
- [ ] If packaging fails, fix ONLY that. Do not add engine features now.
- [ ] **Fallback plan:** if the packaged microgame won't run cleanly, seriously
      consider jamming on a known engine this year and keep hardening yours in parallel.

## B. Watch out for (known sharp edges)

- [ ] **Absolute asset paths.** The new project system can set an *absolute* asset
      root. If your scene stores absolute model/texture paths, they won't survive being
      copied to another machine. Keep the game project's assets referenced by relative
      paths; the packaged player's "Asset errors" count will catch this immediately.
- [ ] **OpenGL 3.3** is required — fine on any modern GPU, but note it on the itch page.
- [ ] **Unsigned exe** may trigger a SmartScreen/AV warning on first run — normal for
      jam builds; mention "click More info → Run anyway" on the itch page.
- [ ] **Save often / back up.** Commit the project to git (or copy the folder) after
      every working milestone. Keep one known-good scene you can fall back to.
- [ ] The build sandbox used to prep this was offline, so **every recent code change is
      compile-verified by review only** — the clean rebuild in section A is the real test.

## C. Packaging (at submission time)

1. In the editor: **File → Export Runtime** to produce the runtime scene.
2. Run the packager (from a shell with `cmake` on PATH):
   ```
   ./package_game.ps1 `
       -ContentDir "D:\path\to\YourGame\Content" `
       -SceneRelative "Content/Scenes/level.runtime.scene" `
       -GameName "YourGameTitle"
   ```
   It builds Release + static runtime, stages `player.exe` + `player.cfg` + your
   content beside it, and writes `dist/YourGameTitle.zip`.
3. If the launched game reports **Asset errors > 0**, your scene stores asset paths
   without the `Content/` prefix — re-run with `-StageContentsOnly` and set
   `-SceneRelative "Scenes/level.runtime.scene"`.
4. **Extract the zip to a brand-new folder and double-click `player.exe`.** Verify it
   runs, plays, and shows 0 asset errors. This is the real submission artifact.

## D. itch.io submission

- [ ] Create the project page early (don't do it at the deadline).
- [ ] Kind of project: **Downloadable**. Platform: **Windows**. Upload the zip and
      tick **"This file will be played in the browser"? = No** (it's a download).
- [ ] Record a short gameplay **GIF/screenshots** for the page and the jam gallery.
- [ ] Write 2–3 sentences: what it is, controls, "how does it fit the theme".
- [ ] Note controls + the OpenGL 3.3 / SmartScreen caveats.
- [ ] **Download your own uploaded zip**, extract, run — the final sanity check.
- [ ] Submit before the deadline with buffer; late uploads are the classic jam loss.

## E. Scope discipline (during the jam)

- Pick the smallest idea that expresses the theme. One mechanic, one level.
- Reuse what the engine already gives you for free: primitives, terrain, the HUD
  system, triggers/pickups/doors/damage zones, the camera director, audio buses.
- Lock the feature list by the halfway point; spend the back half on juice,
  audio, and a clean win/lose loop — that's what scores.
- Leave the last few hours ENTIRELY for packaging, testing the download, and the
  itch page. Not for one more feature.
