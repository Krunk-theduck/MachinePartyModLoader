// mpml::util - the small file helpers the loader needs that asp2 does not
// provide. asp2 (asp::fs) stays the only way to actually touch the disk; these
// are the few things on top: the exe path, the POSIX "is executable" check,
// FNV-1a hashing for change detection, deterministic (sorted) listings and a
// file-or-tree removal.
//
// Everything is non-throwing: failures turn into empty results / false / 0,
// exactly where the loader calls them from.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
using LogFn = std::function<void(const std::string&)>;

#include <asp/fs.hpp>

namespace mpml {
namespace util {

// Absolute path of the running executable ("" when it cannot be determined).
// GetModuleFileNameW on Windows, /proc/self/exe on Linux, _NSGetExecutablePath
// on macOS.
asp::fs::Path executablePath();

// POSIX: the executable bit is set. Windows: any regular file.
bool canExecute(const asp::fs::Path& p);

// FNV-1a 64 - the cheap content fingerprint behind the patch record and the
// compile-check verdict cache.
inline constexpr uint64_t kHashSeed = 1469598103934665603ULL;
uint64_t hashBytes(uint64_t seed, const uint8_t* data, size_t n);
uint64_t hashFile(const asp::fs::Path& p); // 0 when the file cannot be read

// Directory listing sorted by name. asp2's iterDir order is OS-defined; the
// loader relies on deterministic order (first .pck wins, first game exe wins),
// so every listing goes through here.
std::vector<asp::fs::Path> listDirSorted(const asp::fs::Path& dir);

// Recursive file listing sorted by full path, optionally filtered by
// extension. The filter may be written with or without the dot (".gd"/"gd").
// Symlinks are never followed (same rule as asp2's recursive iterator).
std::vector<asp::fs::Path> listFilesRecursive(const asp::fs::Path& dir, const std::string& ext = {});

// Removes a file or a whole folder tree. Returns how many entries went away.
uint64_t removeTree(const asp::fs::Path& p);

// Size of a file (0 when it is missing or not a regular file).
uint64_t fileSizeOr0(const asp::fs::Path& p);

// Last-write time in nanoseconds since the Unix epoch (0 when unknown).
int64_t mtimeNs(const asp::fs::Path& p);

// Lexical parent that always answers: "." when the path has none. Saves every
// call site the std::optional dance for the one case that cannot happen.
asp::fs::Path parentOf(const asp::fs::Path& p);

// File name / stem as plain std::string ("" when the component is absent).
std::string nameOf(const asp::fs::Path& p);
std::string stemOf(const asp::fs::Path& p);

// Absolute form as a string; falls back to the input when the OS refuses
// (used only for "is this the running exe?" comparisons and logs).
std::string absString(const asp::fs::Path& p);

// Absolute form as a Path; falls back to the input when the OS refuses.
asp::fs::Path absOf(const asp::fs::Path& p);

// The error text of a failed asp2 Result, for throw/log sites.
//   throw Error("cannot open pck: " + util::errText(result));
template <typename R>
std::string errText(R&& r) {
    return std::string(std::forward<R>(r).unwrapErr().toString());
}

} // namespace util
} // namespace mpml
