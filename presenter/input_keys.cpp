/*
 * Linux/X11 to Android key-code translation tables.
 *
 * This intentionally covers the keys a desktop host actually produces.  It is
 * not a replacement for a full key layout: function keys, keypad and media
 * keys can be added as fixtures need them.
 */

#include "input_keys.h"

#include <android/input.h>
#include <android/keycodes.h>

namespace droidlet {
namespace {

constexpr uint32_t kXkBackSpace = 0xff08;
constexpr uint32_t kXkTab = 0xff09;
constexpr uint32_t kXkReturn = 0xff0d;
constexpr uint32_t kXkEscape = 0xff1b;
constexpr uint32_t kXkHome = 0xff50;
constexpr uint32_t kXkLeft = 0xff51;
constexpr uint32_t kXkUp = 0xff52;
constexpr uint32_t kXkRight = 0xff53;
constexpr uint32_t kXkDown = 0xff54;
constexpr uint32_t kXkPageUp = 0xff55;
constexpr uint32_t kXkPageDown = 0xff56;
constexpr uint32_t kXkEnd = 0xff57;
constexpr uint32_t kXkDelete = 0xffff;
constexpr uint32_t kXkShiftL = 0xffe1;
constexpr uint32_t kXkShiftR = 0xffe2;
constexpr uint32_t kXkControlL = 0xffe3;
constexpr uint32_t kXkControlR = 0xffe4;
constexpr uint32_t kXkAltL = 0xffe9;
constexpr uint32_t kXkAltR = 0xffea;
constexpr uint32_t kXkSuperL = 0xffeb;
constexpr uint32_t kXkSuperR = 0xffec;
constexpr uint32_t kXkKPEnter = 0xff8d;
constexpr uint32_t kXkXF86Back = 0x1008ff1c;
constexpr uint32_t kXkXF86HomePage = 0x1008ff18;
constexpr uint32_t kXkXF86Search = 0x1008ff1b;

int asciiKeyCode(uint32_t c, uint32_t* extraMeta) {
    if (c >= 'a' && c <= 'z') {
        return AKEYCODE_A + static_cast<int>(c - 'a');
    }
    if (c >= 'A' && c <= 'Z') {
        *extraMeta |= kMetaShiftOn;
        return AKEYCODE_A + static_cast<int>(c - 'A');
    }
    if (c >= '0' && c <= '9') {
        return AKEYCODE_0 + static_cast<int>(c - '0');
    }
    switch (c) {
        case ' ': return AKEYCODE_SPACE;
        case '\n':
        case '\r': return AKEYCODE_ENTER;
        case '\t': return AKEYCODE_TAB;
        case '\b': return AKEYCODE_DEL;
        case '!': *extraMeta |= kMetaShiftOn; return AKEYCODE_1;
        case '@': *extraMeta |= kMetaShiftOn; return AKEYCODE_2;
        case '#': *extraMeta |= kMetaShiftOn; return AKEYCODE_3;
        case '$': *extraMeta |= kMetaShiftOn; return AKEYCODE_4;
        case '%': *extraMeta |= kMetaShiftOn; return AKEYCODE_5;
        case '^': *extraMeta |= kMetaShiftOn; return AKEYCODE_6;
        case '&': *extraMeta |= kMetaShiftOn; return AKEYCODE_7;
        case '*': *extraMeta |= kMetaShiftOn; return AKEYCODE_8;
        case '(': *extraMeta |= kMetaShiftOn; return AKEYCODE_9;
        case ')': *extraMeta |= kMetaShiftOn; return AKEYCODE_0;
        case '-': return AKEYCODE_MINUS;
        case '_': *extraMeta |= kMetaShiftOn; return AKEYCODE_MINUS;
        case '=': return AKEYCODE_EQUALS;
        case '+': *extraMeta |= kMetaShiftOn; return AKEYCODE_EQUALS;
        case '[': return AKEYCODE_LEFT_BRACKET;
        case '{': *extraMeta |= kMetaShiftOn; return AKEYCODE_LEFT_BRACKET;
        case ']': return AKEYCODE_RIGHT_BRACKET;
        case '}': *extraMeta |= kMetaShiftOn; return AKEYCODE_RIGHT_BRACKET;
        case '\\': return AKEYCODE_BACKSLASH;
        case '|': *extraMeta |= kMetaShiftOn; return AKEYCODE_BACKSLASH;
        case ';': return AKEYCODE_SEMICOLON;
        case ':': *extraMeta |= kMetaShiftOn; return AKEYCODE_SEMICOLON;
        case '\'': return AKEYCODE_APOSTROPHE;
        case '"': *extraMeta |= kMetaShiftOn; return AKEYCODE_APOSTROPHE;
        case '`': return AKEYCODE_GRAVE;
        case '~': *extraMeta |= kMetaShiftOn; return AKEYCODE_GRAVE;
        case ',': return AKEYCODE_COMMA;
        case '<': *extraMeta |= kMetaShiftOn; return AKEYCODE_COMMA;
        case '.': return AKEYCODE_PERIOD;
        case '>': *extraMeta |= kMetaShiftOn; return AKEYCODE_PERIOD;
        case '/': return AKEYCODE_SLASH;
        case '?': *extraMeta |= kMetaShiftOn; return AKEYCODE_SLASH;
        default: return 0;
    }
}

}  // namespace

int androidKeyCodeFromEvdev(int code) {
    // evdev key codes describe physical key positions.  Android key codes
    // also describe physical positions, but their numeric values are not in
    // the same order as evdev's, so map each key explicitly.  Using
    // AKEYCODE_A + offset (or Q/Z + offset) shifts whole rows and produces
    // the wrong characters ("help" -> "fsiz").
    switch (code) {
        case 1: return AKEYCODE_ESCAPE;
        case 2: return AKEYCODE_1;
        case 3: return AKEYCODE_2;
        case 4: return AKEYCODE_3;
        case 5: return AKEYCODE_4;
        case 6: return AKEYCODE_5;
        case 7: return AKEYCODE_6;
        case 8: return AKEYCODE_7;
        case 9: return AKEYCODE_8;
        case 10: return AKEYCODE_9;
        case 11: return AKEYCODE_0;
        case 12: return AKEYCODE_MINUS;
        case 13: return AKEYCODE_EQUALS;
        case 14: return AKEYCODE_DEL;
        case 15: return AKEYCODE_TAB;

        // Top row: QWERTYUIOP
        case 16: return AKEYCODE_Q;
        case 17: return AKEYCODE_W;
        case 18: return AKEYCODE_E;
        case 19: return AKEYCODE_R;
        case 20: return AKEYCODE_T;
        case 21: return AKEYCODE_Y;
        case 22: return AKEYCODE_U;
        case 23: return AKEYCODE_I;
        case 24: return AKEYCODE_O;
        case 25: return AKEYCODE_P;
        case 26: return AKEYCODE_LEFT_BRACKET;
        case 27: return AKEYCODE_RIGHT_BRACKET;
        case 28: return AKEYCODE_ENTER;
        case 29: return AKEYCODE_CTRL_LEFT;

        // Home row: ASDFGHJKL
        case 30: return AKEYCODE_A;
        case 31: return AKEYCODE_S;
        case 32: return AKEYCODE_D;
        case 33: return AKEYCODE_F;
        case 34: return AKEYCODE_G;
        case 35: return AKEYCODE_H;
        case 36: return AKEYCODE_J;
        case 37: return AKEYCODE_K;
        case 38: return AKEYCODE_L;
        case 39: return AKEYCODE_SEMICOLON;
        case 40: return AKEYCODE_APOSTROPHE;
        case 41: return AKEYCODE_GRAVE;
        case 42: return AKEYCODE_SHIFT_LEFT;
        case 43: return AKEYCODE_BACKSLASH;

        // Bottom row: ZXCVBNM
        case 44: return AKEYCODE_Z;
        case 45: return AKEYCODE_X;
        case 46: return AKEYCODE_C;
        case 47: return AKEYCODE_V;
        case 48: return AKEYCODE_B;
        case 49: return AKEYCODE_N;
        case 50: return AKEYCODE_M;
        case 51: return AKEYCODE_COMMA;
        case 52: return AKEYCODE_PERIOD;
        case 53: return AKEYCODE_SLASH;
        case 54: return AKEYCODE_SHIFT_RIGHT;
        case 55: return AKEYCODE_NUMPAD_MULTIPLY;
        case 56: return AKEYCODE_ALT_LEFT;
        case 57: return AKEYCODE_SPACE;
        case 58: return AKEYCODE_CAPS_LOCK;

        case 59: return AKEYCODE_F1;
        case 60: return AKEYCODE_F2;
        case 61: return AKEYCODE_F3;
        case 62: return AKEYCODE_F4;
        case 63: return AKEYCODE_F5;
        case 64: return AKEYCODE_F6;
        case 65: return AKEYCODE_F7;
        case 66: return AKEYCODE_F8;
        case 67: return AKEYCODE_F9;
        case 68: return AKEYCODE_F10;
        case 69: return AKEYCODE_NUM_LOCK;
        case 70: return AKEYCODE_SCROLL_LOCK;

        case 71: return AKEYCODE_NUMPAD_7;
        case 72: return AKEYCODE_NUMPAD_8;
        case 73: return AKEYCODE_NUMPAD_9;
        case 74: return AKEYCODE_NUMPAD_SUBTRACT;
        case 75: return AKEYCODE_NUMPAD_4;
        case 76: return AKEYCODE_NUMPAD_5;
        case 77: return AKEYCODE_NUMPAD_6;
        case 78: return AKEYCODE_NUMPAD_ADD;
        case 79: return AKEYCODE_NUMPAD_1;
        case 80: return AKEYCODE_NUMPAD_2;
        case 81: return AKEYCODE_NUMPAD_3;
        case 82: return AKEYCODE_NUMPAD_0;
        case 83: return AKEYCODE_NUMPAD_DOT;

        case 85: return AKEYCODE_ZENKAKU_HANKAKU;
        case 86: return AKEYCODE_BACKSLASH;
        case 87: return AKEYCODE_F11;
        case 88: return AKEYCODE_F12;
        case 89: return AKEYCODE_RO;
        case 90: return AKEYCODE_KATAKANA_HIRAGANA;
        case 91: return AKEYCODE_KATAKANA_HIRAGANA;
        case 92: return AKEYCODE_HENKAN;
        case 93: return AKEYCODE_KATAKANA_HIRAGANA;
        case 94: return AKEYCODE_MUHENKAN;
        case 95: return AKEYCODE_NUMPAD_COMMA;

        case 96: return AKEYCODE_NUMPAD_ENTER;
        case 97: return AKEYCODE_CTRL_RIGHT;
        case 98: return AKEYCODE_NUMPAD_DIVIDE;
        case 99: return AKEYCODE_SYSRQ;
        case 100: return AKEYCODE_ALT_RIGHT;
        case 101: return AKEYCODE_ENTER;
        case 102: return AKEYCODE_MOVE_HOME;
        case 103: return AKEYCODE_DPAD_UP;
        case 104: return AKEYCODE_PAGE_UP;
        case 105: return AKEYCODE_DPAD_LEFT;
        case 106: return AKEYCODE_DPAD_RIGHT;
        case 107: return AKEYCODE_MOVE_END;
        case 108: return AKEYCODE_DPAD_DOWN;
        case 109: return AKEYCODE_PAGE_DOWN;
        case 110: return AKEYCODE_INSERT;
        case 111: return AKEYCODE_FORWARD_DEL;
        case 113: return AKEYCODE_VOLUME_MUTE;
        case 114: return AKEYCODE_VOLUME_DOWN;
        case 115: return AKEYCODE_VOLUME_UP;
        case 116: return AKEYCODE_POWER;
        case 117: return AKEYCODE_NUMPAD_EQUALS;
        case 119: return AKEYCODE_BREAK;
        case 121: return AKEYCODE_NUMPAD_COMMA;
        case 124: return AKEYCODE_YEN;
        case 125: return AKEYCODE_META_LEFT;
        case 126: return AKEYCODE_META_RIGHT;
        case 127: return AKEYCODE_MENU;
        case 158: return AKEYCODE_BACK;
        case 172: return AKEYCODE_HOME;
        case 217: return AKEYCODE_SEARCH;
        default: return 0;
    }
}

int androidKeyCodeFromKeysym(uint32_t keysym, uint32_t* extraMeta) {
    if (keysym >= 0x20 && keysym <= 0x7e) {
        return asciiKeyCode(keysym, extraMeta);
    }
    switch (keysym) {
        case kXkBackSpace: return AKEYCODE_DEL;
        case kXkTab: return AKEYCODE_TAB;
        case kXkReturn: return AKEYCODE_ENTER;
        case kXkKPEnter: return AKEYCODE_ENTER;
        case kXkEscape: return AKEYCODE_ESCAPE;
        case kXkHome: return AKEYCODE_MOVE_HOME;
        case kXkLeft: return AKEYCODE_DPAD_LEFT;
        case kXkUp: return AKEYCODE_DPAD_UP;
        case kXkRight: return AKEYCODE_DPAD_RIGHT;
        case kXkDown: return AKEYCODE_DPAD_DOWN;
        case kXkPageUp: return AKEYCODE_PAGE_UP;
        case kXkPageDown: return AKEYCODE_PAGE_DOWN;
        case kXkEnd: return AKEYCODE_MOVE_END;
        case kXkDelete: return AKEYCODE_FORWARD_DEL;
        case kXkShiftL: return AKEYCODE_SHIFT_LEFT;
        case kXkShiftR: return AKEYCODE_SHIFT_RIGHT;
        case kXkControlL: return AKEYCODE_CTRL_LEFT;
        case kXkControlR: return AKEYCODE_CTRL_RIGHT;
        case kXkAltL: return AKEYCODE_ALT_LEFT;
        case kXkAltR: return AKEYCODE_ALT_RIGHT;
        case kXkSuperL: return AKEYCODE_META_LEFT;
        case kXkSuperR: return AKEYCODE_META_RIGHT;
        case kXkXF86Back: return AKEYCODE_BACK;
        case kXkXF86HomePage: return AKEYCODE_HOME;
        case kXkXF86Search: return AKEYCODE_SEARCH;
        default: return 0;
    }
}

}  // namespace droidlet
