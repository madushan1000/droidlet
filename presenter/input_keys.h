/*
 * Host key-code translation.
 *
 * Wayland and X11 report different key identifiers.  droidlet needs Android
 * KeyEvent key codes for both, so keep the tables in one place rather than
 * duplicating them in each presenter.
 */

#pragma once

#include <cstdint>

namespace droidlet {

// Translates a Linux evdev key code (as delivered by wl_keyboard.key) to an
// Android KeyEvent key code.  Returns 0 (KEYCODE_UNKNOWN) when unmapped.
int androidKeyCodeFromEvdev(int evdevCode);

// Translates an X11 keysym to an Android KeyEvent key code.  extraMeta is
// OR'ed with any modifier the key itself implies (for example '!' implies
// KEYCODE_1 with Shift).  Returns 0 when unmapped.
int androidKeyCodeFromKeysym(uint32_t keysym, uint32_t* extraMeta);

// Modifier bits shared with android/input.h.  Kept here so presenters do not
// have to include the whole native input header just for these.
constexpr uint32_t kMetaShiftOn = 0x01;
constexpr uint32_t kMetaAltOn = 0x02;
constexpr uint32_t kMetaCtrlOn = 0x1000;
constexpr uint32_t kMetaMetaOn = 0x10000;

}  // namespace droidlet
