// MachinePartyModLoader launcher.
//
//   Steam launch option:   "<path>/MachinePartyModLoader" %command%
//
// It makes sure "<game>.modded.pck" is a current patched copy of the game's pck,
// then starts the game with "--main-pack <game>.modded.pck" appended.
// The game's own files are never modified. If anything goes wrong it launches
// the unmodified game so a broken loader can never stop you playing.
//
//   MachinePartyModLoader                 patch now (standalone), create mods/ + gd/
//   MachinePartyModLoader --play          ...and then start the game
//   MachinePartyModLoader --compile [p]   compile-check .gd scripts (gd/ + mods/,
//                                         or one file/folder/zip), exit 1 on errors
//   MachinePartyModLoader --uninstall     delete the modded pck + record
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <asp/fs.hpp>
#include "logger/logger.hpp"
#include "patch.h"
#include "gdcheck.h"
#include "util/util.hpp"
#include "Dirs/paths.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

namespace mpml {

// The launcher's one logger: stderr + mpml.log
static logger g_log;
void Log(const std::string& s) { g_log.log(s); }

namespace Detail {

namespace fs = asp::fs;

std::vector<fs::Path> GetArgs(int argc, char** argv) {
    std::vector<fs::Path> out;
#ifdef _WIN32
    (void)argc; (void)argv;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i) out.emplace_back(fs::Path::fromWide(std::wstring(w[i])));
    LocalFree(w);
#else
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
#endif
    return out;
}

#ifdef _WIN32
// Standard Windows argv quoting ("Everyone quotes command line arguments the wrong way").
void QuoteArg(std::wstring& out, const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) { out += a; return; }
    out += L'"';
    for (auto it = a.begin();; ++it) {
        unsigned backslashes = 0;
        while (it != a.end() && *it == L'\\') { ++it; ++backslashes; }
        if (it == a.end()) { out.append(backslashes * 2, L'\\'); break; }
        if (*it == L'"') out.append(backslashes * 2 + 1, L'\\'), out += L'"';
        else out.append(backslashes, L'\\'), out += *it;
    }
    out += L'"';
}
#endif

// Run the command and (on Windows) wait for it; on POSIX replace this process.
int Launch(const std::vector<fs::Path>& cmd) {
    if (cmd.empty()) return 1;
#ifdef _WIN32
    std::wstring line;
    for (size_t i = 0; i < cmd.size(); ++i) { if (i) line += L' '; QuoteArg(line, cmd[i].native()); }
    STARTUPINFOW si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        Log("could not start the game (error " + std::to_string(GetLastError()) + ")");
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return int(code);
#else
    std::vector<fs::Path> abs;
    fs::Path first = cmd[0];
    if (!first.isAbsolute()) {
        auto a = first.absolute();
        if (a.isOk()) first = std::move(a).unwrap(); // execvp only searches PATH
    }
    abs.push_back(first);
    abs.insert(abs.end(), cmd.begin() + 1, cmd.end());
    std::vector<std::string> s;
    for (auto& p : abs) s.push_back(p.string());
    std::vector<char*> argv;
    for (auto& x : s) argv.push_back(x.data());
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    Log(std::string("could not start the game: ") + std::strerror(errno));
    return 1;
#endif
}

bool IsFlag(const fs::Path& p) { return p.string().rfind("--", 0) == 0; }

bool HasMainPack(const std::vector<fs::Path>& v) {
    for (auto& p : v) if (p.string() == "--main-pack") return true;
    return false;
}

// The game exe candidates in one folder (skips loader/crash-handler tools and
// the running loader itself).
std::vector<fs::Path> GameExeCandidatesIn(const fs::Path& dir, const fs::Path& self) {
    std::vector<fs::Path> c;
    for (const fs::Path& p : util::listDirSorted(dir)) {
        std::string n = util::nameOf(p);
        for (auto& ch : n) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        if (!self.empty() && util::absString(p) == util::absString(self)) continue;
#ifdef _WIN32
        if (p.extension().value_or("") == "exe") c.push_back(p);
#else
        if (p.extension().value_or("").empty() && p.isFile() && util::canExecute(p)) c.push_back(p);
#endif
    }
    return c; // listDirSorted is sorted, so candidates are too
}

// Standalone: find the game exe beside the loader. When the loader lives
// inside the MachinePartyModLoader folder (Linux cannot have the binary AND
// that folder side by side - same name), also look one level up. A candidate
// only counts when a matching .pck sits next to it.
std::optional<fs::Path> FindGameExeBeside(const fs::Path& dir, const fs::Path& self) {
    const fs::Path up = util::parentOf(dir);
    for (const fs::Path* scan : {&dir, &up}) {
        if (scan->empty()) continue;
        for (const fs::Path& cand : GameExeCandidatesIn(*scan, self)) {
            if (PathsFromExe(cand)) return cand;
        }
    }
    return std::nullopt;
}

