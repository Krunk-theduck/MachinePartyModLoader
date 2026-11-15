#include "gdcheck.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <miniz.h>

#include "util/util.hpp"
#include "version.h"

namespace mpml {
namespace gd {
namespace Detail {

// ---------------------------------------------------------------------------
// Tokenizer: source bytes -> tokens grouped into statements (logical lines).
// Mirrors the engine's rules: newlines only end statements outside brackets,
// '\' joins lines, strings/comments are opaque, leading whitespace = indent.
// ---------------------------------------------------------------------------

enum TokKind { kWord, kNumber, kString, kOp, kAnnotation };

struct Tok {
    TokKind kind = kOp;
    std::string text;
    int line = 0;
    int depth = 0; // bracket nesting level the token sits at
};

struct Stmt {
    int line = 0; // first physical line
    int indent = 0;
    bool sawTab = false;   // indentation used a tab
    bool sawSpace = false; // indentation used a space
    std::vector<Tok> toks;
};

bool isWordChar(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return u >= 0x80 || c == '_' || std::isalnum(u) != 0;
}

bool isOpChar(char c) {
    return std::strchr("+-*/%=<>!&|^~:,.$@?;", c) != nullptr && c != '\0';
}

struct Scanner {
    const std::string& src;
    std::vector<Diag>& diags;
    size_t pos = 0;
    int line = 1;
    int depth = 0;
    std::vector<std::pair<char, int>> open; // bracket char + the line it opened
    Stmt cur;

    explicit Scanner(const std::string& s, std::vector<Diag>& d) : src(s), diags(d) {
        if (src.size() >= 3 && src[0] == '\xEF' && src[1] == '\xBB' && src[2] == '\xBF') pos = 3; // BOM
    }

    bool eof() const { return pos >= src.size(); }
    char cur_c() const { return src[pos]; }
    char at(size_t off) const { return pos + off < src.size() ? src[pos + off] : '\0'; }
    void err(int ln, const std::string& m) { diags.push_back({ln, true, m}); }

    void push(Tok t) {
        if (cur.toks.empty()) cur.line = t.line;
        cur.toks.push_back(std::move(t));
    }

    // Brackets are always single-char tokens and drive the nesting depth.
    void scanBracket(char c) {
        ++pos; // the caller passed cur_c()
        Tok t;
        t.kind = kOp;
        t.text = std::string(1, c);
        t.line = line;
        t.depth = depth;
        if (c == '(' || c == '[' || c == '{') {
            open.emplace_back(c, line);
            ++depth;
        } else {
            if (open.empty()) {
                err(line, std::string("unbalanced '") + c + "' - nothing is open to close");
            } else {
                const char o = open.back().first;
                const int ol = open.back().second;
                const char want = o == '(' ? ')' : o == '[' ? ']' : '}';
                if (c != want)
                    err(line, std::string("mismatched '") + c + "' - '" + o + "' opened on line " +
                                  std::to_string(ol));
                open.pop_back();
                --depth;
            }
        }
        push(std::move(t));
    }

    void scanString() {
        const char q = cur_c();
        const int startLine = line;
        Tok t;
        t.kind = kString;
        t.line = line;
        t.depth = depth;
        if (at(1) == q && at(2) == q) { // triple quoted
            pos += 3;
            bool closed = false;
            while (!eof()) {
                if (cur_c() == '\\' && pos + 1 < src.size()) { pos += 2; continue; }
                if (cur_c() == q && at(1) == q && at(2) == q) { pos += 3; closed = true; break; }
                if (cur_c() == '\n') ++line;
                ++pos;
            }
            if (!closed) err(startLine, std::string("unterminated triple-quoted string (missing ") + q + q + q + ")");
        } else {
            ++pos; // opening quote
            bool closed = false;
            while (!eof()) {
                const char c = cur_c();
                if (c == '\n') break;
                ++pos;
                if (c == '\\') { // the escaped byte is consumed, even a newline
                    if (!eof() && cur_c() == '\n') ++line;
                    ++pos;
                    continue;
                }
                if (c == q) { closed = true; break; }
            }
            if (!closed) err(startLine, "unterminated string (missing closing quote)");
        }
        push(std::move(t));
    }

