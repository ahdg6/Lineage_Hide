#include "susfs_backend.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <set>
#include <signal.h>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace lh {
namespace {
constexpr size_t kMaxPaths = 512, kMaxEntries = 100000;
#ifndef LH_HOST_TEST
constexpr const char *kKsud = "/data/adb/ksud";
#endif
using Clock = std::chrono::steady_clock;

bool trusted_file(const struct stat &st) {
#ifndef LH_HOST_TEST
    if (st.st_uid != 0) return false;
#endif
    return S_ISREG(st.st_mode);
}

// Reject symlinks in every component, including intermediate directories.
bool existing_ro_file(const std::string &path, std::string &error) {
    if (!valid_ro_path(path)) { error = "Invalid read-only path: " + path; return false; }
    size_t pos = 1;
    while (true) {
        pos = path.find('/', pos);
        std::string part = pos == std::string::npos ? path : path.substr(0, pos);
        struct stat st{};
        if (lstat(part.c_str(), &st) < 0) {
            if (errno != ENOENT && errno != ENOTDIR) error = "Cannot inspect path: " + path;
            return false;
        }
        if (S_ISLNK(st.st_mode)) { error = "Symlink path rejected: " + path; return false; }
        if (pos == std::string::npos) {
            if (!trusted_file(st)) error = "Not a root-owned regular file: " + path;
            return trusted_file(st);
        }
        if (!S_ISDIR(st.st_mode)) { error = "Non-directory component: " + path; return false; }
        ++pos;
    }
}

bool matches(const std::string &name, const std::vector<std::string> &keywords) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    });
    for (const auto &word : keywords) if (lower.find(word) != std::string::npos) return true;
    return false;
}

