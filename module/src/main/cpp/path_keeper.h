#pragma once
#include "susfs_backend.h"
#include <sys/types.h>

namespace lh {
// References are acquired BEFORE registration and never dropped during handoff.
// O_PATH holds dentries/inodes, without reading/pinning file contents in RAM.
class PathPins {
public:
    struct Pin { std::string path; int fd; dev_t dev; ino_t ino; };
    ~PathPins();
    PathPins() = default;
    PathPins(const PathPins &) = delete;
    PathPins &operator=(const PathPins &) = delete;
    bool acquire(const PathPlan &plan, std::string &error);
    bool matches(const std::string &path, std::string &error) const;
    bool current() const;
    const std::vector<Pin> &files() const { return files_; }
private:
    std::vector<Pin> files_;
};

bool write_path_runtime(const char *name, const std::string &text);
// Starts one idle, root-private keeper and waits for its publication/ready ACK.
// The child inherits the lock and pins; the caller can then close its copies.
bool start_path_keeper(const PathPins &pins, int lockfd, const Config &config,
                       const std::string &receipt, const std::string &log, std::string &error);
std::string native_path_status();
bool stop_path_keeper(std::string &error);
} // namespace lh
