// Dewitt test host: the few things that differ between Windows and Mac (loading a CLAP, folders).
#pragma once
#include <string>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define TEST_SEP "\\"
#define SOLO_EXT ".dll"   // the standalone test plug-ins
static inline void testMkdir(const std::string& p) { _mkdir(p.c_str()); }
static inline std::string clapBinary(const char* path) { return path; }
static inline void* testOpenLib(const char* path) { return (void*)LoadLibraryA(path); }
static inline void* testSym(void* h, const char* s) { return (void*)GetProcAddress((HMODULE)h, s); }
static inline std::string testConfigBase() { const char* a = std::getenv("APPDATA"); return a ? a : ""; }
#else
#include <dlfcn.h>
#include <sys/stat.h>
#define TEST_SEP "/"
#define SOLO_EXT ".clap"
static inline void testMkdir(const std::string& p) { mkdir(p.c_str(), 0755); }
// a Mac .clap is a bundle: the binary is Contents/MacOS/<name>
static inline std::string clapBinary(const char* path)
{
    struct stat st;
    std::string p(path);
    while (!p.empty() && p.back() == '/') p.pop_back();
    if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        std::string base = p.substr(p.find_last_of('/') + 1);
        const size_t dot = base.find_last_of('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        return p + "/Contents/MacOS/" + base;
    }
    return p;
}
static inline void* testOpenLib(const char* path) { return dlopen(clapBinary(path).c_str(), RTLD_NOW | RTLD_LOCAL); }
static inline void* testSym(void* h, const char* s) { return h ? dlsym(h, s) : nullptr; }
#ifdef __APPLE__
static inline std::string testConfigBase() { const char* h = std::getenv("HOME"); return h ? std::string(h) + "/Library/Application Support" : ""; }
#else
static inline std::string testConfigBase() { const char* h = std::getenv("HOME"); return h ? std::string(h) + "/.config" : ""; }
#endif
#endif