    void scanRun() { // operators / punctuation, greedy up to 3 chars
        Tok t;
        t.kind = kOp;
        t.line = line;
        t.depth = depth;
        while (!eof() && t.text.size() < 3 && isOpChar(cur_c())) {
            if (cur_c() == '@' && !t.text.empty()) break; // '@' starts annotations
            t.text += cur_c();
            ++pos;
        }
        if (t.text.empty()) { // unknown byte: consume it opaquely (never stall)
            t.text = cur_c();
            ++pos;
        }
        if (t.text == "@" && !eof() && isWordChar(cur_c())) { // @annotation
            t.kind = kAnnotation;
            while (!eof() && isWordChar(cur_c())) { t.text += cur_c(); ++pos; }
        }
        push(std::move(t));
    }

    void scanWordOrNumber() {
        Tok t;
        t.kind = (cur_c() >= '0' && cur_c() <= '9') ? kNumber : kWord;
        t.line = line;
        t.depth = depth;
        const bool num = t.kind == kNumber;
        while (!eof()) {
            const char c = cur_c();
            if (isWordChar(c) || (num && (c == '.' || c == '_'))) { t.text += c; ++pos; continue; }
            break;
        }
        push(std::move(t));
    }

    void run(std::vector<Stmt>& out) {
        while (!eof()) {
            const char c = cur_c();
            if (c == '\n') {
                ++pos;
                ++line;
                if (depth == 0) {
                    if (!cur.toks.empty()) out.push_back(std::move(cur));
                    cur = Stmt(); // flush the statement, or drop a blank/comment line's indent
                }
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\r') {
                if (c != '\r' && cur.toks.empty() && depth == 0) {
                    cur.indent += 1;
                    if (c == '\t') cur.sawTab = true;
                    else cur.sawSpace = true;
                }
                ++pos;
                continue;
            }
            if (c == '#') { // comment to end of line
                while (!eof() && cur_c() != '\n') ++pos;
                continue;
            }
            if (c == '\\' && at(1) == '\n') { // line continuation
                pos += 2;
                ++line;
                continue;
            }
            if (c == '"' || c == '\'') { scanString(); continue; }
            if (c == '(' || c == '[' || c == '{' || c == ')' || c == ']' || c == '}') { scanBracket(c); continue; }
            if (isWordChar(c)) { scanWordOrNumber(); continue; }
            scanRun();
        }
        if (!cur.toks.empty()) out.push_back(std::move(cur));
        if (!open.empty())
            err(open.back().second,
                std::string("unclosed '") + open.back().first + "' - the bracket is never closed");
    }
};

// ---------------------------------------------------------------------------
// Block pass: statements -> indentation frames -> the actual findings.
// ---------------------------------------------------------------------------

struct Frame {
    int indent = 0;
    enum Kind { Top, Cls, Loop, If, Else, Plain, Func } kind = Top;
    Kind lastChild = Top; // header kind of the most recently closed child block
    std::map<std::string, int> funcs;    // func name -> first line (duplicate check)
    std::map<std::string, char> members; // var/const/signal name -> kind
};

struct Checker {
    std::vector<Diag>& diags;
    std::vector<Frame> frames;
    // pending: the last statement ended with ':' and awaits an indented body
    bool pending = false;
    Frame::Kind pendingKind = Frame::Plain;
    int pendingIndent = 0;
    int pendingLine = 0;
    std::string pendingWhat;
    bool seenExtends = false;
    bool seenClassName = false;

    explicit Checker(std::vector<Diag>& d) : diags(d) { frames.push_back(Frame{}); }

    void err(int ln, const std::string& m) { diags.push_back({ln, true, m}); }
    void warn(int ln, const std::string& m) { diags.push_back({ln, false, m}); }

    Frame::Kind headerKind(const std::string& w) {
        if (w == "if" || w == "elif") return Frame::If;
        if (w == "else") return Frame::Else;
        if (w == "for" || w == "while") return Frame::Loop;
        if (w == "func") return Frame::Func;
        if (w == "class") return Frame::Cls;
        return Frame::Plain; // match, patterns, properties, lambdas
    }

    bool classContext() const {
        const Frame::Kind k = frames.back().kind;
        return k == Frame::Top || k == Frame::Cls;
    }

