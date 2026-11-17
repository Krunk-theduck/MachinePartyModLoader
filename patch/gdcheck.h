// mpml::gd - the .gd script compile check.
//
// The game's engine is the final authority on GDScript, but waiting for the
// game to start is a slow way to find a typo. This is a static front-end that
// parses GDScript 2.0 (Godot 4) the way the engine's tokenizer does - logical
// lines, indentation blocks, strings, brackets - and reports the syntax errors
// the engine would report first, with file:line, BEFORE the game launches.
//
//   MachinePartyModLoader --compile            check gd/ and mods/ next to the
//                                              game exe, exit 1 on errors
//   MachinePartyModLoader --compile <path>     check one file or folder
//   on every launch                            same check, log only
//
// It is deliberately a syntax check only: type errors and unknown names stay
// the engine's job. Verdicts are cached in
// MachinePartyModLoader/cache/compile/ keyed by content hash + loader version,
// and every run writes MachinePartyModLoader/cache/compile_report.txt.
#pragma once
#include <string>
#include <vector>

#include <asp/fs.hpp>
#include "patch.h" // LogFn

namespace mpml {
namespace gd {

// One finding. line is 1-based.
struct Diag {
    int line = 0;
    bool error = false;
    std::string msg;
};

// Pure static check of one .gd source. No disk access, throws nothing.
std::vector<Diag> CheckSource(const std::string& src);

struct FileResult {
    asp::fs::Path path;
    bool cached = false; // verdict replayed from cache/compile/
    std::vector<Diag> diags;
};

// Check every .gd script under `root` (a file or a folder, recursive, sorted).
// With useCache the per-file verdict is stored in / replayed from
// cacheDir/compile/, so unchanged scripts are not re-parsed.
std::vector<FileResult> CheckTree(const asp::fs::Path& root, const asp::fs::Path& cacheDir,
                                  bool useCache);

// Check arbitrary roots (the --compile target). Returns the number of errors.
int CheckRoots(const std::vector<asp::fs::Path>& roots, const asp::fs::Path& cacheDir,
               const LogFn& log, bool writeReport, bool useCache);

// Check gd/ + mods/ next to the game exe, log a summary and every error, and
// write MachinePartyModLoader/compile_report.txt. Returns the number of errors.
int CheckLoaderScripts(const asp::fs::Path& gameDir, const LogFn& log, bool useCache);

} // namespace gd
} // namespace mpml
