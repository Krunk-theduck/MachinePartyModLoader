// version.dll proxy. Windows loads this instead of the system version.dll because it
// sits next to the game exe, i.e. before Godot's code runs.
//
//   Make sure <game>.modded.pck is a current patched copy of the game's pck
//      (same code as the launcher: mpml_patch).
//
//    Hook kernelbase!CreateFileW so that opening "<game>.pck" opens
//      "<game>.modded.pck" instead.
//
// If anything fails nothing is hooked and the vanilla game starts.
// Diagnostics: mpml_proxy.log next to the exe. If that file doesn't appear, Windows
#include <cstdio>
#include <cwchar>
#include <string>
#include <windows.h>

#include "MinHook.h"
#include <asp/fs.hpp>
#include "logger/logger.hpp"
#include "patch.h"
#include "settings/settings.hpp"
#include "util/util.hpp"

extern "C" BOOL InitializeProxy();

namespace mpml {
namespace proxy {

using CreateFileW_t = HANDLE(WINAPI *)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
CreateFileW_t g_realCreateFileW = nullptr;
std::wstring g_originalFull; // normalized absolute path of the game's pck (no \\?\ prefix)
std::wstring g_moddedFull;   // absolute path of the modded copy
logger g_log;
volatile LONG g_pckOpens = 0;
volatile LONG g_writeLogs = 0;

// windows WideChars are weird, this fixes that
std::string Narrow(const wchar_t *w) {
        int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 1)
                return {};
        std::string s(size_t(n - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
        return s;
}

void Log(const std::string &s) { g_log.log(s); }

std::wstring FullPath(const wchar_t *p) {
        wchar_t buf[2048];
        DWORD n = GetFullPathNameW(p, DWORD(sizeof buf / sizeof *buf), buf, nullptr);
        if (n == 0 || n >= sizeof buf / sizeof *buf)
                return {};
        std::wstring s(buf, n);
        if (s.rfind(L"\\\\?\\", 0) == 0)
                s.erase(0, 4);
        return s;
}

bool EndsWithPck(const wchar_t *p) {
        size_t n = std::wcslen(p);
        return n >= 4 && _wcsicmp(p + n - 4, L".pck") == 0;
}

// Diagnostic: lets the log reveal where the game's user:// folder is (godot.log / modloader.log).
bool LooksInteresting(const wchar_t *p) {
        wchar_t low[600];
        size_t n = std::wcslen(p);
        if (n == 0 || n >= sizeof low / sizeof *low)
                return false;
        for (size_t i = 0; i <= n; ++i)
                low[i] = (p[i] >= L'A' && p[i] <= L'Z') ? wchar_t(p[i] + 32) : p[i];
        return std::wcsstr(low, L"godot.log") || std::wcsstr(low, L"modloader") ||
               std::wcsstr(low, L"app_userdata");
}

HANDLE WINAPI HookedCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags, HANDLE tmpl) {
        const bool writing = (access & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
        bool redirect = false;
        if (name && !writing && EndsWithPck(name)) {
                std::wstring full = FullPath(name);
                redirect = !full.empty() && _wcsicmp(full.c_str(), g_originalFull.c_str()) == 0;
                LONG n = InterlockedIncrement(&g_pckOpens);
                if (n <= 3 || (redirect && n % 500 == 0))
                        Log(".pck open #" + std::to_string(n) + ": " + Narrow(name) + (redirect ? "  -> redirected to modded" : "  (modded/other, passed through)"));
                if (redirect)
                        name = g_moddedFull.c_str();
        } else if (name && writing && InterlockedCompareExchange(&g_writeLogs, 0, 0) < 20 && LooksInteresting(name)) {
                InterlockedIncrement(&g_writeLogs);
                Log(std::string("game writes: ") + Narrow(name));
        }
        return g_realCreateFileW(name, access, share, sa, disposition, flags, tmpl);
}

void *FindExport(const char *fn) {
        for (const wchar_t *dll : {L"kernelbase.dll", L"kernel32.dll"}) {
                HMODULE m = GetModuleHandleW(dll);
                if (!m)
                        continue;
                if (void *p = reinterpret_cast<void *>(GetProcAddress(m, fn)))
                        return p;
        }
        return nullptr;
}

bool InstallHook(const char *name, void *detour, void **original) {
        void *target = FindExport(name);
        if (!target) {
                Log(std::string("could not find ") + name);
                return false;
        }
        MH_STATUS s = MH_CreateHook(target, detour, original);
        if (s != MH_OK) {
                Log(std::string("MH_CreateHook(") + name + ") failed: " + MH_StatusToString(s));
                return false;
        }
        s = MH_EnableHook(target);
        if (s != MH_OK) {
                Log(std::string("MH_EnableHook(") + name + ") failed: " + MH_StatusToString(s));
                return false;
        }
        Log(std::string("hooked ") + name);
        return true;
}

// settings::Values.console = true -> give the (windowed) game a console so the
// loader logs are visible live. Off by default; flipped by loader.cfg.
void EnableConsole() {
        if (!AllocConsole())
                return;
        // plain freopen: works on MSVC and MinGW alike (_CRT_SECURE_NO_WARNINGS
        // is set project-wide, so MSVC stays quiet about it)
        if (!freopen("CONOUT$", "w", stdout))
                return;
        if (!freopen("CONOUT$", "w", stderr))
                return;
        (void)freopen("CONIN$", "r", stdin);
        SetConsoleTitleW(L"Machine Party mod loader");
}

void Run() {
        const asp::fs::Path exe = util::executablePath();
        if (exe.empty())
                return;

        const asp::fs::Path gameDir = util::parentOf(exe);
        if (mpml::settings::load(mpml::settings::fileFor(gameDir)).console)
                EnableConsole();

        g_log.setFile(gameDir / "mpml_proxy.log");
        Log("proxy loaded in: " + exe.string()); // proves Windows loaded this DLL

        auto paths = PathsFromExe(exe);
        if (!paths) {
                Log("no .pck found next to the exe");
                return;
        }

        // Do the patching BEFORE hooking, so our own reads of the original pck aren't redirected.
        Result r = EnsureModdedPck(*paths, Log);
        if (!r.ok) {
                Log("patching failed, starting vanilla: " + r.message);
                return;
        }
        EnsureLoaderFolders(util::parentOf(paths->original), Log);

        g_originalFull = paths->original.native();
        g_moddedFull = paths->modded.native();
        Log("original: " + Narrow(FullPath(g_originalFull.c_str()).c_str()));
        Log("modded:   " + Narrow(FullPath(g_moddedFull.c_str()).c_str()));

        MH_STATUS s = MH_Initialize();
        if (s != MH_OK) {
                Log(std::string("MH_Initialize failed: ") + MH_StatusToString(s));
                return;
        }

        InstallHook("CreateFileW", reinterpret_cast<void *>(&HookedCreateFileW), reinterpret_cast<void **>(&g_realCreateFileW));
        Log("ready - waiting for the game to start");
}

} // namespace proxy
} // namespace mpml

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
        if (reason == DLL_PROCESS_ATTACH) {
                if (!InitializeProxy())
                        return FALSE;
                DisableThreadLibraryCalls(h);
                try {
                        mpml::proxy::Run();
                } catch (...) {
                }
        }
        return TRUE;
}