    void check(const Stmt& st) {
        // 1) a pending header collects its body
        if (pending) {
            pending = false;
            if (st.indent > pendingIndent) {
                Frame f;
                f.indent = st.indent;
                f.kind = pendingKind;
                frames.push_back(std::move(f));
            } else {
                err(pendingLine, "expected an indented block after '" + pendingWhat + "' (line " +
                                     std::to_string(pendingLine) + ")");
            }
        }

        // 2) align the frame stack with this line's indentation
        bool indentError = false;
        if (st.indent > frames.back().indent) {
            err(st.line, "unexpected indentation");
            indentError = true;
            Frame f; // recovery: keep parsing the deeper lines as a block
            f.indent = st.indent;
            f.kind = Frame::Plain;
            frames.push_back(f);
        } else {
            while (frames.size() > 1 && frames.back().indent > st.indent) {
                const Frame::Kind closed = frames.back().kind;
                frames.pop_back();
                frames.back().lastChild = closed;
            }
            if (frames.back().indent != st.indent) {
                err(st.line, "unindent does not match any outer indentation level");
                indentError = true;
            }
        }
        if (st.sawTab && st.sawSpace) err(st.line, "mixed tabs and spaces in indentation");

        // statement shape
        const std::vector<Tok>& toks = st.toks;
        const bool ann = toks[0].kind == kAnnotation;
        const std::string first = ann ? toks[0].text : toks[0].kind == kWord ? toks[0].text : std::string();
        int colonAt = -1;
        for (size_t i = 0; i < toks.size(); ++i)
            if (toks[i].kind == kOp && toks[i].text == ":" && toks[i].depth == 0) colonAt = int(i);
        const bool endsColon = colonAt == int(toks.size()) - 1;
        const bool inClass = classContext();

        // 3) break / continue need a loop around them
        if (first == "break" || first == "continue") {
            const bool inLoop = std::any_of(frames.begin(), frames.end(),
                                            [](const Frame& f) { return f.kind == Frame::Loop; });
            if (!inLoop) err(st.line, "'" + first + "' outside of a loop");
        }

        // 4) elif / else chains
        if (first == "elif" || first == "else") {
            if (frames.back().lastChild != Frame::If)
                err(st.line, "'" + first + "' without a matching 'if'");
            if (colonAt < 0) err(st.line, "expected ':' after '" + first + "'");
        } else if (!indentError) {
            // 5) what may appear at class level
            static const char* const kClassLevel[] = {"class_name", "extends", "enum",  "signal",
                                                      "const",      "var",     "static", "func",
                                                      "class",      "pass"};
            const bool okClass = ann ||
                                 std::any_of(std::begin(kClassLevel), std::end(kClassLevel),
                                             [&](const char* w) { return first == w; });
            if (inClass && !okClass)
                err(st.line, "unexpected statement at class level: '" + first +
                                 "' (only var/const/func/signal/enum/class/extends/class_name/@annotations are allowed)");

            // 6) placement rules
            if (!inClass) {
                if (first == "func" && toks.size() > 1 && toks[1].kind == kWord)
                    err(st.line, "GDScript does not allow nested functions - use a lambda instead: func(x): ...");
                if (first == "extends" || first == "class_name")
                    err(st.line, "'" + first + "' is only allowed at the top level of the file");
                if (first == "static")
                    err(st.line, "'static' is only allowed at class level");
                if (first == "signal" || first == "enum" || first == "class")
                    warn(st.line, "'" + first + "' is only allowed at class level");
            } else {
                if (first == "extends" || first == "class_name") {
                    const bool dup = first == "extends" ? seenExtends : seenClassName;
                    if (dup) err(st.line, "duplicate '" + first + "'");
                    if (first == "extends") seenExtends = true; else seenClassName = true;
                }
                if (first == "class_name") {
                    for (const Tok& t : toks)
                        if (t.kind == kWord && t.text == "extends" && t.depth == 0) {
                            if (seenExtends) err(st.line, "duplicate 'extends'");
                            seenExtends = true;
                        }
                }
                if (first == "static" && (toks.size() < 2 || toks[1].kind != kWord ||
                                          (toks[1].text != "var" && toks[1].text != "func")))
                    err(st.line, "'static' must be followed by 'var' or 'func'");

                // duplicate members within the same class
                if (first == "func" || first == "var" || first == "const" || first == "signal") {
                    const bool isFunc = first == "func";
                    const size_t nameAt = 1;
                    if (toks.size() > nameAt && toks[nameAt].kind == kWord) {
                        const std::string& name = toks[nameAt].text;
                        Frame& cls = frames.back();
                        if (isFunc) {
                            const auto it = cls.funcs.find(name);
                            if (it != cls.funcs.end())
                                err(st.line, "duplicate function '" + name + "' (first defined on line " +
                                                 std::to_string(it->second) + ")");
                            else
                                cls.funcs[name] = st.line;
                        }
                        const auto mt = cls.members.find(name);
                        if (mt != cls.members.end()) {
                            if (mt->second != 'f' || !isFunc)
                                warn(st.line, "duplicate member '" + name + "'");
                        } else {
                            cls.members[name] = isFunc ? 'f' : first[0];
                        }
                    }
                }
            }

            // 7) classic typo: assignment in a condition
            if (first == "if" || first == "elif" || first == "while")
                for (const Tok& t : toks)
                    if (t.kind == kOp && t.text == "=" && t.depth == 0) {
                        err(st.line, "assignment in condition (use == to compare)");
                        break;
                    }

            // 8) headers must end with ':'
            const bool header = first == "if" || first == "elif" || first == "else" || first == "for" ||
                                first == "while" || first == "match" || first == "func" || first == "class";
            if (header && colonAt < 0) err(st.line, "expected ':' after '" + first + "'");
        }

        // 9) every line ending with ':' opens a block (if/for/func/match
        //    patterns/properties/lambdas) unless it has an inline body
        const bool header = first == "if" || first == "elif" || first == "else" || first == "for" ||
                            first == "while" || first == "match" || first == "func" || first == "class";
        if (endsColon) {
            pending = true;
            pendingKind = headerKind(first);
            pendingIndent = st.indent;
            pendingLine = st.line;
            pendingWhat = toks[0].kind == kString ? "<match pattern>" : first.empty() ? toks[0].text : first;
        } else if (header && colonAt >= 0) {
            // an inline body ('if x: return') still counts as a header for
            // the elif/else chain that may follow it
            frames.back().lastChild = headerKind(first);
        }
    }

