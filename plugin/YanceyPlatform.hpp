// Yancey: platform helpers (file reading, global preferences shared by every instance).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace yancey {

// read a whole file (UTF-8 path, works with non-English characters on Windows)
bool readFileBytes(const char* path, std::vector<uint8_t>& out);

// Global preferences shared by every Yancey on this computer; unset values are -1 / 0 / empty.
// The controller setup lives here too, so every project already knows your controller.
struct Prefs {
    float scale = 0.f;       // window width / design width, in pixels (older builds read this one)
    float uscale = 0.f;      // the user's chosen size on top of the screen's scale factor (1 = default)
    int dark = -1;
    int kmode = -1;          // knob encoding (KnobMode)
    int notes = -2;          // note map start (-1 off, -2 unset)
    int norder = -1;         // note map row order (0 top row first, 1 bottom row first)
    int welcomed = -1;       // 1 once the welcome card has been finished or dismissed
    int kspeed = -1;         // endless knob speed: 0 slow, 1 normal, 2 fast
    int kjump = -1;          // normal knobs: 1 jump straight to the knob, 0 (or unset) wait for pickup
    int kcount = -1;         // knobs on the controller: 0 (or unset) count the linked ones, 1-5 set by hand
    int kgrid = -1;          // knobs on screen: 0 top row 1-4, 1 bottom row 1-4
    int padfeel = -1;        // 0 velocity + pressure (hardware-sampler style), 1 velocity only, 2 full on any touch
    std::string ktypes;      // each knob's learned type (KnobMode per knob), comma separated
    std::string map;         // MIDI map (16 pads + 16 knob jobs), comma separated
};
bool readPrefs(Prefs& p);
void writePrefs(const Prefs& update);   // merges with what is already saved
void clearPrefs();                      // factory reset: forget everything saved on this computer

// presets: <settings folder>/Sets and /Pads; names are file names without the extension
std::string presetsDir(const char* kind);                       // creates it if needed; "" if unavailable
bool writeFileBytes(const char* path, const std::string& data);
bool removeFilePath(const char* path);
bool fileExists(const char* path);
bool copyFileTo(const char* from, const char* to);
bool makeDir(const char* path);
void listFiles(const char* dir, const char* ext, std::vector<std::string>& names);   // sorted, names only

// keyboard: take it for a text box (returns what had it), give it back afterwards (Windows; no-op elsewhere)
uintptr_t grabKeyboard(uintptr_t nativeWindow);
void returnKeyboard(uintptr_t nativeWindow, uintptr_t previous);
// the standard Save dialog, starting in dir with a suggested name; returns the chosen name (no folder, no
// extension) or "" if cancelled or unavailable
std::string saveNameDialog(uintptr_t owner, const char* dir, const char* suggested, const char* ext, const char* title);

// accept files dropped onto the plug-in window (Windows only); x/y are window pixels
typedef void (*DropCallback)(void* ctx, const char* utf8Path, int x, int y);
bool installFileDrop(uintptr_t nativeWindow, DropCallback cb, void* ctx);
void removeFileDrop(uintptr_t nativeWindow);

} // namespace yancey