bool scan(int fd, const std::string &path, unsigned depth, const Config &c,
          PathPlan &plan, std::set<std::string> &paths, std::string &error, Clock::time_point deadline) {
    DIR *dir = fdopendir(fd);
    if (!dir) { close(fd); error = "Cannot enumerate: " + path; return false; }
    bool ok = true;
    while (ok) {
        if (Clock::now() >= deadline) { error = "Path scan exceeded 5-second budget"; ok = false; break; }
        errno = 0;
        dirent *entry = readdir(dir);
        if (!entry) { if (errno) { error = "Directory read failed: " + path; ok = false; } break; }
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (++plan.scanned > kMaxEntries) { error = "Scan exceeds 100000 entries; narrow keywords or use explicit paths"; ok = false; break; }
        struct stat st{};
        if (fstatat(dirfd(dir), name.c_str(), &st, AT_SYMLINK_NOFOLLOW) < 0) {
            error = "Cannot inspect directory entry: " + path + '/' + name; ok = false; break;
        }
        if (S_ISLNK(st.st_mode)) continue;
        const std::string child = path + '/' + name;
        if (S_ISDIR(st.st_mode) && depth < 8) {
            int childfd = openat(dirfd(dir), name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (childfd < 0) { error = "Cannot open directory: " + child; ok = false; break; }
            ok = scan(childfd, child, depth + 1, c, plan, paths, error, deadline);
        } else if (trusted_file(st) && matches(name, c.path_keywords)) {
            if (child.size() > 255) { error = "Matching path exceeds SUSFS 255-byte limit: " + child; ok = false; break; }
#ifndef LH_HOST_TEST
            if (!valid_ro_path(child)) { error = "Matching path contains unsupported characters: " + child; ok = false; break; }
#endif
            paths.insert(child);
            if (paths.size() > kMaxPaths) { error = "Plan exceeds 512 hidden paths; narrow keywords"; ok = false; }
        }
    }
    closedir(dir);
    return ok;
}

bool runtime_dir(std::string &error) {
    if (mkdir(LH_RUNTIME_DIR, 0700) < 0 && errno != EEXIST) { error = "Cannot create runtime directory"; return false; }
    struct stat st{};
    if (lstat(LH_RUNTIME_DIR, &st) < 0 || !S_ISDIR(st.st_mode) ||
        st.st_uid != geteuid() || (st.st_mode & 0022)) {
        error = "Unsafe runtime directory"; return false;
    }
    return true;
}

bool write_runtime(const char *name, const std::string &text) {
    std::string tmp = std::string(LH_RUNTIME_DIR) + "/.paths.XXXXXX";
    int fd = mkstemp(tmp.data());
    if (fd < 0) return false;
    bool ok = fchmod(fd, 0600) == 0;
    size_t at = 0;
    while (ok && at < text.size()) {
        ssize_t n = write(fd, text.data() + at, text.size() - at);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        at += n;
    }
    if (ok) ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    if (ok) ok = rename(tmp.c_str(), (std::string(LH_RUNTIME_DIR) + '/' + name).c_str()) == 0;
    if (!ok) unlink(tmp.c_str());
    return ok;
}

#ifndef LH_HOST_TEST
bool run_ksud(const std::vector<std::string> &args, std::string &output,
              Clock::time_point deadline, std::string &error) {
    if (Clock::now() >= deadline) { error = "Path operation exceeded 60-second budget"; return false; }
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) { error = "Cannot create command pipe"; return false; }
    pid_t child = fork();
    if (child < 0) { close(pipefd[0]); close(pipefd[1]); error = "Cannot start ksud"; return false; }
    if (child == 0) {
        setpgid(0, 0);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO); dup2(pipefd[1], STDERR_FILENO); close(pipefd[1]);
        std::vector<char *> argv;
        argv.push_back(const_cast<char *>(kKsud));
        for (const auto &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        execv(kKsud, argv.data());
        _exit(127);
    }
    close(pipefd[1]);
    setpgid(child, child);
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    auto end = std::min(deadline, Clock::now() + std::chrono::seconds(8));
    output.clear();
    int status = 0;
    bool finished = false, failed = false;
    while (true) {
        char buf[1024];
        ssize_t n;
        while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
            output.append(buf, n);
            if (output.size() > 16384) { error = "ksud output exceeds limit"; failed = true; break; }
        }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            error = "Cannot read ksud output"; failed = true;
        }
        if (!finished) {
            pid_t result = waitpid(child, &status, WNOHANG);
            if (result == child) finished = true;
            else if (result < 0 && errno != EINTR) { error = "Cannot wait for ksud"; failed = true; }
        }
        if (failed || (finished && n == 0)) break;
        if (Clock::now() >= end) { error = "ksud command timed out"; failed = true; break; }
        pollfd ready{pipefd[0], POLLIN | POLLHUP, 0}; poll(&ready, 1, 20);
    }
    if (failed) kill(-child, SIGKILL);
    if (!finished) { kill(-child, SIGKILL); kill(child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {} }
    close(pipefd[0]);
    if (failed) return false;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) { error = "ksud failed: " + output; return false; }
    return true;
}

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n"), last = s.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
}
bool has_feature(const std::string &text, const std::string &feature) {
    std::istringstream lines(text); std::string line;
    while (std::getline(lines, line)) if (trim(line) == feature) return true;
    return false;
}
#endif
} // namespace

bool build_path_plan(const Config &c, PathPlan &plan, std::string &error,
                     const std::vector<std::string> &roots) {
    plan = {}; error.clear();
    std::set<std::string> paths, maps, missing;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    for (const auto &root : roots) {
        if (c.path_keywords.empty()) break;
        struct stat st{};
        if (lstat(root.c_str(), &st) < 0) {
            if (errno == ENOENT || errno == ENOTDIR) continue;
            error = "Cannot inspect scan root: " + root; return false;
        }
        // Android aliases such as /system/vendor are not followed twice.
        if (S_ISLNK(st.st_mode)) continue;
        if (!S_ISDIR(st.st_mode)) { error = "Scan root is not a directory: " + root; return false; }
        int fd = open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) { error = "Cannot open scan root: " + root; return false; }
        if (!scan(fd, root, 0, c, plan, paths, error, deadline)) return false;
    }
    for (const auto *list : {&c.hide_paths, &c.map_paths}) {
        auto &target = list == &c.hide_paths ? paths : maps;
        for (const auto &path : *list) {
            std::string why;
            if (existing_ro_file(path, why)) target.insert(path);
            else if (!why.empty()) { error = why; return false; }
            else missing.insert(path);
        }
    }
    if (paths.size() > kMaxPaths || maps.size() > kMaxPaths) { error = "Plan exceeds 512 paths"; return false; }
    plan.paths.assign(paths.begin(), paths.end()); plan.maps.assign(maps.begin(), maps.end());
    plan.missing.assign(missing.begin(), missing.end());
    return true;
}

