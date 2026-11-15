#include "patch.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <string>
#include <vector>

#include "embedded.h"
#include "pck.h"
#include "version.h"

namespace mpml {
namespace Detail {

// res:// paths inside the game pck that we add or edit.
const char* const kBootstrapRes = "res://modloader/bootstrap.gd";
const char* const kStampRes = "res://modloader/.stamp";
const char* const kAutoloadKey = "autoload/ModLoader";
const char* const kProjectBinary = "res://project.binary";

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string hex64(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

// bootstrap.gd (embedded at compile time) carries the @MPML_VERSION@ placeholder;
// this injects mpml::kVersion so the version lives in exactly one place. A
// version bump therefore changes the bootstrap bytes -> the modded pck rebuilds.
std::string finalizeBootstrap() {
    static const std::string kPlaceholder = "@MPML_VERSION@";
    std::string s(reinterpret_cast<const char*>(kBootstrapGd), kBootstrapGdSize);
    size_t at = s.find(kPlaceholder);
    if (at == std::string::npos)
        throw Error("bootstrap.gd lost its version placeholder - the loader version can no\n"
                    "  longer be synced; restore the line  const LOADER_VERSION := \"@MPML_VERSION@\"");
    for (; at != std::string::npos; at = s.find(kPlaceholder))
        s.replace(at, kPlaceholder.size(), kVersion);
    return s;
}

// --- patch record (one small text file beside the modded pck) ---------------

struct Record {
    uint64_t size = 0;
    int64_t mtime = 0;
    uint64_t hash = 0;
    std::string loader;
};

bool loadRecord(const asp2::fs::Path& p, Record& r) {
    try {
        std::string s = asp2::fs::readText(p);
        unsigned long long size = 0, hash = 0;
        long long mtime = 0;
        char loader[128] = {0};
        if (std::sscanf(s.c_str(), "%llu %lld %llx %127s", &size, &mtime, &hash, loader) != 4)
            return false;
        r.size = size;
        r.mtime = mtime;
        r.hash = hash;
        r.loader = loader;
        return true;
    } catch (...) {
        return false;
    }
}

void saveRecord(const asp2::fs::Path& p, const Record& r) {
    char buf[256];
    int n = std::snprintf(buf, sizeof buf, "%llu %lld %llx %s\n",
                          static_cast<unsigned long long>(r.size), static_cast<long long>(r.mtime),
                          static_cast<unsigned long long>(r.hash), r.loader.c_str());
    asp2::fs::writeBytes(p, buf, size_t(n));
}

// --- the three pck edits ----------------------------------------------------

std::vector<uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

void applyPatch(pck::Pck& p, const std::string& bootstrap, const std::string& stamp) {
    std::vector<uint8_t> pb;
    if (!p.readFile(kProjectBinary, pb))
        throw Error("no project.binary inside the pck - is this really a Godot game?");

    std::vector<pck::Setting> settings = pck::ecfgParse(pb), next;
    next.emplace_back(kAutoloadKey, pck::encString(std::string("*") + kBootstrapRes)); // autoload runs first
    for (auto& s : settings)
        if (s.first != kAutoloadKey) next.push_back(std::move(s));

    p.put(kProjectBinary, pck::ecfgBuild(next));
    p.put(kBootstrapRes, bytes(bootstrap));
    p.put(kStampRes, bytes(stamp));
}

void verify(const asp2::fs::Path& file, const std::string& bootstrap) {
    pck::Pck check(file);
    std::vector<uint8_t> got;
    if (!check.readFile(kBootstrapRes, got) || std::string(got.begin(), got.end()) != bootstrap)
        throw Error("verification failed: bootstrap script not found in the modded pck");
    std::vector<uint8_t> pb;
    check.readFile(kProjectBinary, pb);
    auto s = pck::ecfgParse(pb);
    if (s.empty() || s[0].first != kAutoloadKey)
        throw Error("verification failed: autoload is not first");
}

void buildModded(const asp2::fs::Path& original, const asp2::fs::Path& out,
                 const std::string& bootstrap, const std::string& stamp, const LogFn& log) {
    pck::Pck p(original);
    if (log)
        log("pck: " + original.name() + "  (Godot " + std::to_string(p.version(0)) + "." +
            std::to_string(p.version(1)) + "." + std::to_string(p.version(2)) + ", pack format " +
            std::to_string(p.fmt()) + ", " + std::to_string(p.entries().size()) + " files)");
    applyPatch(p, bootstrap, stamp);
    if (p.fmt() >= 3) {
        if (!asp2::fs::copyFile(original, out)) throw Error("cannot copy the original pck");
        p.appendPatchV3(out);
    } else {
        p.rewriteV2(out);
    }
    verify(out, bootstrap);
}

bool endsWithModded(const asp2::fs::Path& p) {
    std::string n = lower(p.name());
    return n.size() >= 11 && n.compare(n.size() - 11, 11, ".modded.pck") == 0;
}

} // namespace Detail

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

asp2::fs::Path Paths::record() const {
    asp2::fs::Path r = modded.str() + ".record";
    return r;
}

std::optional<Paths> PathsFromExe(const asp2::fs::Path& gameExe) {
    asp2::fs::Path dir = gameExe.parent();
    if (dir.empty()) dir = asp2::fs::Path(".");
    asp2::fs::Path original;

    asp2::fs::Path guess = dir / (gameExe.stem() + ".pck");
    if (asp2::fs::isFile(guess) && !Detail::endsWithModded(guess)) {
        original = guess;
    } else {
        std::vector<asp2::fs::Path> pcks;
        for (const asp2::fs::Path& p : asp2::fs::listDir(dir))
            if (p.ext() == ".pck" && !Detail::endsWithModded(p)) pcks.push_back(p);
        if (pcks.empty()) return std::nullopt;
        original = pcks.front(); // listDir is sorted
    }
    Paths out;
    out.original = original;
    out.modded = original.parent() / (original.stem() + ".modded.pck");
    return out;
}

// ---------------------------------------------------------------------------
// Loader folders (mods/ + gd/ with their examples)
// ---------------------------------------------------------------------------

void EnsureLoaderFolders(const asp2::fs::Path& gameDir, const LogFn& log) {
    // Example mod (mods/console) - same rule as before: create it only once.
    asp2::fs::Path mods = gameDir / "mods";
    asp2::fs::makeDirs(mods);
    asp2::fs::Path console = mods / "console";
    if (!asp2::fs::exists(console)) {
        if (asp2::fs::makeDirs(console)) {
            asp2::fs::writeBytes(console / "mod.json", kExampleModJson, kExampleModJsonSize);
            asp2::fs::writeBytes(console / "main.gd", kExampleModGd, kExampleModGdSize);
            if (log) log("wrote example mod -> " + console.str());
        }
    }

    // Loose scripts (gd/) - loaded automatically, no mod.json needed. Write the
    // example only when the folder is new or has no scripts at all.
    asp2::fs::Path gd = gameDir / "gd";
    if (!asp2::fs::exists(gd) || asp2::fs::listFilesRecursive(gd, ".gd").empty()) {
        if (asp2::fs::makeDirs(gd)) {
            asp2::fs::writeBytes(gd / "example.gd", kExampleLooseGd, kExampleLooseGdSize);
            if (log) log("wrote example script -> " + (gd / "example.gd").str());
        }
    }
}

// ---------------------------------------------------------------------------
// The modded pck
// ---------------------------------------------------------------------------

Result EnsureModdedPck(const Paths& paths, const LogFn& log) {
    Result r;
    asp2::fs::Path tmp(paths.modded.str() + ".tmp");
    try {
        if (!asp2::fs::isFile(paths.original))
            throw Error("original pck not found: " + paths.original.str());

        const std::string bootstrap = Detail::finalizeBootstrap();
        const std::string stamp =
            std::string(kVersion) + ":" +
            Detail::hex64(asp2::fs::hashBytes(asp2::fs::kHashSeed,
                                              reinterpret_cast<const uint8_t*>(bootstrap.data()),
                                              bootstrap.size()));

        Detail::Record now;
        now.size = asp2::fs::fileSize(paths.original);
        now.mtime = asp2::fs::mtime(paths.original);
        now.loader = stamp;

        Detail::Record saved;
        const bool have = Detail::loadRecord(paths.record(), saved) &&
                          asp2::fs::isFile(paths.modded) && saved.loader == stamp;

        // Tier 1: size + mtime unchanged -> nothing to do.
        if (have && saved.size == now.size && saved.mtime == now.mtime) {
            r.ok = true;
            return r;
        }

        // Tier 2: hash the original; Steam may touch the timestamp without changing content.
        now.hash = asp2::fs::hashFile(paths.original);
        if (now.hash == 0) throw Error("cannot read " + paths.original.str());
        if (have && saved.size == now.size && saved.hash == now.hash) {
            Detail::saveRecord(paths.record(), now);
            r.ok = true;
            return r;
        }

        // Rebuild: original changed, first run, or bootstrap changed.
        if (log) log(have ? "game pck changed - rebuilding modded copy" : "building modded copy");
        asp2::fs::makeDirs(paths.modded.parent());
        asp2::fs::removeTree(tmp);
        Detail::buildModded(paths.original, tmp, bootstrap, stamp, log);
        asp2::fs::remove(paths.modded);
        if (!asp2::fs::rename(tmp, paths.modded)) throw Error("cannot put the modded pck in place");
        Detail::saveRecord(paths.record(), now);
        if (log) log("patched and verified -> " + paths.modded.name());
        r.ok = true;
        r.rebuilt = true;
    } catch (const std::exception& e) {
        asp2::fs::removeTree(tmp);
        r.ok = false;
        r.message = e.what();
    }
    return r;
}

void RemoveModded(const Paths& paths, const LogFn& log) {
    if (asp2::fs::remove(paths.modded) && log) log("removed " + paths.modded.str());
    if (asp2::fs::remove(paths.record()) && log) log("removed " + paths.record().str());
}

} // namespace mpml
