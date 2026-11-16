// mpml_patch: everything that decides *whether* and *how* to build the modded
// pack, plus the runtime folders. Used by both the launcher (main/) and the
// proxy DLL (proxy/).
#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

#include <asp/fs.hpp>
#include "util/util.hpp"

namespace mpml {

// Error type thrown across the patcher (caught in EnsureModdedPck and main).
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};



struct Paths {
    asp::fs::Path original; // e.g. "Machine Party.pck"     (never touched)
    asp::fs::Path modded;   // e.g. "Machine Party.modded.pck"
    asp::fs::Path record() const;
};

struct Result {
    bool ok = false;      // modded pck exists and is current
    bool rebuilt = false; // we had to (re)build it this run
    std::string message;  // error text when !ok
};

// Find the game's .pck next to `gameExe` (prefers "<exe name>.pck").
std::optional<Paths> PathsFromExe(const asp::fs::Path& gameExe);

// Cheap check first (size + mtime), then a hash of the original, and only
// rebuild the modded copy when the game's pck really changed or the
// bootstrap script changed.
Result EnsureModdedPck(const Paths& paths, const LogFn& log = nullptr);

// Creates the loader folders next to the game: mods/ (with the example
// console mod when new), MachinePartyModLoader/plugins (with one example loose script when new) and
// MachinePartyModLoader/loader.cfg (the settings file, defaults only).
void EnsureLoaderFolders(const asp::fs::Path& gameDir, const LogFn& log = nullptr);

// Remove the modded pck and its record.
void RemoveModded(const Paths& paths, const LogFn& log = nullptr);

} // namespace mpml
