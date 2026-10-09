// sc-offline's keys through sco-core's hotkey table: see hotkeys.h.
#include "hotkeys.h"
#include "common.h"
#include "menu.h"
#include "sco/ui.h"
#include <cctype>
#include <string>

const char* const kReservedChords[] = {
    "m",           // the menu (menu.cpp)
    "r",           // build mode: rotate (build.cpp)
    "lbracket",    // build mode: reach closer
    "rbracket",    // build mode: reach farther
    "backspace",   // build mode: undo
};
const size_t kReservedChordCount = sizeof(kReservedChords) / sizeof(kReservedChords[0]);

namespace {

// Every key of sco.ui's chord grammar, by its normalized name.
struct Key { int vk; const char* name; };
constexpr Key kNamed[] = {
    { VK_NUMPAD0, "num0" }, { VK_NUMPAD1, "num1" }, { VK_NUMPAD2, "num2" }, { VK_NUMPAD3, "num3" }, { VK_NUMPAD4, "num4" },
    { VK_NUMPAD5, "num5" }, { VK_NUMPAD6, "num6" }, { VK_NUMPAD7, "num7" }, { VK_NUMPAD8, "num8" }, { VK_NUMPAD9, "num9" },
    { VK_ESCAPE, "escape" }, { VK_RETURN, "enter" }, { VK_TAB, "tab" }, { VK_SPACE, "space" }, { VK_BACK, "backspace" },
    { VK_INSERT, "insert" }, { VK_DELETE, "delete" }, { VK_HOME, "home" }, { VK_END, "end" },
    { VK_PRIOR, "pageup" }, { VK_NEXT, "pagedown" }, { VK_UP, "up" }, { VK_DOWN, "down" }, { VK_LEFT, "left" }, { VK_RIGHT, "right" },
    { VK_OEM_MINUS, "minus" }, { VK_OEM_PLUS, "equals" }, { VK_OEM_COMMA, "comma" }, { VK_OEM_PERIOD, "period" },
    { VK_OEM_2, "slash" }, { VK_OEM_5, "backslash" }, { VK_OEM_1, "semicolon" }, { VK_OEM_7, "apostrophe" },
    { VK_OEM_3, "grave" }, { VK_OEM_4, "lbracket" }, { VK_OEM_6, "rbracket" },
};
constexpr int kLetters = 26, kDigits = 10, kFKeys = 24;
constexpr int kKeyCount = kLetters + kDigits + kFKeys + static_cast<int>(sizeof(kNamed) / sizeof(kNamed[0]));
constexpr DWORD kPollMs = 15;

// The i-th key: its virtual-key code and normalized name.
int KeyAt(int i, char* name, size_t n) {
    if (i < kLetters) { snprintf(name, n, "%c", 'a' + i); return 'A' + i; }
    i -= kLetters;
    if (i < kDigits) { snprintf(name, n, "%c", '0' + i); return '0' + i; }
    i -= kDigits;
    if (i < kFKeys) { snprintf(name, n, "f%d", i + 1); return VK_F1 + i; }
    i -= kFKeys;
    snprintf(name, n, "%s", kNamed[i].name);
    return kNamed[i].vk;
}

bool Held(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool g_down[kKeyCount] = {};
char g_current[48] = "";   // Hotkeys_Current while a command runs

// "ctrl+alt+9" -> "Ctrl+Alt+9", "f7" -> "F7".
void Label(const std::string& chord, char* out, size_t n) {
    size_t j = 0;
    bool start = true;
    for (char c : chord) {
        if (j + 1 >= n) break;
        out[j++] = start ? static_cast<char>(toupper(static_cast<unsigned char>(c))) : c;
        start = c == '+';
    }
    out[j] = 0;
}

void Dispatch(const std::string& chord) {
    std::string command;
    for (const sco::ui::HotkeyInfo& h : sco::ui::Hotkeys())
        if (h.chord == chord) { command = h.command; break; }
    if (command.empty()) return;   // unbound or reserved: nothing to run
    Label(chord, g_current, sizeof(g_current));
    std::string reply;
    const sco::Result r = sco::ui::Dispatch(chord.c_str(), &reply);
    Log("[hotkey] %s -> %s: %s%s%s%s", g_current, command.c_str(), sco::ResultName(r),
        reply.empty() ? "" : " \"", reply.c_str(), reply.empty() ? "" : "\"");
    g_current[0] = 0;
}

}  // namespace

void Hotkeys_Poll() {
    static DWORD last = 0;
    const DWORD now = GetTickCount();
    if (now - last < kPollMs) return;
    last = now;

    const bool listening = GameHasFocus() && !Menu_Typing();
    std::string mods;
    if (Held(VK_CONTROL)) mods += "ctrl+";
    if (Held(VK_MENU)) mods += "alt+";
    if (Held(VK_SHIFT)) mods += "shift+";
    for (int i = 0; i < kKeyCount; ++i) {
        char name[16];
        const bool down = Held(KeyAt(i, name, sizeof(name)));
        const bool pressed = down && !g_down[i];
        g_down[i] = down;
        if (pressed && listening) Dispatch(mods + name);
    }
}

const char* Hotkeys_Current() { return g_current[0] ? g_current : nullptr; }