    void finish() {
        if (pending)
            err(pendingLine, "expected an indented block after '" + pendingWhat + "' (line " +
                                 std::to_string(pendingLine) + ")");
    }
};

std::vector<Diag> checkStatements(const std::vector<Stmt>& stmts) {
    std::vector<Diag> diags;
    Checker chk(diags);
    for (const Stmt& st : stmts) chk.check(st);
    chk.finish();
    return diags;
}

// --- verdict cache: cache/compile/<version>-<hash>.check --------------------

std::string hex16(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

bool loadCached(const asp::fs::Path& p, std::vector<Diag>& out) {
    auto res = asp::fs::readToString(p);
    if (res.isErr()) return false;
    std::string s = std::move(res).unwrap();
    std::vector<Diag> parsed;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find('\n', start);
        if (end == std::string::npos) end = s.size();
        const std::string ln = s.substr(start, end - start);
        start = end + 1;
        if (ln.empty()) { if (end == s.size()) break; continue; }
        Diag d;
        d.error = ln[0] == 'E';
        if (ln[0] != 'E' && ln[0] != 'W') return false;
        const size_t sp1 = ln.find(' ');
        if (sp1 == std::string::npos) return false;
        const size_t sp2 = ln.find(' ', sp1 + 1);
        if (sp2 == std::string::npos) return false;
        d.line = std::atoi(ln.c_str() + sp1 + 1);
        d.msg = ln.substr(sp2 + 1);
        parsed.push_back(std::move(d));
        if (end == s.size()) break;
    }
    out = std::move(parsed);
    return true;
}

void saveCached(const asp::fs::Path& p, const std::vector<Diag>& diags) {
    std::string s;
    for (const Diag& d : diags) {
        s += d.error ? "E " : "W ";
        s += std::to_string(d.line);
        s += ' ';
        s += d.msg;
        s += '\n';
    }
    (void)asp::fs::write(p, s);
}

std::string describe(const Diag& d) {
    return std::string(d.error ? "error: " : "warning: ") + d.msg;
}

} // namespace Detail

// The public API below speaks in Detail's types.
using namespace Detail;

// --- zip mods: same check for the .gd files packed inside a .zip ------------

// Zip verdicts cache in one file per zip:
//   F <entry name>            starts the block of one packed script
//   E|W <line> <message>      that script's findings
bool loadZipCached(const asp::fs::Path& p, std::vector<FileResult>& out) {
    auto res = asp::fs::readToString(p);
    if (res.isErr()) return false;
    const std::string s = std::move(res).unwrap();
    std::vector<FileResult> parsed;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find('\n', start);
        if (end == std::string::npos) end = s.size();
        const std::string ln = s.substr(start, end - start);
        start = end + 1;
        if (ln.empty()) { if (end == s.size()) break; continue; }
        if (ln[0] == 'F') {
            FileResult r;
            r.path = asp::fs::Path{ln.substr(2)};
            parsed.push_back(std::move(r));
            continue;
        }
        if (parsed.empty() || (ln[0] != 'E' && ln[0] != 'W')) return false;
        Diag d;
        d.error = ln[0] == 'E';
        const size_t sp1 = ln.find(' ');
        if (sp1 == std::string::npos) return false;
        const size_t sp2 = ln.find(' ', sp1 + 1);
        if (sp2 == std::string::npos) return false;
        d.line = std::atoi(ln.c_str() + sp1 + 1);
        d.msg = ln.substr(sp2 + 1);
        parsed.back().diags.push_back(std::move(d));
        if (end == s.size()) break;
    }
    out = std::move(parsed);
    return true;
}

void saveZipCached(const asp::fs::Path& p, const std::vector<FileResult>& results) {
    std::string s;
    for (const FileResult& r : results) {
        s += "F " + r.path.string() + "\n";
        for (const Diag& d : r.diags) {
            s += d.error ? "E " : "W ";
            s += std::to_string(d.line);
            s += ' ';
            s += d.msg;
            s += '\n';
        }
    }
    (void)asp::fs::write(p, s);
}

// Checks every .gd file packed inside `zipPath` (sorted by internal name).
// Results are reported as "<zip>!<internal path>".
void checkZip(const asp::fs::Path& zipPath, const asp::fs::Path& cacheDir, bool useCache,
              std::vector<FileResult>& out) {
    const asp::fs::Path cdir = cacheDir / "compile";
    std::string key;
    if (useCache) {
        const uint64_t h = util::hashFile(zipPath);
        if (h != 0) {
            key = std::string(kVersion) + "-" + hex16(h);
            const asp::fs::Path cp = cdir / (key + ".zipcheck");
            std::vector<FileResult> replay;
            if (cp.isFile() && loadZipCached(cp, replay)) {
                for (FileResult& r : replay) r.cached = true;
                out.insert(out.end(), std::make_move_iterator(replay.begin()),
                           std::make_move_iterator(replay.end()));
                return;
            }
        }
    }

    // Read the archive through asp2 (unicode-safe on Windows, and it keeps
    // every disk access in one place), then hand the bytes to miniz. The
    // buffer must outlive the reader - it does (same scope).
    auto bytesRes = asp::fs::read(zipPath);
    std::vector<uint8_t> bytes = bytesRes.isErr() ? std::vector<uint8_t>{} : std::move(bytesRes).unwrap();
    mz_zip_archive zip = {};
    if (bytes.empty() || !mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) {
        FileResult r;
        r.path = zipPath;
        r.diags.push_back({0, true, "cannot open the zip - is it a valid .zip mod archive?"});
        out.push_back(std::move(r));
        return;
    }

    std::vector<std::pair<std::string, int>> scripts; // internal name -> entry index
    const int count = mz_zip_reader_get_num_files(&zip);
    for (int i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        const std::string name = st.m_filename;
        if (name.empty() || name.back() == '/') continue;
        const size_t slash = name.find_last_of('/');
        const std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
        if (base.size() > 3 && base.compare(base.size() - 3, 3, ".gd") == 0)
            scripts.emplace_back(name, i);
    }
    std::sort(scripts.begin(), scripts.end());

    for (const auto& [name, idx] : scripts) {
        FileResult r;
        r.path = asp::fs::Path{zipPath.string() + "!" + name};
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, idx, &size, 0);
        if (!data) {
            r.diags.push_back({0, true, "cannot extract the script from the zip"});
        } else {
            r.diags = CheckSource(std::string(static_cast<const char*>(data), size));
            mz_free(data);
        }
        out.push_back(std::move(r));
    }
    mz_zip_reader_end(&zip);

