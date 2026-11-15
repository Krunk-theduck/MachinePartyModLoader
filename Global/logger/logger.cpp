#include "logger/logger.hpp"

namespace mpml {

logger::~logger() {
    std::lock_guard<std::mutex> guard(mutex_);
    file_.close();
}

bool logger::setFile(const asp2::fs::Path& path) {
    std::lock_guard<std::mutex> guard(mutex_);
    file_.close();
    try {
        file_.open(path, "w");
    } catch (...) {
        return false;
    }
    return file_.isOpen();
}

void logger::log(const std::string& line) {
    std::lock_guard<std::mutex> guard(mutex_);
    std::fprintf(stderr, "  %s\n", line.c_str());
    if (file_.isOpen()) {
        try {
            file_.write(line.data(), line.size());
            file_.write("\n", 1);
            file_.flush();
        } catch (...) {
            // a full disk must never take the game down
        }
    }
}

} // namespace mpml
