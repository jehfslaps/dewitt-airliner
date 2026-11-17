// JAY: small platform layer (kept in its own file so Windows headers don't touch the UI code).
#include "YanceyPlatform.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cerrno>
#include <algorithm>

#ifdef _WIN32
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
# include <shellapi.h>
# include <commdlg.h>
# include <map>
#else
# include <sys/stat.h>
# include <sys/types.h>
# include <dirent.h>
#endif
#ifdef __APPLE__
# include <CoreFoundation/CoreFoundation.h>
#endif

namespace yancey {

bool reduceMotion()
{
#ifdef _WIN32
    BOOL anim = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0)) return anim == FALSE;
    return false;
#elif defined(__APPLE__)
    Boolean valid = false;
    const Boolean on = CFPreferencesGetAppBooleanValue(CFSTR("reduceMotion"), CFSTR("com.apple.universalaccess"), &valid);
    return valid && on;
#else
    const char* e = std::getenv("DEWITT_REDUCE_MOTION");   // for testing on Linux
    return e && e[0] == '1';
#endif
}

#ifdef _WIN32
static std::wstring toWide(const char* utf8)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], n);
    return w;
}
static std::string toUtf8(const wchar_t* w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
#endif

bool readFileBytes(const char* path, std::vector<uint8_t>& out)
{
    out.clear();
#ifdef _WIN32
    FILE* f = _wfopen(toWide(path).c_str(), L"rb");
#else
    FILE* f = std::fopen(path, "rb");
#endif
    if (f == nullptr) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 512L * 1024 * 1024) { std::fclose(f); return false; }
    out.resize((size_t)n);
    const size_t got = std::fread(out.data(), 1, (size_t)n, f);
    std::fclose(f);
    out.resize(got);
    return got > 0;
}

// ------------------------------------------------------------------------------------------------
// Global preferences: the last window size and dark mode, used by new instances

// the settings folder's parent: %APPDATA% on Windows, ~/Library/Application Support on Mac, ~/.config on Linux
static std::string configBase()
{
#ifdef _WIN32
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    if (appdata == nullptr) return std::string();
    return toUtf8(appdata);
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home == nullptr) return std::string();
    return std::string(home) + "/Library/Application Support";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) return std::string(xdg);
    const char* home = std::getenv("HOME");
    if (home == nullptr) return std::string();
    return std::string(home) + "/.config";
#endif
}
#ifdef _WIN32
static const char* const kSep = "\\";
#else
static const char* const kSep = "/";
#endif

// Dewitt lives in %APPDATA%\slapworx\Dewitt (it was %APPDATA%\slapworx\JAY when it was called JAY)
static std::string prefsDir()
{
    const std::string b = configBase();
    if (b.empty()) return b;
#if   defined(AIRLINER)
    return b + kSep + "slapworx" + kSep + "Airliner";
#else
    return b + kSep + "slapworx" + kSep + "Dewitt";
#endif
}

