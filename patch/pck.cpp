#include "pck.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <type_traits>
#include <utility>

#include "md5.h"
#include "patch.h" // mpml::Error
#include "util/util.hpp"

namespace mpml {
namespace pck {

namespace Detail {

constexpr uint64_t kIndexStartV2 = 96; // python: V2_INDEX_START - 4
constexpr size_t kOffFormat = 4, kOffVersion = 8, kOffFlags = 20, kOffFileBase = 24, kOffDirOffset = 32;
constexpr size_t kChunk = 1 << 20;

// asp2 speaks Result; the pck layer speaks exceptions. This maps one onto the
// other, keeping the parse code free of error checks at every line.
template <typename T> requires (!std::is_void_v<T>)
T expect(asp::fs::Result<T>&& r, const std::string& what) {
    if (r.isErr()) throw Error(what + ": " + util::errText(std::move(r)));
    return std::move(r).unwrap();
}

void expect(asp::fs::Result<>&& r, const std::string& what) {
    if (r.isErr()) throw Error(what + ": " + util::errText(std::move(r)));
}

std::vector<uint8_t> pad4(const std::string& s) {
    std::vector<uint8_t> b(s.begin(), s.end());
    while (b.size() % 4) b.push_back(0);
    return b;
}

bool validUtf8(const std::string& s) {
    size_t i = 0, n = s.size();
    while (i < n) {
        uint8_t c = uint8_t(s[i]);
        size_t extra;
        uint32_t cp;
        if (c < 0x80) { ++i; continue; }
        else if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
        else return false;
        if (i + extra >= n) return false;
        for (size_t k = 1; k <= extra; ++k) {
            uint8_t cc = uint8_t(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000)) return false;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += extra + 1;
    }
    return true;
}

} // namespace Detail

// Detail's constants are used throughout this file; the using-directive keeps
// the call sites unqualified (this is a .cpp, so nothing leaks anywhere).
using namespace Detail;

Pck::Pck(const asp::fs::Path& path) : path_(path) {
    if (!path_.isFile())
        throw Error("pck not found: " + path_.string());
    filesize_ = util::fileSizeOr0(path_);
    asp::fs::File f = expect(asp::fs::File::open(path_), "cannot open " + path_.string());
    uint8_t head[256] = {0};
    size_t hn = expect(f.read(std::span<uint8_t>(head, sizeof head)),
                       "cannot read " + path_.string());
    if (hn < 32 || std::memcmp(head, "GDPC", 4) != 0)
        throw Error(util::nameOf(path_) +
                    " does not start with GDPC - it is probably embedded inside the .exe, "
                    "which this tool cannot patch.");
    fmt_ = int(le32(head + kOffFormat));
    for (int i = 0; i < 3; ++i) ver_[i] = le32(head + kOffVersion + 4 * i);
    if (fmt_ >= 2) {
        flags_ = le32(head + kOffFlags);
        fileBase_ = le64(head + kOffFileBase);
    }
    if (flags_ & kPackDirEncrypted) throw Error("This .pck has an encrypted directory. Cannot patch.");

    if (fmt_ >= 3) {
        locateDirectoryV3(head, hn, f);
    } else {
        dirOffset_ = Detail::kIndexStartV2;
        if (!parseDirectory(f, Detail::kIndexStartV2, true, entries_))
            throw Error("could not parse the format-2 file index.");
    }
    if (!entries_.empty()) resPrefix_ = entries_[0].path.rfind("res://", 0) == 0;
}

void Pck::locateDirectoryV3(const uint8_t* head, size_t headLen, asp::fs::File& f) {
    struct Cand { int field; uint64_t base, target; };
    std::vector<Cand> cands;
    std::set<int> seen;
    std::vector<int> fields = {int(kOffDirOffset)};
    for (int x = 24; x < 112; x += 8) fields.push_back(x);
    for (int field : fields) {
        if (size_t(field) + 8 > headLen || seen.count(field)) continue;
        uint64_t raw = le64(head + field);
        for (uint64_t base : {uint64_t(0), fileBase_}) {
            uint64_t target = raw + base;
            if (fileBase_ < target && target < filesize_) {
                cands.push_back({field, base, target});
                seen.insert(field);
            }
        }
    }
    for (const Cand& c : cands) {
        for (bool hasCount : {true, false}) {
            std::vector<Entry> entries;
            if (parseDirectory(f, c.target, hasCount, entries)) {
                dirField_ = c.field;
                dirFieldBase_ = c.base;
                dirOffset_ = c.target;
                dirHasCount_ = hasCount;
                entries_ = std::move(entries);
                return;
            }
        }
    }
    throw Error("Could not locate the file index. The header looks like pack format " +
                std::to_string(fmt_) + ", which this tool does not fully understand.");
}

bool Pck::parseDirectory(asp::fs::File& f, uint64_t start, bool hasCount, std::vector<Entry>& out) const {
    if (start >= filesize_) return false;
    const uint64_t region = filesize_ - start;
    uint64_t pos = 0;
    expect(f.seek(start), "cannot seek in " + path_.string());

    auto need = [&](uint64_t n) { return pos + n <= region; };
    auto rd32 = [&]() { uint8_t b[4]; expect(f.readExact(std::span<uint8_t>(b, 4)), "truncated index in " + path_.string()); pos += 4; return le32(b); };
    auto rd64 = [&]() { uint8_t b[8]; expect(f.readExact(std::span<uint8_t>(b, 8)), "truncated index in " + path_.string()); pos += 8; return le64(b); };

    uint64_t count = 0;
    if (hasCount) {
        if (!need(4)) return false;
        count = rd32();
        if (count < 1 || count > 5000000) return false;
    }
    const uint64_t tailFixed = 16 + 16 + (fmt_ >= 2 ? 4 : 0);
    for (uint64_t i = 0;; ++i) {
        if (hasCount && i >= count) break;
        if (pos >= region) break;
        if (!need(4)) return false;
        uint32_t plen = rd32();
        if (plen < 4 || plen > 4096 || plen % 4 != 0) return false;
        if (!need(plen)) return false;
        std::string raw(plen, '\0');
        expect(f.readExact(std::span<uint8_t>(reinterpret_cast<uint8_t*>(raw.data()), plen)),
               "truncated index in " + path_.string());
        pos += plen;
        while (!raw.empty() && raw.back() == '\0') raw.pop_back();
        if (raw.empty() || !Detail::validUtf8(raw)) return false;
        if (!need(tailFixed)) return false;

        Entry e;
        e.path = std::move(raw);
        uint64_t off = rd64();
        e.size = rd64();
        expect(f.readExact(std::span<uint8_t>(e.md5.data(), e.md5.size())),
               "truncated index in " + path_.string());
        pos += 16;
        if (fmt_ >= 2) e.flags = rd32();
        e.offset = off + (rel() ? fileBase_ : 0);
        if (e.offset > filesize_ || e.size > filesize_ - e.offset) return false;
        out.push_back(std::move(e));
    }
    if (out.empty()) return false;
    if (fmt_ >= 3 && pos != region) return false;
    return true;
}

std::string Pck::norm(const std::string& p) const {
    bool hasPrefix = p.rfind("res://", 0) == 0;
    if (resPrefix_ && !hasPrefix) return "res://" + p;
    if (!resPrefix_ && hasPrefix) return p.substr(6);
    return p;
}

const Entry* Pck::find(const std::string& resPath) const {
    std::string p = norm(resPath);
    for (const Entry& e : entries_)
        if (e.path == p) return &e;
    return nullptr;
}

bool Pck::readFile(const std::string& resPath, std::vector<uint8_t>& out) const {
    const Entry* e = find(resPath);
    if (!e) return false;
    if (e->hasData) { out = e->data; return true; }
    asp::fs::File f = expect(asp::fs::File::open(path_), "cannot open " + path_.string());
    expect(f.seek(e->offset), "cannot seek in " + path_.string());
    out.assign(e->size, 0);
    expect(f.readExact(std::span<uint8_t>(out.data(), out.size())),
           "cannot read " + path_.string());
    return true;
}

void Pck::put(const std::string& resPath, std::vector<uint8_t> data) {
    std::string p = norm(resPath);
    Entry* e = nullptr;
    for (Entry& x : entries_)
        if (x.path == p) { e = &x; break; }
    if (!e) {
        entries_.emplace_back();
        e = &entries_.back();
        e->path = p;
    }
    e->size = data.size();
    e->md5 = md5(data.data(), data.size());
    e->data = std::move(data);
    e->hasData = true;
}

std::vector<uint8_t> Pck::encodeIndex(const std::vector<Entry>& entries) const {
    std::vector<uint8_t> out;
    if (dirHasCount_ || fmt_ < 3) put32(out, uint32_t(entries.size()));
    for (const Entry& e : entries) {
        std::vector<uint8_t> b = Detail::pad4(e.path);
        uint64_t stored = e.offset - (rel() ? fileBase_ : 0);
        put32(out, uint32_t(b.size()));
        out.insert(out.end(), b.begin(), b.end());
        put64(out, stored);
        put64(out, e.size);
        out.insert(out.end(), e.md5.begin(), e.md5.end());
        if (fmt_ >= 2) put32(out, e.flags);
    }
    return out;
}

static std::vector<Entry> sortedByPath(std::vector<Entry> v) {
    std::sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });
    return v;
}

