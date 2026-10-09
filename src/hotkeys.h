#pragma once
#include <cstddef>
// sc-offline's keys through sco-core's hotkey table (sco.ui, sco/ui.h).
//
// The product reserves the keys it handles itself (kReservedChords, handed to sco::app::Start);
// every other key press in the game or the menu becomes a chord ("ctrl+alt+9") and goes to
// sco::ui::Dispatch, which runs the command bound to it. The built-ins bind sc-offline's own
// command keys that way (F6 build.toggle, F7 teleport.save, F8 teleport.go); a plugin binds any
// free chord.

// M opens the menu (menu.cpp); R, [ ], Backspace are build mode's keys while it is on (build.cpp).
extern const char* const kReservedChords[];
extern const size_t kReservedChordCount;

// Game thread, on every pass of the message hook once the host kit has started: reads the keys
// and dispatches each new chord. Quiet while the game isn't in front or the menu is typing.
void Hotkeys_Poll();

// While a hotkey's command runs (on the game thread, inside Hotkeys_Poll): the key as players read
// it ("F7", "Ctrl+Alt+9"); otherwise nullptr. Lets a command name its key in mod.log.
const char* Hotkeys_Current();
