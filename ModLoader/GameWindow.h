#pragma once
#include <windows.h>

// Returns the game's main window — the largest visible, unowned, non-console
// window in the current process. Returns nullptr until the game window exists.
HWND FindGameWindow();
