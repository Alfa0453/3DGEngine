# Crystal Chamber — Creating a Game as a Separate Content Project

A complete, step-by-step guide to building **The Crystal Chamber** (Wizard's Trial) as its own
3DGEngine **content project** — a self-contained folder the editor opens and the standalone player
packages and runs. You author it entirely in the editor and visual scripting; no C++.

**The game:** a wizard stands in a sealed room with three crystals. Collect all three and a door
opens; step through the exit to escape and win.

**What it exercises:** the project system, primitives + physics, the player controller, physics
**trigger events**, visual-scripting **events / variables / branch / timeline**, the debugger, and
packaging to the player.

---

## Key engine facts you'll rely on

- A **project** is a folder containing `Project.3dgproject` and a `Content/` tree (with
  `Content/Scenes/Main.scene`). The editor's **File → New Project** scaffolds this for you.
- The physics layer delivers **trigger/collision events straight to the touched object's graph** as
  custom events named exactly:
  `TriggerEnter`, `TriggerStay`, `TriggerExit`, `CollisionEnter`, `CollisionStay`, `CollisionExit`.
  Each carries an **`Other`** (Entity), **`Point`**/**`Normal`** (Vector3), and **`Impulse`** (Float).
  So an `On Custom Event "TriggerEnter"` handler fires whenever a **trigger** collider overlaps.
- Entity-access policy: nodes act on **Self** by default. To touch another object, use a `(Target)`
  node variant fed by a reference (an exposed **Entity** variable picked in the Inspector). You won't
  need cross-object access here — we keep each object's state local and communicate with **events**.
- Graphs communicate on a shared **event bus**: `Broadcast Event` sends to everyone; an
  `On Custom Event` with the same name handles it. No object names are hard-coded.

---

## Step 1 — Create the project

1. Launch **3DGEditor**.
2. **File → New Project**. Pick an empty folder outside the engine source, e.g.
   `D:\Games\CrystalChamber`, and name it `CrystalChamber`.
   The editor writes `Project.3dgproject` and creates `Content/Scenes/Main.scene`. The Content browser
   now roots at this project.
3. Create two folders in the Content browser for tidiness: `Content/VisualScripts` and
   `Content/Levels` (right-click → New Folder). Save (**Ctrl+S**).

You now have a standalone project. Everything below lives inside it; the engine binaries stay separate.

---

## Step 2 — Build the room

Work in the viewport / scene outliner.

1. **Floor:** add a **Plane** (or a flattened Cube), scale it to ~20 × 20. In the Inspector add a
   **Collider** (Box), leave it **static** (no RigidBody) so it's solid ground.
2. **Walls:** add four **Cubes**, stretch each into a wall around the floor's edges, each with a static
   **Box Collider**. (Duplicate with Ctrl+D and reposition.)
3. Add a **Light** (Directional) so you can see, and position the editor camera to look down into the
   room. Save.

Tip: keep the room centered on the origin — it makes the door/crystal offsets below easy to reason about.

---

## Step 3 — Add the player

1. Add the wizard: either import a character via the **Character Editor** or, for a first pass, add a
   **Capsule** as a stand-in.
2. Give it a **RigidBody** (dynamic, mass ~1, gravity on) and a **Capsule Collider**.
3. Enable the **player controller** for this object in **Game Mode settings** (the play loop wires
   `PlayerController` to the designated player; pick `CameraRelative` facing for classic third-person
   or `MovementDirection` for free-orbit). Confirm **player input enabled** is on.
4. Enter **Play** briefly to check you can walk around and the camera follows, then exit Play.

---

## Step 4 — Add the crystals

1. Add a small **Cube** (or Sphere), name it `Crystal_1`, place it in the room.
2. Give it a **Box Collider** and tick **Is Trigger** (so it fires `TriggerEnter` instead of blocking
   the player). No RigidBody needed on a static trigger, but the player's dynamic body overlapping it
   will generate the event.
3. **Duplicate** it twice (Ctrl+D) → `Crystal_2`, `Crystal_3`, and spread them around the room.

We'll attach the same `Crystal` graph to all three in Step 8.

---

## Step 5 — Add the door and the exit

1. **Door:** add a tall **Cube**, name it `Door`, and place it blocking the only opening in the wall.
   Give it a static **Box Collider** so it's solid while closed. Note its **closed position** (say
   `(0, 2, -10)`) and decide an **open position** (e.g. slide up: `(0, 6, -10)`).
2. **Exit:** add a thin **Cube** just past the door, name it `Exit`, give it a **Box Collider** with
   **Is Trigger** on. Stepping into it after the door opens will win the level.

Save the scene.

---

## Step 6 — Author the Crystal graph

Open **Panels → Visual Script Editor**.

1. In the **Template** dropdown choose **Pickup** → **Create from Template**. Save it as
   `Content/VisualScripts/Crystal.3dgvs`. It gives you an `On Custom Event "TriggerEnter"` handler.
2. Replace the template's Print with the pickup logic. Drag from the handler's **Then** pin into empty
   canvas and, from the search, add:
   - **Broadcast Event** → in the side-panel **Events** section add an event `CrystalCollected`, then
     use its **Broadcast** button to drop a pre-wired Broadcast node; wire the handler **Then** →
     Broadcast **In**. Leave **Broadcast** = true.
   - **Set Position (Self)** → set its **Position** default to `(0, -100, 0)` and wire Broadcast
     **Then** → Set Position **In**. (This "collects" the crystal by dropping it out of sight; simple
     and reliable for a starter. You can swap in a particle/despawn later.)
3. (Optional polish) Before broadcasting, add a **Print String** "Crystal collected" for feedback.
4. **Save** (validates automatically; the Validation section should read *No errors*).

Flow: player overlaps crystal → `TriggerEnter` → broadcast `CrystalCollected` → crystal sinks away.

---

## Step 7 — Author the Door graph

New graph → **Door** template → save as `Content/VisualScripts/Door.3dgvs`. The template already has
`Timeline → Lerp (Vector3) → Set Position (Self)`. We change its trigger and gate it on a counter.

1. **Variables** section → **Add** `Crystals` (Int). Add **Open** (Bool) too if you want a guard.
2. Retarget the entry: set the template's `On Custom Event` **eventName** to `CrystalCollected`
   (or delete it and, from the **Events** section, create/handle `CrystalCollected`).
3. Count + gate. Between the event and the Timeline, build:
   - `Get Crystals` (Variables → **Get**) → **Add (Int)** with **B** = 1 → `Set Crystals`
     (Variables → **Set**). Wire the event **Then** → Set Crystals **In**.
   - `Get Crystals` → **Greater or Equal (Int)** (search "Greater") with **B** = 3 → **Branch**
     **Condition**. Wire Set Crystals **Then** → Branch **In**.
   - Branch **True** → the **Timeline** **Play**.
4. Set the Lerp's **A** = the door's **closed** local offset `(0,0,0)` and **B** = the **open** offset
   (e.g. `(0,4,0)` to slide up 4 units). The Timeline drives **Alpha** → Lerp → **Set Position (Self)**,
   which is exactly what the door needs (it moves itself).
5. Announce the open state so the exit can react: from Branch **True**, also add a **Broadcast Event**
   `DoorOpened` (broadcast = true). Wire Branch **True** → Broadcast **In** (a Sequence node lets you
   fire both the Timeline and the Broadcast from one exec pin).
6. **Save**.

Flow: each `CrystalCollected` bumps `Crystals`; on the third the door animates open and broadcasts
`DoorOpened`. No object names, no cross-object reads.

---

## Step 8 — Author the Level Exit graph

New graph → **Blank** → save as `Content/VisualScripts/LevelExit.3dgvs`.

1. **Variables** → **Add** `DoorOpen` (Bool, default false).
2. Add `On Custom Event "DoorOpened"` (Events section) → `Set DoorOpen` = true.
3. Add `On Custom Event "TriggerEnter"` → **Branch** on `Get DoorOpen`:
   - **True** → **Print String** "You escaped!" (your win feedback). Optionally also fire a HUD panel or
     `Request Scene Load` to a victory scene.
   - **False** → (leave unwired — walking through before the door opens does nothing).
4. **Save**.

Flow: the exit stays inert until it hears `DoorOpened`; then stepping into its trigger wins.

---

## Step 9 — Attach the graphs to objects

Select each object and, in the Inspector, expand **Visual Script** → **Graph** and pick:

- `Crystal_1`, `Crystal_2`, `Crystal_3` → **Crystal**.
- `Door` → **Door**.
- `Exit` → **LevelExit**.

If you exposed any variable (checked **Exposed on Component**), you can override it per object here.
Save the scene. Because references are stored by stable id, you can freely rename or move these graph
files later without breaking the links.

---

## Step 10 — Play-test and debug

1. Press **Play**. Walk the wizard into each crystal — each should sink away and (if you added Print)
   log "Crystal collected". On the third, the door slides open; the exit then wins on entry.
2. Open the **Visual Script Editor** → **Debugger** while in Play:
   - Right-click the Door's **Branch** → **Add Breakpoint** (F9); collect crystals to pause there and
     inspect **Live Variables** and the **Call Stack**.
   - Watch the **Event Trace** to see `CrystalCollected` and `DoorOpened` broadcasts (sender → receiver).
   - Toggle **Compiled execution** and glance at **Budgets** to sanity-check performance.
3. Use the **Automated Tests → Run Graph Test** button on each graph for a quick no-error smoke check,
   and **Validate Project** in **Asset Lifecycle** to confirm the whole project is clean.

---

## Step 11 — Package and run in the standalone player

1. In **Asset Lifecycle** click **Packaging Audit** to confirm every `.3dgvs` compiles clean with no
   validation errors.
2. Cook/package the project to the player (the player stages `Content/` + scenes + the cook manifest):
   ```
   cmake -S <engine> -B build -DPLAYER_COOKED_DIR=D:/Games/CrystalChamber/Build/Cooked/CrystalChamber
   cmake --build build --target player
   cmake --install build --prefix D:/Games/CrystalChamber/dist --component player
   ```
   (Or use the editor's package action if present.)
3. Run the produced **player** executable. It finds `player.cfg` + the cooked `Content/` beside it,
   loads your scene, and runs the exact same graphs — packaged builds strip editor-only data and run
   with diagnostics off, executing the compiled graph data directly.
4. Collect three crystals, watch the door open, step through the exit — you've shipped a game.

---

## Appendix — graph node maps (quick reference)

**Crystal.3dgvs**
```
On Custom Event "TriggerEnter" ──▶ Broadcast Event "CrystalCollected" (Broadcast=true) ──▶ Set Position (Self) [Position = 0,-100,0]
```

**Door.3dgvs**  (variable: Crystals : Int)
```
On Custom Event "CrystalCollected"
   └▶ Set Crystals = (Get Crystals) + 1
         └▶ Branch [ (Get Crystals) >= 3 ]
               ├─ True ─▶ Sequence ─▶ Timeline.Play  ──(Update/Alpha)──▶ Lerp(A=0,0,0 ; B=0,4,0) ─▶ Set Position (Self)
               │                    └▶ Broadcast Event "DoorOpened" (Broadcast=true)
               └─ False ─ (nothing)
```

**LevelExit.3dgvs**  (variable: DoorOpen : Bool)
```
On Custom Event "DoorOpened"  ──▶ Set DoorOpen = true
On Custom Event "TriggerEnter" ─▶ Branch [ Get DoorOpen ]
                                     └─ True ─▶ Print String "You escaped!"
```

### Next steps / polish
- Swap the crystal "sink" for a **particle burst** + despawn, and add a **pickup sound**.
- Show a HUD counter "Crystals: n/3" by driving a HUD Text widget from the Door's `Crystals` variable.
- Replace the capsule with a real wizard character + an **animation graph** (Idle/Walk).
- Add an **Ability** (fireball) using the Ability template + Cooldown gate.
- Turn the door/exit logic into a reusable **function** or an **interface** (`IInteractable`) so more
  objects can share it.
```