static bool dirExists(const std::string& path)
{
#ifdef _WIN32
    const DWORD a = GetFileAttributesW(toWide(path.c_str()).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

// make the settings folder and the folders above it
static void makePrefsDirs(const std::string& d)
{
    const std::string b = configBase();
    if (b.empty()) return;
    std::string cur = b;
    makeDir(cur.c_str());
    size_t pos = b.size();
    while (pos < d.size()) {
        size_t nx = d.find(kSep[0], pos + 1);
        if (nx == std::string::npos) nx = d.size();
        cur = d.substr(0, nx);
        makeDir(cur.c_str());
        pos = nx;
    }
}

static std::string prefsPath()
{
    const std::string d = prefsDir();
    if (d.empty()) return d;
#ifdef _WIN32
    return d + "\\settings.txt";
#else
    return d + "/settings.txt";
#endif
}

bool readPrefs(Prefs& p)
{
    const std::string path = prefsPath();
    if (path.empty()) return false;
    std::vector<uint8_t> bytes;
    if (!readFileBytes(path.c_str(), bytes)) {
#ifdef AIRLINER
        return false;       // Airliner starts fresh (it has no controller setup to bring over)
#endif
        // first run: start from an earlier controller setup on this computer, if there is one
        // (JAY's folder, then the private JAY builds' folder, then Howard's)
        const std::string b = configBase();
        if (b.empty()) return false;
        const std::string older[3] = { std::string("slapworx") + kSep + "JAY", "JAY", "Howard" };
        bool found = false;
        for (const std::string& o : older) {
            const std::string op = b + kSep + o + kSep + "settings.txt";
            if (op != path && readFileBytes(op.c_str(), bytes)) { found = true; break; }
        }
        if (!found) return false;
    }
    std::string s(bytes.begin(), bytes.end());
    size_t pos = 0;
    while (pos < s.size()) {
        size_t nl = s.find('\n', pos);
        if (nl == std::string::npos) nl = s.size();
        const std::string line = s.substr(pos, nl - pos);
        pos = nl + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "scale") p.scale = (float)std::atof(v.c_str());
        if (k == "uscale") p.uscale = (float)std::atof(v.c_str());
        else if (k == "dark") p.dark = std::atoi(v.c_str());
        else if (k == "kmode") p.kmode = std::atoi(v.c_str());
        else if (k == "notes") p.notes = std::atoi(v.c_str());
        else if (k == "norder") p.norder = std::atoi(v.c_str());
        else if (k == "welcomed3") p.welcomed = std::atoi(v.c_str());
        else if (k == "kspeed") p.kspeed = std::atoi(v.c_str());
        else if (k == "kjump") p.kjump = std::atoi(v.c_str());
        else if (k == "kcount") p.kcount = std::atoi(v.c_str());
        else if (k == "kgrid") p.kgrid = std::atoi(v.c_str());
        else if (k == "padfeel") p.padfeel = std::atoi(v.c_str());
        else if (k == "follow") p.follow = std::atoi(v.c_str());
        else if (k == "ktypes") p.ktypes = v;
        else if (k == "map3") { p.map = v; while (!p.map.empty() && (p.map.back() == '\r' || p.map.back() == ' ')) p.map.pop_back(); }
    }
    return true;
}

void writePrefs(const Prefs& update)
{
    Prefs cur;
    readPrefs(cur);
    if (update.scale > 0.f) cur.scale = update.scale;
    if (update.uscale > 0.f) cur.uscale = update.uscale;
    if (update.dark >= 0) cur.dark = update.dark;
    if (update.kmode >= 0) cur.kmode = update.kmode;
    if (update.notes >= -1) cur.notes = update.notes;
    if (update.norder >= 0) cur.norder = update.norder;
    if (update.welcomed >= 0) cur.welcomed = update.welcomed;
    if (update.kspeed >= 0) cur.kspeed = update.kspeed;
    if (update.kjump >= 0) cur.kjump = update.kjump;
    if (update.kcount >= 0) cur.kcount = update.kcount;
    if (update.kgrid >= 0) cur.kgrid = update.kgrid;
    if (update.padfeel >= 0) cur.padfeel = update.padfeel;
    if (update.follow >= 0) cur.follow = update.follow;
    if (!update.ktypes.empty()) cur.ktypes = update.ktypes;
    if (!update.map.empty()) cur.map = update.map;
    const std::string dir = prefsDir();
    if (dir.empty()) return;
    makePrefsDirs(dir);
#ifdef _WIN32
    FILE* f = _wfopen(toWide(prefsPath().c_str()).c_str(), L"wb");
#else
    FILE* f = std::fopen(prefsPath().c_str(), "wb");
#endif
    if (f == nullptr) return;
    std::fprintf(f, "scale=%.4f\ndark=%d\n", cur.scale > 0.f ? cur.scale : 1.f, cur.dark >= 0 ? cur.dark : 0);
    if (cur.uscale > 0.f) std::fprintf(f, "uscale=%.4f\n", cur.uscale);
    if (cur.kmode >= 0) std::fprintf(f, "kmode=%d\n", cur.kmode);
    if (cur.notes >= -1) std::fprintf(f, "notes=%d\n", cur.notes);
    if (cur.norder >= 0) std::fprintf(f, "norder=%d\n", cur.norder);
    if (cur.welcomed >= 0) std::fprintf(f, "welcomed3=%d\n", cur.welcomed);
    if (cur.kspeed >= 0) std::fprintf(f, "kspeed=%d\n", cur.kspeed);
    if (cur.kjump >= 0) std::fprintf(f, "kjump=%d\n", cur.kjump);
    if (cur.kcount >= 0) std::fprintf(f, "kcount=%d\n", cur.kcount);
    if (cur.kgrid >= 0) std::fprintf(f, "kgrid=%d\n", cur.kgrid);
    if (cur.padfeel >= 0) std::fprintf(f, "padfeel=%d\n", cur.padfeel);
    if (cur.follow >= 0) std::fprintf(f, "follow=%d\n", cur.follow);
    if (!cur.ktypes.empty()) std::fprintf(f, "ktypes=%s\n", cur.ktypes.c_str());
    if (!cur.map.empty()) std::fprintf(f, "map3=%s\n", cur.map.c_str());
    std::fclose(f);
}

std::string presetsDir(const char* kind)
{
    const std::string base = prefsDir();
    if (base.empty()) return std::string();
    makePrefsDirs(base);
    const std::string d = base + kSep + kind;
    // first time: bring over the sets and pad presets saved while Dewitt was called JAY
    if (!dirExists(d) && (std::strcmp(kind, "Sets") == 0 || std::strcmp(kind, "Pads") == 0)) {
        makeDir(d.c_str());
        const std::string od = configBase() + kSep + "slapworx" + kSep + "JAY" + kSep + kind;
        const char* ext = std::strcmp(kind, "Sets") == 0 ? ".yset" : ".ypad";
        std::vector<std::string> names;
        listFiles(od.c_str(), ext, names);
        for (const std::string& nm : names) copyFileTo((od + kSep + nm + ext).c_str(), (d + kSep + nm + ext).c_str());
    }
    makeDir(d.c_str());
    return d;
}

bool writeFileBytes(const char* path, const std::string& data)
{
#ifdef _WIN32
    FILE* f = _wfopen(toWide(path).c_str(), L"wb");
#else
    FILE* f = std::fopen(path, "wb");
#endif
    if (f == nullptr) return false;
    const size_t n = std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
    return n == data.size();
}

bool removeFilePath(const char* path)
{
#ifdef _WIN32
    return DeleteFileW(toWide(path).c_str()) != 0;
#else
    return std::remove(path) == 0;
#endif
}

void listFiles(const char* dir, const char* ext, std::vector<std::string>& names)
{
    names.clear();
    const size_t el = std::strlen(ext);
#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    const std::wstring pat = toWide((std::string(dir) + "\\*" + ext).c_str());
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string n = toUtf8(fd.cFileName);
        if (n.size() > el) names.push_back(n.substr(0, n.size() - el));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir);
    if (d == nullptr) return;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n.size() > el && n.compare(n.size() - el, el, ext) == 0) names.push_back(n.substr(0, n.size() - el));
    }
    closedir(d);