void Pck::appendPatchV3(const asp::fs::Path& target) {
    asp::fs::OpenOptions opts;
    opts.read = true;
    opts.write = true;
    asp::fs::File f = expect(asp::fs::File::open(target, opts), "cannot reopen " + target.string());
    expect(f.seek(expect(f.size(), "cannot size " + target.string())),
           "cannot seek to the end of " + target.string());
    for (Entry& e : entries_) {
        if (!e.hasData) continue;
        e.offset = expect(f.tell(), "cannot tell in " + target.string());
        expect(f.writeAll(std::span<const uint8_t>(e.data.data(), e.data.size())),
               "cannot write " + target.string());
    }
    const uint64_t newDir = expect(f.tell(), "cannot tell in " + target.string());
    std::vector<uint8_t> index = encodeIndex(sortedByPath(entries_));
    expect(f.writeAll(std::span<const uint8_t>(index.data(), index.size())),
           "cannot write the index to " + target.string());
    expect(f.sync(), "cannot flush " + target.string());

    std::vector<uint8_t> ptr;
    put64(ptr, newDir - dirFieldBase_);
    expect(f.seek(uint64_t(dirField_)), "cannot seek to the header pointer in " + target.string());
    expect(f.writeAll(std::span<const uint8_t>(ptr.data(), ptr.size())),
           "cannot repoint the header of " + target.string());
    expect(f.sync(), "cannot flush " + target.string());
}

