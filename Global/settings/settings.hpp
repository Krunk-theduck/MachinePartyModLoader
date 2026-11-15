#pragma once

#include <string>
#include <string_view>

#include <asp/fs.hpp>

#define MPML_SETTINGS(Setting) \
    Setting(bool, console, false) \
    Setting(bool, logging, true) 

namespace mpml::settings {

struct Values {
#define DECLARE_SETTING(Type, Name, Default) Type Name = Default;
    MPML_SETTINGS(DECLARE_SETTING)
#undef DECLARE_SETTING
};

Values parse(std::string_view contents);

Values load(const asp::fs::Path& file);

asp::fs::Path fileFor(
    const asp::fs::Path& gameDir,
    std::string_view fileName = "loader.cfg"
);

void ensureDefault(const asp::fs::Path& file);

} // namespace mpml::settings