// asp2 implementation. See fs.h for the story.
#include "asp2/fs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#else
#include <climits>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace asp2 {
namespace fs {

namespace Detail {

inline std::error_code NoErr() { return {}; }

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Trim trailing separators so "C:\a\b\" compares equal to "C:\a\b".
std::string normalizeForCompare(std::string s, bool windows) {
    if (windows) for (char& c : s) if (c == '/') c = '\\';
    while (s.size() > 3 && (s.back() == '\\' || s.back() == '/')) s.pop_back();
    return s;
}

} // namespace Detail

// ---------------------------------------------------------------------------
// Path queries
// ---------------------------------------------------------------------------
bool Path::exists() const { return fs::exists(*this); }
bool Path::isFile() const { return fs::isFile(*this); }
bool Path::isDir() const { return fs::isDir(*this); }
bool Path::isLink() const { return fs::isLink(*this); }
bool Path::isAbsolute() const { return p_.is_absolute(); }
uint64_t Path::size() const { return fs::fileSize(*this); }
int64_t Path::mtime() const { return fs::mtime(*this); }
Path Path::absolute() const {
    std::error_code ec;
    return Path(std::filesystem::absolute(p_, ec));
}

bool exists(const Path& p) {
    std::error_code ec;
    return std::filesystem::exists(p.stdPath(), ec);
}

bool isFile(const Path& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p.stdPath(), ec);
}

bool isDir(const Path& p) {
    std::error_code ec;
    return std::filesystem::is_directory(p.stdPath(), ec);
}

bool canExecute(const Path& p) {
#ifdef _WIN32
    return isFile(p);
#else
    return ::access(p.str().c_str(), X_OK) == 0;
#endif
}

uint64_t fileSize(const Path& p) {
    std::error_code ec;
    auto n = std::filesystem::file_size(p.stdPath(), ec);
    return ec ? 0 : uint64_t(n);
}

int64_t mtime(const Path& p) {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(p.stdPath(), ec);
    return ec ? 0 : int64_t(t.time_since_epoch().count());
}

bool makeDirs(const Path& p) {
    if (p.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(p.stdPath(), ec);
    std::error_code ec2;
    return std::filesystem::is_directory(p.stdPath(), ec2);
}

bool remove(const Path& p) {
    std::error_code ec;
    bool did = std::filesystem::remove(p.stdPath(), ec);
    return !ec && did;
}

uint64_t removeTree(const Path& p) {
    // Never follow links: deleting a junction/symlink must not delete the
    // folder it points at.
    if (isLink(p)) return removeLink(p) ? 1 : 0;
    std::error_code ec;
    auto n = std::filesystem::remove_all(p.stdPath(), ec);
    return ec ? 0 : uint64_t(n);
}

bool rename(const Path& from, const Path& to) {
    std::error_code ec;
    std::filesystem::rename(from.stdPath(), to.stdPath(), ec);
    return !ec;
}

bool move(const Path& from, const Path& to) {
    if (from.empty() || to.empty() || !exists(from)) return false;
    if (rename(from, to)) return true;
    // Cross-volume fallback: copy, then delete the original.
    if (isDir(from)) return false; // never silently deep-copy directories
    if (!copyFile(from, to)) return false;
    return remove(from);
}

bool copyFile(const Path& from, const Path& to) {
    std::error_code ec;
    std::filesystem::copy_file(from.stdPath(), to.stdPath(),
                               std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

std::vector<Path> listDir(const Path& dir) {
    std::vector<Path> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir.stdPath(), ec), end; !ec && it != end; it.increment(ec))
        out.push_back(Path(it->path()));
    std::sort(out.begin(), out.end(), [](const Path& a, const Path& b) { return a.str() < b.str(); });
    return out;
}

std::vector<Path> listFilesRecursive(const Path& dir, const std::string& extFilter) {
    std::vector<Path> out;
    std::error_code ec;
    std::string want = Detail::lower(extFilter);
    for (std::filesystem::recursive_directory_iterator it(dir.stdPath(), ec), end;
         !ec && it != end; it.increment(ec)) {
        const std::filesystem::directory_entry& e = *it;
        std::error_code ec2;
        if (!e.is_regular_file(ec2) || ec2) continue;
        if (!want.empty() && Detail::lower(e.path().extension().string()) != want) continue;
        out.push_back(Path(e.path()));
    }
    std::sort(out.begin(), out.end(), [](const Path& a, const Path& b) { return a.str() < b.str(); });
    return out;
}

Path executablePath() {
#ifdef _WIN32
    wchar_t b[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, b, DWORD(sizeof b / sizeof *b));
    if (n == 0 || n >= sizeof b / sizeof *b) return {};
    return Path(std::wstring(b, n));
#else
    std::error_code ec;
    std::filesystem::path p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec || p.empty()) return {};
    return Path(p);
#endif
}

