# Dialogue Editor

The Dialogue Editor creates engine-owned `.3dgdialogue` conversation assets for editor Play and packaged games.

## Authoring a conversation

1. Open **Panels > Level Design > Dialogue Editor** and choose **New**.
2. Add speakers. Each speaker has a stable ID, display name, localization key, and optional engine texture portrait.
3. Add dialogue nodes. Set the speaker, text, localization key, optional voice clip, camera hook, and enter/exit events.
4. Add choices and select the next node. Choose **End Conversation** for a terminal response.
5. Add flag conditions to nodes or individual choices to control which branches are available.
6. Mark the first node with **Set as Entry**, test the flow in the live debugger, then save.
7. Select an NPC or conversation object and choose **Assign to Selected**. This binding is saved with the scene and exported into packaged levels.

Portrait and voice fields search engine-owned texture and audio assets. The flow preview shows branch links and highlights the selected node. Validation catches missing speakers, entry nodes, and branch targets before saving.

## Native C++ scripts

```cpp
if (StartDialogue(wizardEntity)) {
    Log(DialogueSpeaker() + ": " + DialogueText());
}

ChooseDialogue(0);
SetDialogueFlag("HasWizardKey", true);
CancelDialogue();
```

`StartDialogue()` also accepts a `.3dgdialogue` asset path. Use `ContinueDialogue()` when a line has exactly one available response. `SaveDialogueState()` and `LoadDialogueState()` persist an active conversation and its flags.

## Lua scripts

```lua
function OnInteract(wizard)
    if Engine.StartDialogue(wizard) then
        Engine.Log(Engine.DialogueSpeaker() .. ": " .. Engine.DialogueText())
    end
end

function SelectResponse(index)
    Engine.ChooseDialogue(index)
end
```

Runtime events report conversation start/end, entered and exited nodes, chosen responses, authored script events, and camera hooks. Voice playback, portrait HUD presentation, localized string lookup, camera execution, and quest-event reactions can consume these events without coupling them to the asset format.
