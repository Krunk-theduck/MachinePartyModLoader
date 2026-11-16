#include <array>
#include <regex>
#include <sstream>
#include "paths.hpp"
#include "bootstrapper/embedded.h"
static std::string getJsonVersion(const std::string &json) {
	static const std::regex pattern(
	    R"REGEX("version"\s*:\s*"([^"]+)")REGEX");

	std::smatch match;
	if (std::regex_search(json, match, pattern))
		return match[1].str();

	return {};
};

std::array<int, 3> parseVersion(const std::string &version) {
	std::array<int, 3> result{0, 0, 0};
	char dot1, dot2;

	std::istringstream stream(version);
	if (!(stream >> result[0] >> dot1 >> result[1] >> dot2 >> result[2]) || dot1 != '.' || dot2 != '.') {
		return {0, 0, 0};
	}

	return result;
};

bool isNewerVersion(
    const std::string &current,
    const std::string &latest) {
	return parseVersion(latest) > parseVersion(current);
};

void mpml::dirs::AddFolders(const asp::fs::Path &gameDir, const std::function<void(const std::string &)> &log) {
		asp::fs::Path console = dirs::mods(gameDir) / "console";
		asp::fs::Path modJson = console / "mod.json";

		if (asp::fs::exists(console)) {
			auto contents = asp::fs::readToString(modJson);

			if (contents.isOk()) {
				std::string existingJson = contents.unwrapOrDefault();

				std::string currentVersion = getJsonVersion(existingJson);

				std::string latestJson(
				    reinterpret_cast<const char *>(kExampleModJson),
				    kExampleModJsonSize);

				std::string latestVersion = getJsonVersion(latestJson);
                log("Latest version"+latestVersion);

				if (!currentVersion.empty() && !latestVersion.empty()) {
					if (isNewerVersion(
					    currentVersion,
					    latestVersion)){
                            if (log) log("updated example mod -> " + console.string());
                            (void)asp::fs::write(
                                console / "mod.json",
                                std::span{kExampleModJson, kExampleModJsonSize});

                            (void)asp::fs::write(
                                console / "main.gd",
                                std::span{kExampleModGd, kExampleModGdSize});
                        };
				}
			}
		} else {
            if (!asp::fs::exists(dirs::mods(gameDir))) {
                if (asp::fs::createDirAll(console).isOk()) {
                    (void)asp::fs::write(
                        console / "mod.json",
                        std::span{kExampleModJson, kExampleModJsonSize});

                    (void)asp::fs::write(
                        console / "main.gd",
                        std::span{kExampleModGd, kExampleModGdSize});

                    if (log)
                        log("installed example mod -> " + console.string());
                }
        }
    }
	asp::fs::Path gd = plugins(gameDir);

	if (!asp::fs::exists(gd) || util::listFilesRecursive(gd, ".gd").empty()) {
		if (asp::fs::createDirAll(gd).isOk()) {
			(void)asp::fs::write(
			    gd / "example.gd",
			    std::span{kExampleLooseGd, kExampleLooseGdSize});

			if (log)
				log("wrote example script -> " + (gd / "example.gd").string());
		}
	}

	settings::ensureDefault(settings::fileFor(gameDir, "loader.cfg"));
}