// Best guess at the game dir when no pck is around: the folder that holds
// mods/ (the loader may sit inside MachinePartyModLoader/ on Linux).
fs::Path GuessGameDir(const fs::Path& dir) {
    const fs::Path up = util::parentOf(dir);
    for (const fs::Path* d : {&dir, &up}) {
        if ((*d / "mods").isDir()) return *d;
    }
    return dir;
}

} // namespace Detail

int main(int argc, char** argv) {
    std::vector<asp::fs::Path> args = Detail::GetArgs(argc, argv);
    std::vector<asp::fs::Path> rest(args.begin() + (args.empty() ? 0 : 1), args.end());

    bool uninstall = false, play = false, compile = false;
    std::optional<asp::fs::Path> compileTarget;
    for (size_t i = 0; i < rest.size(); ++i) {
        const asp::fs::Path& a = rest[i];
        if (a == "--uninstall") uninstall = true;
        if (a == "--play") play = true;
        if (a == "--compile") compile = true;
        if (a == "--compile" && i + 1 < rest.size() && !Detail::IsFlag(rest[i + 1]))
            compileTarget = rest[i + 1];
    }

    if (!rest.empty() && !Detail::IsFlag(rest[0])) {
        std::optional<Paths> paths;
        for (auto& a : rest) {
            if (!Detail::IsFlag(a) && a.isFile()) {
                paths = PathsFromExe(util::absOf(a));
                if (paths) break;
            }
        }
        std::vector<asp::fs::Path> cmd = rest;
        if (paths) {
            const asp::fs::Path gameDir = util::parentOf(paths->original);
            g_log.setFile(mpml::dirs::logs(gameDir) / "mpml.log");
            Result r = EnsureModdedPck(*paths, Log);
            if (r.ok) {
                EnsureLoaderFolders(gameDir, Log);
                gd::CheckLoaderScripts(gameDir, Log, true);
                if (!Detail::HasMainPack(cmd)) {
                    cmd.emplace_back("--main-pack");
                    cmd.push_back(paths->modded);
                }
            } else {
                Log("patching failed, starting the unmodified game: " + r.message);
            }
        } else {
            std::fprintf(stderr, "MachinePartyModLoader: could not find the game's .pck; starting unmodified.\n");
        }
        return Detail::Launch(cmd);
    }

    // Standalone mode.
    std::printf("\nMachine Party mod loader\n");
    const asp::fs::Path self = util::executablePath();
    asp::fs::Path dir = self.empty() ? asp::fs::Path(".") : util::parentOf(self);
    std::optional<asp::fs::Path> exe = Detail::FindGameExeBeside(dir, self);
    std::optional<Paths> paths = exe ? PathsFromExe(*exe) : std::nullopt;
    // Without the game's pck we can still serve mod authors: --compile works
    // anywhere, rooted at the folder that holds mods/ or gd/.
    const asp::fs::Path gameDir = paths ? util::parentOf(paths->original) : Detail::GuessGameDir(dir);
    g_log.setFile(mpml::dirs::logs(gameDir) / "mpml.log");

    if (compile) {
        std::vector<asp::fs::Path> roots;
        if (compileTarget) {
            roots.push_back(*compileTarget);
        } else {
            roots.push_back(gameDir / "gd");
            roots.push_back(gameDir / "mods");
        }
        const int errors = gd::CheckRoots(roots, mpml::dirs::cache(gameDir), Log, !compileTarget, true);
        if (errors == 0) Log("compile check passed");
        else Log(std::to_string(errors) + " script error(s) - details above and in " +
                 (mpml::dirs::cache(gameDir) /  "compile_report.txt").string());
        return errors == 0 ? 0 : 1;
    }

    if (!paths) {
        std::fprintf(stderr, "ERROR: put this next to the game's executable and .pck, or use it as a Steam launch option.\n");
        return 1;
    }

    if (uninstall) {
        RemoveModded(*paths, Log);
        Log("done. Also remove the Steam launch option / proxy DLL if you used them. Your mods and gd folders were left alone.");
        return 0;
    }

    Result r = EnsureModdedPck(*paths, Log);
    if (!r.ok) { std::fprintf(stderr, "\nERROR: %s\n", r.message.c_str()); return 1; }
    if (!r.rebuilt) Log("already up to date - nothing to do");
    EnsureLoaderFolders(gameDir, Log);
    gd::CheckLoaderScripts(gameDir, Log, true);

    if (play) {
        return Detail::Launch({*exe, asp::fs::Path("--main-pack"), paths->modded});
    }
    std::printf("\nSet the game's Steam launch option to:\n\n  \"%s\" %%command%%\n\n", self.string().c_str());
    return 0;
}

} // namespace mpml

int main(int argc, char** argv) { return mpml::main(argc, argv); }