#endif
    std::sort(names.begin(), names.end());
}

uintptr_t grabKeyboard(uintptr_t nativeWindow)
{
#ifdef _WIN32
    HWND hwnd = (HWND)nativeWindow;
    if (hwnd == nullptr) return 0;
    return (uintptr_t)SetFocus(hwnd);
#else
    (void)nativeWindow; return 0;
#endif
}

void returnKeyboard(uintptr_t nativeWindow, uintptr_t previous)
{
#ifdef _WIN32
    HWND hwnd = (HWND)nativeWindow;
    if (hwnd == nullptr || GetFocus() != hwnd) return;
    HWND back = (HWND)previous;
    if (back == nullptr || !IsWindow(back)) back = GetAncestor(hwnd, GA_ROOT);
    if (back != nullptr && back != hwnd) SetFocus(back);
#else
    (void)nativeWindow; (void)previous;
#endif
}

std::string saveNameDialog(uintptr_t owner, const char* dir, const char* suggested, const char* ext, const char* title)
{
#ifdef _WIN32
    wchar_t file[MAX_PATH] = {};
    const std::wstring sug = toWide(suggested);
    wcsncpy(file, sug.c_str(), MAX_PATH - 1);
    const std::wstring wdir = toWide(dir), wext = toWide(ext + (ext[0] == '.' ? 1 : 0)), wtitle = toWide(title);
    std::wstring filter = L"Dewitt preset";
    filter.push_back(L'\0'); filter += L"*." + wext; filter.push_back(L'\0'); filter.push_back(L'\0');
    OPENFILENAMEW ofn;
    std::memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetAncestor((HWND)owner, GA_ROOT);
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = wdir.c_str();
    ofn.lpstrTitle = wtitle.c_str();
    ofn.lpstrDefExt = wext.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return std::string();
    std::string path = toUtf8(file);
    size_t slash = path.find_last_of("\\/");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name;
#else
    (void)owner; (void)dir; (void)suggested; (void)ext; (void)title;
    return std::string();
#endif
}

