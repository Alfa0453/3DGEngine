#include "GameScripts.h"

#include <game/GameModule.h>

// The player registers scripts through the shared GAME MODULE (game/), the same
// library the editor links -- so every script you add there runs both in the
// editor's Play mode and in this standalone player. Add scripts in
// game/src/GameModule.cpp, not here.
//
// (This thin forwarder is kept so the player's OnInit call site stays stable, and
// so you have a spot for any player-only scripts if you ever need one.)
void RegisterGameScripts() {
    RegisterGameModule();
}
