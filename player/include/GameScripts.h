#pragma once

// Register your game's native scripts here so the standalone player can run them.
//
// A scene stores scripts by CLASS NAME (a NativeScriptComponent). At runtime the
// player looks that name up in engine::ScriptRegistry and instantiates the class,
// so every class a scene uses must be registered before the simulation starts.
// The player calls RegisterGameScripts() once in OnInit (see RuntimePlayerApp).
//
// This is the player-side twin of the editor's editor/src/GameBtScripts.cpp: add
// your script source files to player/CMakeLists.txt, #include their headers in
// GameScripts.cpp, and add a Register(...) line for each. See GameScripts.cpp.
void RegisterGameScripts();