bool fileExists(const char* path)
{
#ifdef _WIN32
    const DWORD a = GetFileAttributesW(toWide(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st; return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool copyFileTo(const char* from, const char* to)
{
#ifdef _WIN32
    return CopyFileW(toWide(from).c_str(), toWide(to).c_str(), FALSE) != 0;
#else
    std::vector<uint8_t> bytes;
    if (!readFileBytes(from, bytes)) return false;
    return writeFileBytes(to, std::string(bytes.begin(), bytes.end()));
#endif
}

bool makeDir(const char* path)
{
#ifdef _WIN32
    return CreateDirectoryW(toWide(path).c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

void clearPrefs()
{
    // leave a near-empty file rather than none, so a reset doesn't pick up an older setup instead
    const std::string path = prefsPath();
    if (path.empty()) return;
    writeFileBytes(path.c_str(), "reset=1\n");
}

// ------------------------------------------------------------------------------------------------
// File drop (Windows): accept audio files dragged from Explorer onto the window

#ifdef _WIN32
struct DropHook { WNDPROC prev; DropCallback cb; void* ctx; };
static std::map<HWND, DropHook>& dropHooks() { static std::map<HWND, DropHook> m; return m; }

static LRESULT CALLBACK dropWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    std::map<HWND, DropHook>& hooks = dropHooks();
    std::map<HWND, DropHook>::iterator it = hooks.find(hwnd);
    if (it == hooks.end()) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_DROPFILES) {
        HDROP drop = (HDROP)wp;
        POINT pt; DragQueryPoint(drop, &pt);
        RECT rc; GetClientRect(hwnd, &rc);
        const float cw = (float)std::max(1L, rc.right - rc.left), ch = (float)std::max(1L, rc.bottom - rc.top);
        const UINT len = DragQueryFileW(drop, 0, nullptr, 0);
        if (len > 0) {
            std::wstring w(len + 1, L'\0');
            DragQueryFileW(drop, 0, &w[0], len + 1);
            w.resize(len);
            const std::string path = toUtf8(w.c_str());
            it->second.cb(it->second.ctx, path.c_str(), (float)pt.x / cw, (float)pt.y / ch);
        }
        DragFinish(drop);
        return 0;
    }
    return CallWindowProcW(it->second.prev, hwnd, msg, wp, lp);
}
#endif

#ifndef __APPLE__   // the Mac version is in YanceyDropMac.mm
bool installFileDrop(uintptr_t nativeWindow, DropCallback cb, void* ctx)
{
#ifdef _WIN32
    HWND hwnd = (HWND)nativeWindow;
    if (hwnd == nullptr || dropHooks().count(hwnd)) return false;
    DropHook h;
    h.cb = cb; h.ctx = ctx;
    h.prev = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    dropHooks()[hwnd] = h;
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)dropWndProc);
    DragAcceptFiles(hwnd, TRUE);
    return true;
#else
    (void)nativeWindow; (void)cb; (void)ctx;
    return false;
#endif
}

void removeFileDrop(uintptr_t nativeWindow)
{
#ifdef _WIN32
    HWND hwnd = (HWND)nativeWindow;
    std::map<HWND, DropHook>::iterator it = dropHooks().find(hwnd);
    if (it == dropHooks().end()) return;
    DragAcceptFiles(hwnd, FALSE);
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)it->second.prev);
    dropHooks().erase(it);
#else
    (void)nativeWindow;
#endif
}
#endif

} // namespace yancey
