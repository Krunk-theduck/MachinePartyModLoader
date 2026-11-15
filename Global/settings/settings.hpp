// mpml::settings - the loader's tiny settings file.
//
// The file is <gameDir>/MachinePartyModLoader/loader.cfg, created with a
// documented default the first time the loader runs. It is plain
// "key = value" text: '#' starts a comment, unknown keys are ignored, and
// missing values fall back to their defaults, so hand edits can never break
// the loader.
//
//   console = false   Windows: allocate a console window for the game so the
//                     loader logs are visible live. Off by default.
#pragma once
#include <string>

#include <asp/fs.hpp>

namespace mpml {
namespace settings {

struct Values {
    bool console = false; // Windows: show a debug console window (off by default)
};

// Parses loader.cfg text. Anything unrecognised is skipped; "true/1/yes/on"
// (case-insensitive) turn a key on, "false/0/no/off" turn it off.
Values parse(const std::string& text);

// Reads + parses the file. A missing or unreadable file simply means defaults.
Values load(const asp::fs::Path& file);

// The canonical settings file for a game folder.
asp::fs::Path fileFor(const asp::fs::Path& gameDir);

// Creates the documented default file when it does not exist yet. Never
// overwrites an existing file (user edits are safe). Best effort: failures
// are silent, the loader works fine without the file.
void ensureDefault(const asp::fs::Path& file);

} // namespace settings
} // namespace mpml
