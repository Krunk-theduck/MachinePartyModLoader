// Godot .pck reader/patcher. Direct port of the Pck class and project.binary
// helpers from the original modloader_patch.py.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <asp/fs.hpp>

namespace mpml {
namespace pck {

constexpr uint32_t kPackDirEncrypted = 1u << 0;
constexpr uint32_t kPackRelFilebase = 1u << 1;

// Little-endian byte helpers (were in mpml_io.h, now the only home).
inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint64_t le64(const uint8_t* p) { return uint64_t(le32(p)) | uint64_t(le32(p + 4)) << 32; }
inline void put32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i)));
}
inline void put64(std::vector<uint8_t>& o, uint64_t v) {
    for (int i = 0; i < 8; ++i) o.push_back(uint8_t(v >> (8 * i)));
}

struct Entry {
    std::string path;
    uint64_t offset = 0; // absolute offset in the file
    uint64_t size = 0;
    std::array<uint8_t, 16> md5{};
    uint32_t flags = 0;
    bool hasData = false; // true = pending in-memory content to be written
    std::vector<uint8_t> data;
};

class Pck {
public:
    explicit Pck(const asp::fs::Path& path); // throws mpml::Error

    int fmt() const { return fmt_; }
    uint32_t version(int i) const { return ver_[i]; }
    const std::vector<Entry>& entries() const { return entries_; }

    // Look up a file by canonical "res://..." path.
    bool readFile(const std::string& resPath, std::vector<uint8_t>& out) const;
    // Add or replace a file (kept in memory until written out).
    void put(const std::string& resPath, std::vector<uint8_t> data);

    // Format >= 3: append new data + new index to `target` (a byte-identical
    // copy of the file this object was parsed from) and repoint the header.
    void appendPatchV3(const asp::fs::Path& target);
    // Format < 3: write a complete rewritten pack to `out`.
    void rewriteV2(const asp::fs::Path& out);

private:
    bool rel() const { return (flags_ & kPackRelFilebase) != 0; }
    std::string norm(const std::string& p) const;
    const Entry* find(const std::string& resPath) const;
    void locateDirectoryV3(const uint8_t* head, size_t headLen, asp::fs::File& f);
    bool parseDirectory(asp::fs::File& f, uint64_t start, bool hasCount, std::vector<Entry>& out) const;
    std::vector<uint8_t> encodeIndex(const std::vector<Entry>& entries) const;

    asp::fs::Path path_;
    uint64_t filesize_ = 0;
    std::vector<Entry> entries_;
    int fmt_ = 2;
    uint32_t ver_[3] = {0, 0, 0};
    uint32_t flags_ = 0;
    uint64_t fileBase_ = 0;
    uint64_t dirOffset_ = 0;
    int dirField_ = -1;
    uint64_t dirFieldBase_ = 0;
    bool dirHasCount_ = true;
    bool resPrefix_ = true;
};

// project.binary ("ECFG") helpers
using Setting = std::pair<std::string, std::vector<uint8_t>>;
std::vector<Setting> ecfgParse(const std::vector<uint8_t>& data);
std::vector<uint8_t> ecfgBuild(const std::vector<Setting>& entries);
std::vector<uint8_t> encString(const std::string& s);

} // namespace pck
} // namespace mpml
