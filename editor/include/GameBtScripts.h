#pragma once

// Registers your custom behaviour-tree scripts (tasks / decorators / services).
// Called once at editor startup (from EditorApp). Write the script classes and add
// their registrations in editor/src/GameBtScripts.cpp -- that is the file to edit.
void RegisterGameBtScripts();
