# Quest Editor

Open **Panels > Level Design > Quest Editor** to create reusable `.3dgquest`
assets. A quest contains start conditions, ordered or independent objectives,
optional objectives, checkpoint markers, dialogue triggers, and rewards.

1. Set the internal name, player-facing title, and description.
2. Add start conditions when a world flag must be set before the quest begins.
3. Add objectives. Use **Required Objective** for ordering and **Required Flag**
   for world-state gates. Checkpoint objectives emit a checkpoint event.
4. Add score, item, ability, or script-event rewards.
5. Use the Live Quest Debugger to start, advance, fail, and reset the quest.
6. Save, select the player or quest-manager object, and choose **Grant to Selected**.

Native C++ scripts use `SetQuestFlag`, `StartQuest`, `AdvanceQuest`, `FailQuest`,
`QuestState`, and `QuestProgress`. Lua exposes the same functions under `Engine`,
for example `Engine.AdvanceQuest("WizardTrial", "CollectCrystals", 1)`.

Use `SaveQuestState` and `LoadQuestState` to store or restore quest progress in a
game-specific save record. Quest bindings and stable asset dependencies persist in
editor scenes and packaged runtime scenes.
