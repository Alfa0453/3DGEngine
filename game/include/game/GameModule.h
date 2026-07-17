#pragma once

// Registers every game script -- gameplay (engine::Script) and behaviour-tree
// (engine::ai::BtScript) -- with the engine's global registries. Both the editor's
// Play mode and the standalone player call this once at startup, so a script written
// in the game module runs in both.
//
// This is the single place to register game scripts. See game/src/GameModule.cpp.
void RegisterGameModule();