// ---------------------------------------------------------------------------
// Whole-file read/write
// ---------------------------------------------------------------------------
std::vector<uint8_t> readBytes(const Path& p) {
    File f(p, "rb");
    std::vector<uint8_t> out;
    uint8_t buf[65536];
    size_t n;
    while ((n = f.read(buf, sizeof buf)) > 0) out.insert(out.end(), buf, buf + n);
    return out;
}

std::string readText(const Path& p) {
    std::vector<uint8_t> b = readBytes(p);
    return std::string(b.begin(), b.end());
}

void writeBytes(const Path& p, const void* data, size_t n) {
    File f(p, "wb");
    f.write(data, n);
}

void writeText(const Path& p, const std::string& s) {
    writeBytes(p, s.data(), s.size());
}

// ---------------------------------------------------------------------------
// File handle
// ---------------------------------------------------------------------------
void File::open(const Path& p, const char* mode) {
    close();
#ifdef _WIN32
    std::wstring m(mode, mode + std::strlen(mode));
    f_ = _wfopen(p.stdPath().c_str(), m.c_str());
#else
    f_ = std::fopen(p.stdPath().c_str(), mode);
#endif
    if (!f_) throw Error("cannot open " + p.str());
}

void File::close() {
    if (f_) std::fclose(f_);
    f_ = nullptr;
}

void File::seek(int64_t offset, int whence) {
#ifdef _WIN32
    int rc = _fseeki64(f_, offset, whence);
#else
    int rc = fseeko(f_, static_cast<off_t>(offset), whence);
#endif
    if (rc != 0) throw Error("seek failed");
}

int64_t File::tell() {
#ifdef _WIN32
    return _ftelli64(f_);
#else
    return static_cast<int64_t>(ftello(f_));
#endif
}

size_t File::read(void* buf, size_t n) { return std::fread(buf, 1, n, f_); }

void File::readExact(void* buf, size_t n) {
    if (read(buf, n) != n) throw Error("unexpected end of file");
}

void File::write(const void* buf, size_t n) {
    if (n && std::fwrite(buf, 1, n, f_) != n) throw Error("write failed");
}

void File::flush() { std::fflush(f_); }

void File::flushSync() {
    std::fflush(f_);
#ifdef _WIN32
    _commit(_fileno(f_));
#else
    fsync(fileno(f_));
#endif
}

