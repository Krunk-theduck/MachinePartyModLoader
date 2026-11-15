// asp2 - the one file API for the whole loader (second edition of asp).
//
// Everything that touches the disk (launcher, proxy DLL, patcher, logger) goes
// through this namespace and nothing else, so file behaviour only ever needs
// to be changed in one place: asp2/fs.h + asp2/fs.cpp.
//
// Second edition - what changed from asp v1:
//   - fs::mtime()      file modification time without throwing (patch record)
//   - fs::executablePath()   path of the running exe (launcher + proxy)
//   - fs::canExecute()       POSIX X_OK check for "is this the game exe?"
//   - File::isOpen()         so callers can log instead of guessing
//   - everything from v1: Path, File, read/write, copy/move/remove, listing,
//     hashing, expandEnv and the junction/symlink "spoof" helpers.
//
//   asp2::fs::Path   - small wrapper around std::filesystem::path
//   asp2::fs::File   - stdio handle with 64-bit seeks (safe in the proxy)
//   asp2::fs::...    - free functions: read/write/copy/move/remove/list/hash
#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace asp2 {
namespace fs {

// Error type used across asp2 (thrown only by read/write/File helpers).
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------------------
// Path
// ---------------------------------------------------------------------------
class Path {
public:
    Path() = default;
    Path(const char* s) : p_(s) {}
    Path(std::string s) : p_(std::move(s)) {}
    Path(std::filesystem::path p) : p_(std::move(p)) {}
#ifdef _WIN32
    Path(std::wstring s) : p_(std::move(s)) {}
#endif

    // Joining: dir / "mods" / "console"
    Path operator/(const Path& more) const { return Path(p_ / more.p_); }
    Path& operator/=(const Path& more) { p_ /= more.p_; return *this; }
    bool operator==(const Path& o) const { return p_ == o.p_; }
    bool operator!=(const Path& o) const { return p_ != o.p_; }

    const std::filesystem::path& stdPath() const { return p_; }
    std::string str() const { return p_.string(); }
#ifdef _WIN32
    std::wstring wstr() const { return p_.wstring(); }
#endif

    bool empty() const { return p_.empty(); }
    Path parent() const { return Path(p_.parent_path()); }
    std::string name() const { return p_.filename().string(); }  // "save.cfg"
    std::string stem() const { return p_.stem().string(); }      // "save"
    std::string ext() const { return p_.extension().string(); }  // ".cfg"

    // Queries (never throw; missing = false). Implemented in fs.cpp.
    bool exists() const;
    bool isFile() const;
    bool isDir() const;
    bool isLink() const; // symlink or junction
    bool isAbsolute() const;
    uint64_t size() const;
    int64_t mtime() const; // last write time, 0 when unknown
    Path absolute() const;

private:
    std::filesystem::path p_;
};

// ---------------------------------------------------------------------------
// Queries and basic operations (all non-throwing unless documented)
// ---------------------------------------------------------------------------
bool exists(const Path& p);
bool isFile(const Path& p);
bool isDir(const Path& p);
bool isLink(const Path& p);
bool canExecute(const Path& p);   // POSIX: X_OK. Windows: is a regular file.
uint64_t fileSize(const Path& p);
int64_t mtime(const Path& p);     // last write time in filesystem clock ticks, 0 when unknown

// Creates every missing folder. Returns false if it still doesn't exist.
bool makeDirs(const Path& p);

// Removes a single file or an EMPTY directory. Returns true if something was removed.
bool remove(const Path& p);
// Removes a file or a whole folder tree. Returns the number of entries removed.
uint64_t removeTree(const Path& p);

// Renames within the same volume. Fails if `to` exists (remove it first).
bool rename(const Path& from, const Path& to);

// Real move: rename, with a copy+delete fallback across volumes/devices.
bool move(const Path& from, const Path& to);

// Overwrite-copy of a single file.
bool copyFile(const Path& from, const Path& to);

// Directory listing, sorted by name (files and folders, not recursive).
std::vector<Path> listDir(const Path& dir);
// Recursive listing of files (optionally filtered by extension, e.g. ".gd").
std::vector<Path> listFilesRecursive(const Path& dir, const std::string& extFilter = {});

// Absolute path of the running executable ("" if it cannot be determined).
Path executablePath();

// ---------------------------------------------------------------------------
// Reading / writing whole files
// ---------------------------------------------------------------------------
std::vector<uint8_t> readBytes(const Path& p); // throws Error on failure
std::string readText(const Path& p);
void writeBytes(const Path& p, const void* data, size_t n);
void writeText(const Path& p, const std::string& s);

// ---------------------------------------------------------------------------
// stdio handle with large-file support (pck parsing needs seeks).
// Deliberately avoids iostreams so it is safe to use from DllMain in the proxy.
// ---------------------------------------------------------------------------
class File {
public:
    File() = default;
    File(const Path& p, const char* mode) { open(p, mode); }
    ~File() { close(); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    void open(const Path& p, const char* mode); // throws Error on failure
    void close();
    bool isOpen() const { return f_ != nullptr; }
    void seek(int64_t offset, int whence = SEEK_SET); // throws Error
    int64_t tell();
    size_t read(void* buf, size_t n);
    void readExact(void* buf, size_t n); // throws Error on short read
    void write(const void* buf, size_t n); // throws Error
    void flush();     // push stdio buffers to the OS
    void flushSync(); // flush + fsync (durable)

private:
    std::FILE* f_ = nullptr;
};

// ---------------------------------------------------------------------------
// Hashing (FNV-1a 64) - cheap content fingerprint used for the patch record
// ---------------------------------------------------------------------------
inline constexpr uint64_t kHashSeed = 1469598103934665603ULL;
uint64_t hashBytes(uint64_t seed, const uint8_t* data, size_t n);
uint64_t hashFile(const Path& p); // 0 if the file cannot be read

// ---------------------------------------------------------------------------
// Environment variables: expands %VAR% (Windows style) and ${VAR} / $VAR.
// Unknown variables are left untouched in the returned string.
// ---------------------------------------------------------------------------
std::string expandEnv(const std::string& in);

// ---------------------------------------------------------------------------
// Folder "spoofing": move a folder's content to a new home and leave a link
// (junction on Windows, symlink on Unix) at the old location.
// ---------------------------------------------------------------------------
// Creates `link` as a junction/symlink pointing at `target` (both absolute).
// `link` must either not exist or be an empty directory. Returns false on failure.
bool makeLink(const Path& target, const Path& link);

// True when `link` exists and points at `target`.
bool isLinkTo(const Path& link, const Path& target);

// The absolute path a junction/symlink points at ("" if not a link).
Path linkTarget(const Path& link);

// Removes the link itself only (the pointed-to folder is untouched).
bool removeLink(const Path& link);

} // namespace fs
} // namespace asp2
