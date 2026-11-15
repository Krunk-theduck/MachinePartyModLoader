// mpml::logger - one tiny logger for both the launcher and the proxy DLL.
// Writes every line to stderr (launcher console) and to the file set with
// setFile(). Thread-safe, since the proxy's CreateFileW hook logs from the
// game's threads. All disk access goes through asp2.
#pragma once
#include <mutex>
#include <string>

#include "asp2/fs.h"

namespace mpml {

class logger {
public:
    logger() = default;
    ~logger();
    logger(const logger&) = delete;
    logger& operator=(const logger&) = delete;

    // Truncates/creates the log file. Call once at startup (after the folder
    // exists). Returns false when the file cannot be opened - logging then
    // stays on stderr only.
    bool setFile(const asp2::fs::Path& path);

    // One line; a newline is appended for you.
    void log(const std::string& line);

private:
    asp2::fs::File file_;
    std::mutex mutex_;
};

} // namespace mpml