// ---------------------------------------------------------------------------
// Hashing (FNV-1a 64)
// ---------------------------------------------------------------------------
uint64_t hashBytes(uint64_t seed, const uint8_t* data, size_t n) {
    uint64_t h = seed;
    for (size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t hashFile(const Path& p) {
    try {
        File f(p, "rb");
        std::vector<uint8_t> buf(1 << 20);
        uint64_t h = kHashSeed;
        size_t n;
        while ((n = f.read(buf.data(), buf.size())) > 0) h = hashBytes(h, buf.data(), n);
        return h;
    } catch (...) {
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Environment variables: %VAR%, ${VAR} and $VAR. Unknown ones stay as-is.
// ---------------------------------------------------------------------------
std::string expandEnv(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        char c = in[i];
        if (c == '%') {
            size_t j = in.find('%', i + 1);
            if (j != std::string::npos && j > i + 1) {
                std::string name = in.substr(i + 1, j - i - 1);
                if (const char* v = std::getenv(name.c_str())) { out += v; i = j + 1; continue; }
            }
            out += c;
            ++i;
        } else if (c == '$' && i + 1 < in.size() && in[i + 1] == '{') {
            size_t j = in.find('}', i + 2);
            if (j != std::string::npos) {
                std::string name = in.substr(i + 2, j - i - 2);
                if (const char* v = std::getenv(name.c_str())) { out += v; i = j + 1; continue; }
            }
            out += c;
            ++i;
        } else if (c == '$') {
            size_t j = i + 1;
            while (j < in.size() && (std::isalnum(static_cast<unsigned char>(in[j])) || in[j] == '_')) ++j;
            if (j > i + 1) {
                std::string name = in.substr(i + 1, j - i - 1);
                if (const char* v = std::getenv(name.c_str())) { out += v; i = j; continue; }
            }
            out += c;
            ++i;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Junctions (Windows) / symlinks (Unix)
// ---------------------------------------------------------------------------
#ifdef _WIN32

namespace Detail {

#pragma pack(push, 4)
struct ReparseHeader {
    unsigned long ReparseTag;
    unsigned short ReparseDataLength;
    unsigned short Reserved;
};
struct MountPointData {
    unsigned short SubstituteNameOffset;
    unsigned short SubstituteNameLength;
    unsigned short PrintNameOffset;
    unsigned short PrintNameLength;
    WCHAR PathBuffer[1];
};
#pragma pack(pop)

bool dirIsEmpty(const std::wstring& dir) {
    std::error_code ec;
    return std::filesystem::is_empty(std::filesystem::path(dir), ec) && !ec;
}

} // namespace Detail

bool makeLink(const Path& target, const Path& link) {
    std::wstring sub = L"\\??\\" + target.stdPath().wstring();
    while (sub.size() > 6 && sub.back() == L'\\') sub.pop_back(); // keep "\??\C:\" intact
    const std::wstring linkPath = link.stdPath().wstring();

    if (!CreateDirectoryW(linkPath.c_str(), nullptr)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
        if (!dirIsEmpty(linkPath)) return false; // refuse to link over content
    }

    HANDLE h = CreateFileW(linkPath.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    const size_t nameBytes = sub.size() * sizeof(WCHAR);
    std::vector<BYTE> buf(sizeof(ReparseHeader) + sizeof(MountPointData) - sizeof(WCHAR) +
                          nameBytes + sizeof(WCHAR));
    auto* hdr = reinterpret_cast<ReparseHeader*>(buf.data());
    hdr->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    hdr->Reserved = 0;
    hdr->ReparseDataLength = unsigned short(4 * sizeof(unsigned short) + nameBytes);
    auto* mp = reinterpret_cast<MountPointData*>(buf.data() + sizeof(ReparseHeader));
    mp->SubstituteNameOffset = 0;
    mp->SubstituteNameLength = unsigned short(nameBytes);
    mp->PrintNameOffset = unsigned short(nameBytes);
    mp->PrintNameLength = 0;
    std::memcpy(mp->PathBuffer, sub.c_str(), nameBytes + sizeof(WCHAR));

    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buf.data(), DWORD(buf.size()),
                              nullptr, 0, &returned, nullptr);
    CloseHandle(h);
    if (!ok) {
        RemoveDirectoryW(linkPath.c_str()); // drop the empty dir we made
        return false;
    }
    return true;
}

bool isLink(const Path& p) {
    DWORD attr = GetFileAttributesW(p.stdPath().c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

Path linkTarget(const Path& p) {
    HANDLE h = CreateFileW(p.stdPath().c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    BYTE buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0, buf, sizeof buf, &returned, nullptr);
    CloseHandle(h);
    if (!ok) return {};
    auto* hdr = reinterpret_cast<ReparseHeader*>(buf);
    if (hdr->ReparseTag != IO_REPARSE_TAG_MOUNT_POINT) return {};
    auto* mp = reinterpret_cast<MountPointData*>(buf + sizeof(ReparseHeader));
    const WCHAR* start = mp->PathBuffer + mp->SubstituteNameOffset / sizeof(WCHAR);
    std::wstring sub(start, mp->SubstituteNameLength / sizeof(WCHAR));
    if (sub.rfind(L"\\??\\UNC\\", 0) == 0) sub = L"\\\\" + sub.substr(8);
    if (sub.rfind(L"\\??\\", 0) == 0) sub.erase(0, 4);
    return Path(std::filesystem::path(sub));
}

bool removeLink(const Path& p) {
    if (!isLink(p)) return false;
    return RemoveDirectoryW(p.stdPath().c_str()) != 0;
}

#else // POSIX

bool makeLink(const Path& target, const Path& link) {
    return ::symlink(target.str().c_str(), link.str().c_str()) == 0;
}

bool isLink(const Path& p) {
    struct stat st;
    return ::lstat(p.str().c_str(), &st) == 0 && S_ISLNK(st.st_mode);
}

Path linkTarget(const Path& p) {
    char buf[PATH_MAX + 1];
    ssize_t n = ::readlink(p.str().c_str(), buf, PATH_MAX);
    if (n <= 0) return {};
    buf[n] = '\0';
    return Path(std::string(buf, size_t(n)));
}

bool removeLink(const Path& p) {
    if (!isLink(p)) return false;
    return ::unlink(p.str().c_str()) == 0;
}

#endif

bool isLinkTo(const Path& link, const Path& target) {
    if (!isLink(link)) return false;
    Path lt = linkTarget(link);
    if (lt.empty()) return false;
    const bool windows =
#ifdef _WIN32
        true;
#else
        false;
#endif
    std::string a = Detail::normalizeForCompare(lt.absolute().str(), windows);
    std::string b = Detail::normalizeForCompare(target.absolute().str(), windows);
    if (windows) { a = Detail::lower(a); b = Detail::lower(b); }
    return a == b;
}

} // namespace fs
} // namespace asp2
