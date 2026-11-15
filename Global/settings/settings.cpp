#include "settings.hpp"

#include <asp/fs.hpp>

#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <type_traits>

#include <Dirs/paths.hpp>

namespace mpml::settings {

namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }

    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }

    return value;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }

    return true;
}

bool parseValue(std::string_view text, bool& out) {
    if (equalsIgnoreCase(text, "true") ||
        equalsIgnoreCase(text, "yes") ||
        equalsIgnoreCase(text, "on") ||
        text == "1") {
        out = true;
        return true;
    }

    if (equalsIgnoreCase(text, "false") ||
        equalsIgnoreCase(text, "no") ||
        equalsIgnoreCase(text, "off") ||
        text == "0") {
        out = false;
        return true;
    }

    return false;
}

bool parseValue(std::string_view text, int& out) {
    int value{};

    const auto [ptr, ec] = std::from_chars(
        text.data(),
        text.data() + text.size(),
        value
    );

    if (ec != std::errc{} || ptr != text.data() + text.size())
        return false;

    out = value;
    return true;
}

bool parseValue(std::string_view text, double& out) {
    std::string copy{text};
    char* end = nullptr;

    const double value = std::strtod(copy.c_str(), &end);

    if (end != copy.c_str() + copy.size() || end == copy.c_str())
        return false;

    if (!std::isfinite(value))
        return false;

    out = value;
    return true;
}

bool parseValue(std::string_view text, std::string& out) {
    // Allow quoted strings, e.g. profile_name = "my profile".
    if (text.size() >= 2 &&
        ((text.front() == '"' && text.back() == '"') ||
         (text.front() == '\'' && text.back() == '\''))) {
        text.remove_prefix(1);
        text.remove_suffix(1);
    }

    out.assign(text);
    return true;
}

template <typename T>
std::string formatValue(const T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_same_v<T, std::string>) {
        return "\"" + value + "\"";
    } else {
        return std::to_string(value);
    }
}

std::string makeDefaultConfig() {
    Values defaults;

    std::string result;
    result += "# MachinePartyModLoader configuration\n";
    result += "# Edit values below, then restart the loader.\n\n";

#define APPEND_SETTING(Type, Name, Default) \
    result += #Name " = ";                  \
    result += formatValue(defaults.Name);   \
    result += "\n";

    MPML_SETTINGS(APPEND_SETTING)

#undef APPEND_SETTING

    return result;
}

} // namespace

Values parse(std::string_view contents) {
    Values values;

    while (!contents.empty()) {
        const auto newline = contents.find('\n');

        auto line = contents.substr(0, newline);

        if (newline == std::string_view::npos)
            contents = {};
        else
            contents.remove_prefix(newline + 1);

        line = trim(line);

        if (line.empty() || line.starts_with('#') || line.starts_with(';'))
            continue;

        const auto equals = line.find('=');

        if (equals == std::string_view::npos)
            continue;

        const auto key = trim(line.substr(0, equals));
        const auto value = trim(line.substr(equals + 1));

        if (key.empty())
            continue;

#define PARSE_SETTING(Type, Name, Default) \
        if (equalsIgnoreCase(key, #Name)) { \
            (void)parseValue(value, values.Name); \
        } else

        MPML_SETTINGS(PARSE_SETTING) {
            // Unknown setting: ignore it.
        }

#undef PARSE_SETTING
    }

    return values;
}

Values load(const asp::fs::Path& file) {
    auto contents = asp::fs::readToString(file);

    if (contents.isErr())
        return {};

    return parse(contents.unwrapOrDefault());
}

asp::fs::Path fileFor(
    const asp::fs::Path& gameDir,
    std::string_view fileName
) {
    return dirs::config(gameDir) / fileName;
}

void ensureDefault(const asp::fs::Path& file) {
    if (file.isFile())
        return;

    const auto parent = file.parent();

    if (parent.has_value())
        asp::fs::createDirAll(parent.value());

    const auto contents = makeDefaultConfig();
    (void)asp::fs::write(file, contents);
}

} // namespace mpml::settings