    if (useCache && !key.empty() && !scripts.empty()) {
        (void)asp::fs::createDirAll(cdir);
        saveZipCached(cdir / (key + ".zipcheck"), out);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<Diag> CheckSource(const std::string& src) {
    std::vector<Diag> diags;
    Scanner sc(src, diags);
    std::vector<Stmt> stmts;
    sc.run(stmts);
    std::vector<Diag> parsed = checkStatements(stmts);
    diags.insert(diags.end(), parsed.begin(), parsed.end());
    std::stable_sort(diags.begin(), diags.end(),
                     [](const Diag& a, const Diag& b) { return a.line < b.line; });
    return diags;
}

// --- per-file + per-zip checking --------------------------------------------

static void checkOneFile(const asp::fs::Path& f, const asp::fs::Path& cdir, bool useCache,
                         std::string& cacheKey, std::vector<FileResult>& out) {
    FileResult r;
    r.path = f;
    if (useCache) {
        const uint64_t h = util::hashFile(f);
        if (h != 0) {
            cacheKey = std::string(kVersion) + "-" + hex16(h);
            const asp::fs::Path cp = cdir / (cacheKey + ".check");
            if (cp.isFile() && loadCached(cp, r.diags)) {
                r.cached = true;
                out.push_back(std::move(r));
                return;
            }
        }
    }
    auto res = asp::fs::readToString(f);
    if (res.isErr()) {
        r.diags.push_back({0, true, "cannot read file: " + util::errText(std::move(res))});
        out.push_back(std::move(r));
        return;
    }
    r.diags = CheckSource(std::move(res).unwrap());
    if (useCache && !cacheKey.empty()) {
        (void)asp::fs::createDirAll(cdir);
        saveCached(cdir / (cacheKey + ".check"), r.diags);
    }
    out.push_back(std::move(r));
}

std::vector<FileResult> CheckTree(const asp::fs::Path& root, const asp::fs::Path& cacheDir, bool useCache) {
    std::vector<FileResult> out;
    const asp::fs::Path cdir = cacheDir / "compile";
    const std::string rootExt(root.extension().value_or(""));
    if (root.isFile()) {
        std::string key;
        if (rootExt == "gd") checkOneFile(root, cdir, useCache, key, out);
        else if (rootExt == "zip") checkZip(root, cacheDir, useCache, out);
        return out;
    }
    if (!root.isDir()) return out;

    // Folder root: every .gd script (recursively) plus every .zip mod found on
    // the way. mods/ holds both shapes, gd/ only ever holds loose scripts.
    for (const asp::fs::Path& f : util::listFilesRecursive(root, ".gd")) {
        std::string key;
        checkOneFile(f, cdir, useCache, key, out);
    }
    for (const asp::fs::Path& z : util::listFilesRecursive(root, ".zip"))
        checkZip(z, cacheDir, useCache, out);
    return out;
}

int CheckRoots(const std::vector<asp::fs::Path>& roots, const asp::fs::Path& cacheDir, const LogFn& log,
               bool writeReport, bool useCache) {
    std::vector<FileResult> all;
    for (const asp::fs::Path& root : roots) {
        std::vector<FileResult> part = CheckTree(root, cacheDir, useCache);
        all.insert(all.end(), part.begin(), part.end());
    }

    int errors = 0, warnings = 0, cached = 0;
    std::string report = "MPML .gd compile check (loader v" + std::string(kVersion) + ")\n";
    for (const FileResult& r : all) {
        int e = 0, w = 0;
        for (const Diag& d : r.diags) (d.error ? e : w) += 1;
        errors += e;
        warnings += w;
        if (r.cached) ++cached;
        std::string head = "  " + r.path.string();
        if (r.cached) head += " (cached)";
        head += e || w ? " : " + std::to_string(e) + " error(s), " + std::to_string(w) + " warning(s)" : " : ok";
        report += head + "\n";
        for (const Diag& d : r.diags)
            report += "    line " + std::to_string(d.line) + ": " + describe(d) + "\n";
    }
    report += std::to_string(all.size()) + " script(s): " + std::to_string(errors) + " error(s), " +
              std::to_string(warnings) + " warning(s)";
    if (cached) report += ", " + std::to_string(cached) + " from cache";
    report += "\n";

    if (log) {
        for (const FileResult& r : all)
            for (const Diag& d : r.diags)
                if (d.error)
                    log(r.path.string() + ", line " + std::to_string(d.line) + ": " + describe(d));
        log(report);
    }
    if (writeReport && !cacheDir.empty()) {
        // the report is a convenience, never a reason to fail
        (void)asp::fs::createDirAll(cacheDir);
        (void)asp::fs::write(cacheDir / "compile_report.txt", report);
    }
    return errors;
}

int CheckLoaderScripts(const asp::fs::Path& gameDir, const LogFn& log, bool useCache) {
    // Loader data lives next to the game: MachinePartyModLoader/ holds the
    // report and the verdict cache (modloader.log lands there too).
    const asp::fs::Path data = gameDir / "MachinePartyModLoader";
    return CheckRoots({gameDir / "gd", gameDir / "mods"}, data / "cache", log, true, useCache);
}

} // namespace gd
} // namespace mpml
