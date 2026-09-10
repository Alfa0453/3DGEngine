# Visual Scripting Tutorial — The Crystal Door (Wizard's Trial)

This walkthrough builds one complete gameplay feature end to end using only visual scripting —
no C++ or Lua. You will make a **door that opens after the wizard collects three crystals**, then
debug it, package it, and run it in the standalone player.

By the end you will have used: **templates, variables, custom events (broadcast + handler),
math and branch nodes, a timeline, the debugger, the packaging audit, and the player**.

---

## 0. Concepts in one minute

- A **graph** is a `.3dgvs` asset that runs on a scene object during Play. You attach it in the
  object Inspector under **Visual Script**.
- **Events** (`On Begin Play`, `On Update`, `On Custom Event`) start execution. White wires are
  **exec** (control flow); colored wires are **data**.
- **Variables** hold per-object state. Mark one **Exposed on Component** to override it per object in
  the Inspector.
- **Custom events** are typed messages on a shared bus: **Broadcast Event** sends one, **On Custom
  Event** handles it. **Call Interface Message** sends one to a single target object.
- References between assets use stable ids, so **renaming or moving a graph never breaks anything**.

---

## 1. Create the crystal graph from a template

1. Open **Panels → Visual Script Editor**.
2. In the **Template** dropdown choose **Pickup**, then click **Create from Template**.
   You get an `On Custom Event "TriggerEnter"` handler wired to a Print node.
3. Rename the event: in the side panel **Events** section, the template created a handler that listens
   for `TriggerEnter`. We want the crystal to announce itself, so add a new event:
   - Under **Events** click **Add Event**, name it `CrystalCollected`.
4. Drag from the `On Custom Event` node's **Then** pin into empty canvas — the search opens filtered to
   compatible nodes. Pick **Broadcast Event**, then set its **Event** to `CrystalCollected` using the
   Events section **Broadcast** button (it drops a pre-wired Broadcast node). Leave **Broadcast** = true
   so every listener hears it.
5. Save (Ctrl+S). Attach this graph to each crystal object (Inspector → **Visual Script**). When a
   crystal's trigger fires `TriggerEnter`, it broadcasts `CrystalCollected` to the whole scene.

> Tip: hover any node in the search to see its category, purity, and typed pins.

---

## 2. Create the door graph

1. In the Template dropdown choose **Door** and **Create from Template**. It gives you an
   `On Custom Event "Interact"` → **Timeline** → **Lerp (Vector3)** → **Set Position** chain — exactly
   the open animation we want. We'll change *when* it runs.
2. In the **Variables** section click **Add**, name it `Crystals`, type **Int**. Leave it un-exposed.
3. Change the trigger: click the `On Custom Event` node, and in its **eventName** set it to
   `CrystalCollected` (or delete it and drag a new **On Custom Event** from the Events section for
   `CrystalCollected`). Now the door reacts to crystals, not "Interact".
4. Count the crystals. Between the event and the Timeline, insert:
   - **Get Crystals** (Variables → **Get** button on the `Crystals` row) → **Add (Int)** with B = 1 →
     **Set Crystals** (Variables → **Set**). Wire the event's **Then** into **Set Crystals**' exec **In**.
5. Gate the open. Add a **Branch**:
   - **Get Crystals** → **Greater or Equal (Int)** (search "Greater") with B = 3 → Branch **Condition**.
   - Wire **Set Crystals**' **Then** → Branch **In**; Branch **True** → the **Timeline**'s **Play**.
6. The Timeline already drives **Lerp (Vector3)** (A = closed, B = open, e.g. `(0,4,0)`) into
   **Set Position**. Set A/B to your door's closed/open offsets.
7. Save. Attach the door graph to the door object.

You now have: crystals broadcast `CrystalCollected`; the door counts them and animates open on the
third. No hard-coded object names — it's all bus events and stable references.

---

## 3. Debug it

1. Press **Play**. The Visual Script Editor's **Debugger** section attaches automatically.
2. Right-click the Branch node → **Add Breakpoint** (or press F9). Collect crystals; execution pauses
   at the branch. Use **Continue** / **Step Node** and read the **Call Stack** and **Live Variables**.
3. Watch the **Event Trace** to see each `CrystalCollected` broadcast (sender → receiver).
4. If something's off, the **Runtime Errors** list links straight to the failing node. The
   **Validation** section flags problems (e.g. a disconnected exec pin) before you even Play.
5. Toggle **Compiled execution** off/on to compare per-node timings; the **Budgets** line shows live
   instance / task / instruction counts.

---

## 4. Verify with automated tests

In the panel's **Automated Tests** section click **Run Regression Suite** — this exercises
serialization, migration, hot reload, latent cancellation, and the instruction-budget guard headlessly
and prints pass/fail. Click **Run Graph Test** to smoke-test the current graph (four ticks, asserts no
runtime error). These same runners are callable from the command line / CI.

---

## 5. Package and run in the player

1. In the panel's **Asset Lifecycle** section click **Packaging Audit** — it lists every `.3dgvs`, its
   node/dependency counts, validation errors, and whether it compiles clean. Fix anything flagged.
2. Click **Who References This** before deleting any shared asset to see its dependents.
3. Build the packaged game (`cmake --build build --target RuntimePlayerApp`) and run the standalone
   **player**. Packaged builds strip editor-only data (comments, node layout) and run with diagnostics
   disabled, executing the compiled graph data directly.
4. Load your level in the player, collect three crystals, and watch the door open — same behavior as in
   the editor, driven by the same `.3dgvs` assets.

---

## Where to go next

- **Functions**: factor the "count and check" logic into a reusable function (side panel → Functions).
- **State machines**: give an enemy Idle/Patrol/Chase/Attack states (side panel → State Machines), each
  state pointing at a function; the current state shows live during Play.
- **Interfaces**: define a `IInteractable` interface with an `Interact` message and call it on any
  object with **Call Interface Message** — safe even if the target doesn't implement it.
- **Async**: use **Load Level (Async)** to stream the next trial and continue on Completed/Timed Out.
- **Node reference**: the panel's **Export Node Reference** writes `docs/VISUAL_SCRIPTING_NODES.md`
  listing every node and its pins.
