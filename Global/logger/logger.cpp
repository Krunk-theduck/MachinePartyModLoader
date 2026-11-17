#include "logger/logger.hpp"

#include <cstdio>

namespace mpml {

logger::~logger() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (file_.valid()) file_.close(); // best effort: a log must never take the game down
}

bool logger::setFile(const asp::fs::Path& path) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (file_.valid()) file_.close();
    if (path.parent().has_value()) {
        if (asp::fs::createDirAll(path.parent().value()).isErr()) {
            return false;
        }
    }
    auto res = asp::fs::File::create(path);
    if (res.isErr()) return false;
    file_ = std::move(res).unwrap();
    return file_.valid();
    return false;
}

void logger::log(const std::string& line) {
    std::lock_guard<std::mutex> guard(mutex_);

    std::fprintf(stderr, "  %s\n", line.c_str());

    if (file_.valid()) {
        std::string output = line + "\n";

        auto result = file_.writeAll(output);

        if (result.isErr()) {
            std::fprintf(stderr, "Failed to write to log file!\n");
        }
    } else {
        std::fprintf(stderr, "Log file is not valid!\n");
    }
}

} // namespace mpml
