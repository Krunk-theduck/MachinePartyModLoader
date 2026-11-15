#pragma once
#include <string>
#include <asp/fs.hpp>

namespace mpml {
    namespace dirs
    {
        inline asp::fs::Path mainDir(const asp::fs::Path& gameDir) {
            return gameDir / "MachinePartyModLoader";
        };
        inline asp::fs::Path logs(const asp::fs::Path& gameDir) {
            return mainDir(gameDir) / "logs";
        };
        inline asp::fs::Path plugins(const asp::fs::Path& gameDir) {
            return mainDir(gameDir) / "plugins";
        };
       inline asp::fs::Path config(const asp::fs::Path& gameDir) {
            return mainDir(gameDir) / "configs";
        };
        inline asp::fs::Path cache(const asp::fs::Path& gameDir) {
            return mainDir(gameDir) / "cache";
        };
        inline asp::fs::Path mods(const asp::fs::Path& gameDir) {
            return gameDir / "mods";
        };
    } // namespace dirs
    
}