std::string serialize_path_plan(const Config &c, const PathPlan &p) {
    std::ostringstream out;
    out << "native_paths_enabled=" << (c.enabled && c.native_paths) << "\nscanned_entries=" << p.scanned
        << "\npaths_count=" << p.paths.size() << "\nmaps_count=" << p.maps.size() << '\n';
    for (const auto &path : p.paths) out << "hide_path=" << path << '\n';
    for (const auto &path : p.maps) out << "map_path=" << path << '\n';
    for (const auto &path : p.missing) out << "missing_path=" << path << '\n';
    return out.str();
}

bool apply_native_paths(const Config &c, std::string &error) {
    if (!runtime_dir(error)) return false;
    int lockfd = open(LH_RUNTIME_DIR "/paths.lock", O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lockfd < 0 || flock(lockfd, LOCK_EX | LOCK_NB) < 0) {
        if (lockfd >= 0) close(lockfd);
        error = "Another path operation is running"; return false;
    }
    struct Guard { int fd; ~Guard() { close(fd); } } guard{lockfd};
    struct stat lockstat{};
    if (fstat(lockfd, &lockstat) < 0 || !S_ISREG(lockstat.st_mode) ||
        lockstat.st_uid != geteuid() || (lockstat.st_mode & 0022)) {
        error = "Unsafe path-operation lock"; return false;
    }
    std::string log;
    size_t done_paths = 0, done_maps = 0;
    auto result = [&](const char *status, bool ok) {
        if (!ok && (done_paths || done_maps)) status = "partial";
        std::ostringstream state;
        state << "paths_status=" << status << "\npaths_count=" << done_paths << "\nmaps_count=" << done_maps << '\n';
        if (!error.empty()) log += "error=" + error + '\n';
        if (!write_runtime("paths.log", log) || !write_runtime("paths.status", state.str())) {
            error = "Cannot save path operation status"; return false;
        }
        return ok;
    };
    if (!c.enabled || !c.native_paths) {
        log = "reboot_required_to_clear_existing_marks=1\n";
        return result("disabled", true);
    }
#ifdef LH_HOST_TEST
    // Host tests must never run a phone root-manager binary.
    error = "Native SUSFS application is unavailable in host tests";
    return result("unsupported", false);
#else
    if (geteuid() != 0) { error = "Root required"; return result("failed", false); }
    const auto deadline = Clock::now() + std::chrono::seconds(60);
    struct stat ksud{};
    if (lstat(kKsud, &ksud) < 0 || !trusted_file(ksud) ||
        !(ksud.st_mode & 0111) || (ksud.st_mode & 0022)) {
        error = "BakaSU ksud not available"; return result("unsupported", false);
    }
    std::string output;
    if (!run_ksud({"susfs", "show", "version"}, output, deadline, error) ||
        (trim(output) != "2.3.0" && trim(output) != "v2.3.0")) {
        if (error.empty()) error = "Only the reviewed SUSFS 2.3.0 adapter is supported";
        return result("unsupported", false);
    }
    if (!run_ksud({"susfs", "show", "enabled_features"}, output, deadline, error) ||
        !has_feature(output, "CONFIG_KSU_SUSFS_SUS_PATH")) {
        if (error.empty()) error = "SUS_PATH feature is unavailable";
        return result("unsupported", false);
    }
    const bool maps_enabled = has_feature(output, "CONFIG_KSU_SUSFS_SUS_MAP");
    PathPlan plan;
    if (!build_path_plan(c, plan, error)) return result("failed", false);
    log = serialize_path_plan(c, plan);
    if (!plan.maps.empty() && !maps_enabled) {
        error = "Configured map paths require SUS_MAP"; return result("unsupported", false);
    }
    for (const auto *list : {&plan.paths, &plan.maps}) {
        const bool map = list == &plan.maps;
        for (const auto &path : *list) {
            // Recheck immediately before calling ksud. Logical RO partitions
            // cannot be swapped by an ordinary app during the check/command.
            if (!existing_ro_file(path, error)) {
                if (error.empty()) error = "Path disappeared: " + path;
                return result("failed", false);
            }
            if (!run_ksud({"susfs", map ? "add_sus_map" : "add_sus_path", path}, output, deadline, error))
                return result("failed", false);
            if (map) ++done_maps; else ++done_paths;
        }
    }
    return result("applied", true);
#endif
}
} // namespace lh
