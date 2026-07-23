# First Game Milestone - Coin Rush

## Objective

Create and ship a small third-person collection game named **Coin Rush**. The
player explores a lit arena, collects every coin, avoids an AI enemy, reads health
and score on a HUD, reaches a victory or game-over state, and launches the same
game from a packaged standalone player.

## Definition of done

- A project contains saved editor scenes under its `Scenes` folder.
- The arena has lighting, collision, a player start, coins, an enemy, and navigation.
- The player can walk, sprint, jump, collide with the arena, and take damage.
- Coins use trigger events and a registered gameplay script to add score and disappear.
- The enemy navigates within a Nav Mesh Bounds Volume and uses a behavior graph.
- The HUD displays health, score, elapsed time, and the game state.
- Collecting the last coin produces victory; losing all health produces game over.
- Audio and particles provide pickup and damage feedback.
- The game passes editor playtesting and F8 runtime validation.
- F7 exports the runtime scene and the packaged player starts it successfully.

## Milestone map

| ID | Milestone | Deliverable | Exit gate |
|---|---|---|---|
| M0 | Project foundation | Project folders, first saved scene, source-control checkpoint | Scene reopens from the project's `Scenes` folder without errors |
| M1 | Graybox arena | Ground, walls, staircase/ramps, lighting, colliders | Player-sized capsule cannot leave the play area |
| M2 | Player loop | Player Start, controller, health, camera tuning | WASD, sprint, jump, camera collision, and health work in Play mode |
| M3 | Collectible prototype | One coin with trigger collider, visual, audio, particles, `Coin` script | Entering the trigger adds score once and removes the coin |
| M4 | Collectible pass | Multiple duplicated coins with stable `Coin_` names | Every coin is reachable and the remaining count reaches zero |
| M5 | Navigation | Nav Mesh Bounds Volume and navigation debug | Walkable debug overlay covers intended floor and excludes obstacles |
| M6 | Enemy AI | Enemy, health/team, Nav Agent, Behavior Graph/Blackboard | Enemy patrols, detects the player, chases, and attacks in range |
| M7 | Rules | `GoalTracker` plus built-in player-death rule | Last coin wins; zero player health loses; restart works |
| M8 | HUD and menus | Health bar, score/time text, state message, restart control | HUD updates live and remains usable at the target resolution |
| M9 | Feedback | Pickup particles/audio, damage sound, camera shake | Actions are readable without watching debug panels |
| M10 | Integration test | Full editor playthrough and repaired defects | Victory and defeat paths both pass from a fresh Play session |
| M11 | Runtime export | Saved editor scene, F7 runtime scene, F8 validation | Validation reports no missing assets or invalid runtime components |
| M12 | Package and release | Player build, config, bundled game directory, ZIP | Game launches outside the editor on a clean machine/folder |

## Ordered production checklist

### M0 - Project foundation

1. Open or create the game project and confirm its Content root.
2. Create folders for `Scenes`, `Assets/Materials`, `Assets/Audio`,
   `Assets/Particles`, `Assets/AI`, and `Assets/HUD`.
3. Create a new scene and save it as `Scenes/CoinRush.scene`.
4. Choose names early: `PlayerStart`, `Enemy`, `GameManager`, and `Coin_01...`.

### M1 - Graybox arena

1. Create ground and boundary walls with primitive colliders.
2. Add a Directional Light and tune World Settings.
3. Add a few obstacles while keeping routes wide enough for the AI agent.
4. Use Physics Status/debug overlays to confirm collider coverage.

### M2 - Player loop

1. Add Player Start and enable the Player Controller.
2. Set walk/run/jump values, capsule dimensions, and third-person camera settings.
3. Add Health at 100/100.
4. Test movement, camera collision, slopes, steps, and respawn/restart behavior.

### M3-M4 - Collectibles

1. Build one `Coin_01` prototype with a trigger collider.
2. Attach a registered `Coin` script and optional Audio Source/Particle System.
3. Verify the single pickup before duplicating it.
4. Duplicate, rename, and place the remaining coins.

### M5-M6 - Navigation and enemy

1. Add a Nav Mesh Bounds Volume that encloses the arena floor.
2. Enable the navigation debug view and rebuild/check walkable coverage.
3. Add the Enemy with collider, Health, Nav Agent, and opposing team.
4. Create a Behavior Graph with Blackboard data, patrol/chase tasks,
   range decorators, and a target-refresh service.
5. Confirm the enemy cannot navigate outside the volume or through obstacles.

### M7-M9 - Rules, HUD, and feedback

1. Attach `GoalTracker` to `GameManager`.
2. Create the HUD and bind health, score, time, game state, and message.
3. Add pickup and damage audio/particles, then add restrained camera shake.
4. Test victory, defeat, pause, and restart flows.

### M10-M12 - Ship

1. Start from Edit mode and complete two clean playthroughs: victory and defeat.
2. Save the editor scene, export with F7, and validate with F8.
3. Configure `player.cfg` and `PLAYER_GAME_DIR`.
4. Build `player`, install the player component, create the package, and test it
   from outside the build tree.

## Release acceptance test

1. Launch the packaged executable without the editor.
2. Confirm the startup scene, HUD, models, materials, audio, particles, and AI load.
3. Collect one coin and verify score, sound, effect, and removal happen once.
4. Let the enemy chase and damage the player; verify health and game over.
5. Restart, collect all coins, and verify victory.
6. Close and relaunch; confirm a clean start and no dependency on absolute paths.

