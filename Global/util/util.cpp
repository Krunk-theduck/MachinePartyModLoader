#include "util/util.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include <asp/iter.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace mpml {
namespace util {
namespace {

namespace fs = asp::fs;
using fs::Path;

} // namespace

Path executablePath() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH + 1] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return Path{};
    return Path::fromWide(std::wstring(buf, n));
#elif defined(__APPLE__)
    uint32_t size = 0;
    if (_NSGetExecutablePath(nullptr, &size) != 0) return Path{};
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return Path{};
    return Path{buf.data()};
#else
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return Path{};
    buf[n] = '\0';
    return Path{buf};
#endif
}

bool canExecute(const Path& p) {
#ifdef _WIN32
    return p.isFile();
#else
    return ::access(p.native().c_str(), X_OK) == 0;
#endif
}

uint64_t hashBytes(uint64_t seed, const uint8_t* data, size_t n) {
    uint64_t h = seed;
    for (size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t hashFile(const Path& p) {
    auto res = fs::File::open(p);
    if (res.isErr()) return 0;
    fs::File f = std::move(res).unwrap();
    std::vector<uint8_t> buf(1 << 16);
    uint64_t h = kHashSeed;
    for (;;) {
        auto got = f.read(std::span<uint8_t>(buf));
        if (got.isErr()) return 0;
        const size_t n = std::move(got).unwrap();
        if (n == 0) break;
        h = hashBytes(h, buf.data(), n);
    }
    return h;
}

std::vector<Path> listDirSorted(const Path& dir) {
    std::vector<Path> out;
    auto it = fs::iterDir(dir);
    if (it.isErr()) return out;
    for (auto next : std::move(it).unwrap()) {
        if (next.isErr()) continue;
        out.push_back(std::move(next).unwrap().path);
    }
    std::sort(out.begin(), out.end(),
              [](const Path& a, const Path& b) { return a.string() < b.string(); });
    return out;
}

static std::string extFilter(const std::string& ext) {
    // callers may write ".gd" or "gd"; asp2 extensions carry no dot
    return !ext.empty() && ext.front() == '.' ? ext.substr(1) : ext;
}

std::vector<Path> listFilesRecursive(const Path& dir, const std::string& ext) {
    std::vector<Path> out;
    const std::string want = extFilter(ext);
    auto it = fs::iterDirRecursive(dir);
    if (it.isErr()) return out;
    for (auto next : std::move(it).unwrap()) {
        if (next.isErr()) continue;
        fs::DirectoryEntry entry = std::move(next).unwrap();
        if (!entry.isFile()) continue;
        if (!want.empty() && entry.path.extension().value_or("") != want) continue;
        out.push_back(std::move(entry.path));
    }
    std::sort(out.begin(), out.end(),
              [](const Path& a, const Path& b) { return a.string() < b.string(); });
    return out;
}

uint64_t removeTree(const Path& p) {
    if (!p.exists()) return 0;
    if (p.isSymlink() || !p.isDir()) return fs::remove(p).isOk() ? 1 : 0;
    return fs::removeDirAll(p, false).isOk() ? 1 : 0;
}

uint64_t fileSizeOr0(const Path& p) {
    auto meta = p.metadata();
    if (meta.isErr()) return 0;
    fs::Metadata m = std::move(meta).unwrap();
    return m.isFile() ? m.size : 0;
}

int64_t mtimeNs(const Path& p) {
    auto meta = p.metadata();
    if (meta.isErr()) return 0;
    fs::Metadata m = std::move(meta).unwrap();
    if (!m.modifiedAt.has_value()) return 0;
    return static_cast<int64_t>(m.modifiedAt->timeSinceEpoch().nanos());
}

Path parentOf(const Path& p) {
    return Path{p.parent().value_or(fs::PathView("."))};
}

std::string nameOf(const Path& p) {
    return std::string(p.filename().value_or(fs::PathView{}));
}

std::string stemOf(const Path& p) {
    return std::string(p.stem().value_or(""));
}

std::string absString(const Path& p) {
    if (p.empty()) return {};
    auto a = p.absolute();
    return a.isOk() ? std::move(a).unwrap().string() : p.string();
}

Path absOf(const Path& p) {
    if (p.empty()) return p;
    auto a = p.absolute();
    return a.isOk() ? std::move(a).unwrap() : p;
}

} // namespace util
} // namespace mpml