void Pck::rewriteV2(const asp::fs::Path& outPath) {
    std::vector<Entry> entries = sortedByPath(entries_);
    uint64_t indexSize = 4;
    for (const Entry& e : entries) indexSize += 4 + Detail::pad4(e.path).size() + 8 + 8 + 16 + 4;
    std::vector<uint64_t> old;
    for (const Entry& e : entries) old.push_back(e.offset);

    uint64_t cursor = Detail::kIndexStartV2 + indexSize;
    for (Entry& e : entries) { e.offset = cursor; cursor += e.size; }
    flags_ &= ~kPackRelFilebase;
    fileBase_ = 0;
    fmt_ = 2;

    asp::fs::File out = expect(asp::fs::File::create(outPath), "cannot create " + outPath.string());
    std::vector<uint8_t> hdr = {'G', 'D', 'P', 'C'};
    put32(hdr, 2);
    for (int i = 0; i < 3; ++i) put32(hdr, ver_[i]);
    put32(hdr, 0);
    put64(hdr, 0);
    hdr.resize(hdr.size() + 64, 0);
    expect(out.writeAll(std::span<const uint8_t>(hdr.data(), hdr.size())),
           "cannot write " + outPath.string());
    std::vector<uint8_t> index = encodeIndex(entries);
    expect(out.writeAll(std::span<const uint8_t>(index.data(), index.size())),
           "cannot write " + outPath.string());

    asp::fs::File src = expect(asp::fs::File::open(path_), "cannot open " + path_.string());
    std::vector<uint8_t> buf(Detail::kChunk);
    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        if (e.hasData) {
            expect(out.writeAll(std::span<const uint8_t>(e.data.data(), e.data.size())),
                   "cannot write " + outPath.string());
            continue;
        }
        expect(src.seek(old[i]), "cannot seek in " + path_.string());
        uint64_t left = e.size;
        while (left) {
            size_t n = size_t(std::min<uint64_t>(Detail::kChunk, left));
            expect(src.readExact(std::span<uint8_t>(buf.data(), n)),
                   "cannot copy data from " + path_.string());
            expect(out.writeAll(std::span<const uint8_t>(buf.data(), n)),
                   "cannot write " + outPath.string());
            left -= n;
        }
    }
    expect(out.sync(), "cannot flush " + outPath.string());
}

// ---- project.binary (ECFG) ------------------------------------------------

std::vector<Setting> ecfgParse(const std::vector<uint8_t>& d) {
    if (d.size() < 8 || std::memcmp(d.data(), "ECFG", 4) != 0)
        throw Error("project.binary has a bad header.");
    size_t off = 4;
    auto u32 = [&]() {
        if (off + 4 > d.size()) throw Error("project.binary is truncated.");
        uint32_t v = le32(d.data() + off);
        off += 4;
        return v;
    };
    auto bytes = [&](uint32_t n) {
        if (off + n > d.size()) throw Error("project.binary is truncated.");
        std::vector<uint8_t> v(d.begin() + off, d.begin() + off + n);
        off += n;
        return v;
    };
    uint32_t count = u32();
    std::vector<Setting> out;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t klen = u32();
        auto k = bytes(klen);
        uint32_t vlen = u32();
        auto v = bytes(vlen);
        out.emplace_back(std::string(k.begin(), k.end()), std::move(v));
    }
    return out;
}

std::vector<uint8_t> ecfgBuild(const std::vector<Setting>& entries) {
    std::vector<uint8_t> out = {'E', 'C', 'F', 'G'};
    put32(out, uint32_t(entries.size()));
    for (const Setting& s : entries) {
        put32(out, uint32_t(s.first.size()));
        out.insert(out.end(), s.first.begin(), s.first.end());
        put32(out, uint32_t(s.second.size()));
        out.insert(out.end(), s.second.begin(), s.second.end());
    }
    return out;
}

std::vector<uint8_t> encString(const std::string& s) {
    std::vector<uint8_t> out;
    put32(out, 4); // Variant::STRING
    put32(out, uint32_t(s.size()));
    out.insert(out.end(), s.begin(), s.end());
    while (out.size() % 4) out.push_back(0);
    return out;
}

} // namespace pck
} // namespace mpml
