#include "settings/settings.hpp"

#include <cctype>
#include <string>
#include <vector>

#include "util/util.hpp"

namespace mpml {
namespace settings {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isTrue(const std::string& v) {
    const std::string l = lower(v);
    return l == "true" || l == "1" || l == "yes" || l == "on";
}

bool isFalse(const std::string& v) {
    const std::string l = lower(v);
    return l == "false" || l == "0" || l == "no" || l == "off";
}

} // namespace

Values parse(const std::string& text) {
    Values out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "console") {
            if (isTrue(value)) out.console = true;
            else if (isFalse(value)) out.console = false;
        }
        // unknown keys are ignored on purpose - see the header comment
    }
    return out;
}

Values load(const asp::fs::Path& file) {
    if (file.empty() || !file.isFile()) return Values{};
    auto res = asp::fs::readToString(file);
    if (res.isErr()) return Values{};
    return parse(std::move(res).unwrap());
}

asp::fs::Path fileFor(const asp::fs::Path& gameDir) {
    return gameDir / "MachinePartyModLoader" / "loader.cfg";
}

void ensureDefault(const asp::fs::Path& file) {
    if (file.empty() || asp::fs::exists(file)) return;
    if (asp::fs::createDirAll(asp::fs::Path{util::parentOf(file)}).isErr()) return;
    static const char* const kDefault =
        "# Machine Party mod loader - loader.cfg\n"
        "#\n"
        "# Simple \"key = value\" settings. Lines starting with # are comments,\n"
        "# unknown keys are ignored, and a missing file means all defaults.\n"
        "\n"
        "# console: open a debug console window next to the game while it runs\n"
        "# (Windows only). The loader's logs appear there live. Off by default:\n"
        "console = false\n";
    (void)asp::fs::write(file, std::string_view{kDefault});
}

} // namespace settings
} // namespace mpml
