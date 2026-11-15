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
    auto res = asp::fs::File::create(path);
    if (res.isErr()) return false;
    file_ = std::move(res).unwrap();
    return file_.valid();
}

void logger::log(const std::string& line) {
    std::lock_guard<std::mutex> guard(mutex_);
    std::fprintf(stderr, "  %s\n", line.c_str());
    if (file_.valid()) {
        // File is unbuffered, so a failed write is the only thing to ignore
        if (file_.writeAll(line).isErr() || file_.writeAll("\n").isErr()) {
            // a full disk must never take the game down
        }
    }
}

} // namespace mpml
