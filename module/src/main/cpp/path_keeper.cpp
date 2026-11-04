#include "path_keeper.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <set>
#include <signal.h>
#include <sstream>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace lh {
namespace {
constexpr const char *kSocket = LH_RUNTIME_DIR "/paths.sock";
constexpr size_t kMaxPins = 1024;
struct Fd {
    int n;
    explicit Fd(int fd = -1) : n(fd) {}
    ~Fd() { if (n >= 0) close(n); }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
};

// Walk with directory descriptors so intermediate symlinks cannot redirect a
// validated partition pathname into another tree between lstat and open.
int open_pin(const std::string &path) {
#ifndef LH_HOST_TEST
    if (!valid_ro_path(path)) { errno = EINVAL; return -1; }
#endif
    if (path.empty() || path.front() != '/') { errno = EINVAL; return -1; }
    int parent = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (parent < 0) return -1;
    size_t start = 1;
    while (start < path.size()) {
        const size_t slash = path.find('/', start);
        const auto part = path.substr(start, slash == std::string::npos ? slash : slash - start);
        if (part.empty() || part == "." || part == "..") { close(parent); errno = EINVAL; return -1; }
        int flags = O_PATH | O_NOFOLLOW | O_CLOEXEC;
        if (slash != std::string::npos) flags |= O_DIRECTORY;
        const int next = openat(parent, part.c_str(), flags);
        const int saved = errno;
        close(parent);
        if (next < 0) { errno = saved; return -1; }
        parent = next;
        if (slash == std::string::npos) {
            struct stat st{};
            if (fstat(parent, &st) < 0 || !S_ISREG(st.st_mode)) { close(parent); errno = EINVAL; return -1; }
#ifndef LH_HOST_TEST
            struct statvfs fs{};
            if (st.st_uid != 0 || fstatvfs(parent, &fs) < 0 || !(fs.f_flag & ST_RDONLY)) {
                close(parent); errno = EROFS; return -1;
            }
#endif
            return parent;
        }
        start = slash + 1;
    }
    close(parent); errno = EINVAL; return -1;
}

bool pin_current(const PathPins::Pin &pin) {
    Fd current(open_pin(pin.path));
    struct stat held{}, now{};
    return current.n >= 0 && fstat(pin.fd, &held) == 0 && fstat(current.n, &now) == 0 &&
           held.st_dev == pin.dev && held.st_ino == pin.ino && now.st_dev == pin.dev && now.st_ino == pin.ino;
}

bool address(sockaddr_un &addr) {
    addr = {}; addr.sun_family = AF_UNIX;
    if (std::strlen(kSocket) >= sizeof(addr.sun_path)) return false;
    std::strcpy(addr.sun_path, kSocket);
    return true;
}

bool same_path_config(const Config &a, const Config &b) {
    return a.enabled == b.enabled && a.native_paths == b.native_paths &&
           a.path_keywords == b.path_keywords && a.hide_paths == b.hide_paths && a.map_paths == b.map_paths;
}

std::string keeper_status(const PathPins &pins, const Config &config) {
    Config saved;
    std::string error;
    const auto current = load_config(saved, error)
        ? (same_path_config(saved, config) ? "yes" : "no") : "unknown";
    std::ostringstream out;
    out << "keeper_protocol=1\npaths_keeper_pid=" << getpid()
        << "\npaths_lifetime=" << (pins.current() ? "held" : "changed")
        << "\npaths_pinned_count=" << pins.files().size()
        << "\npaths_config_current=" << current << '\n';
    return out.str();
}

bool exchange(const char *command, std::string &reply) {
    Fd socketfd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    sockaddr_un addr{};
    if (socketfd.n < 0 || !address(addr) ||
        connect(socketfd.n, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) return false;
    ucred peer{}; socklen_t size = sizeof(peer);
    if (getsockopt(socketfd.n, SOL_SOCKET, SO_PEERCRED, &peer, &size) < 0 || peer.uid != geteuid()) return false;
    const auto len = std::strlen(command);
    if (send(socketfd.n, command, len, MSG_NOSIGNAL) != static_cast<ssize_t>(len)) return false;
    pollfd ready{socketfd.n, POLLIN, 0};
    // No retry loop that could turn a status request into unbounded work.
    if (poll(&ready, 1, 1500) <= 0) return false;
    char buffer[4096];
    const ssize_t n = recv(socketfd.n, buffer, sizeof(buffer), MSG_TRUNC);
    if (n <= 0 || n > static_cast<ssize_t>(sizeof(buffer))) return false;
    reply.assign(buffer, n);
    return reply.starts_with("keeper_protocol=1\n");
}

bool close_unrelated(const std::set<int> &keep) {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) return false;
    while (auto *entry = readdir(dir)) {
        char *end = nullptr;
        const long fd = std::strtol(entry->d_name, &end, 10);
        if (*entry->d_name && !*end && fd > 2 && fd != dirfd(dir) && !keep.count(fd)) close(fd);
    }
    closedir(dir);
    return true;
}

[[noreturn]] void serve(const PathPins &pins, int listener, int ack, int lockfd,
                        const Config &config, const std::string &receipt, const std::string &log) {
    if (setsid() < 0 || chdir("/") < 0) _exit(1);
    Fd nullfd(open("/dev/null", O_RDWR | O_CLOEXEC));
    if (nullfd.n < 0) _exit(1);
    for (int fd = 0; fd < 3; ++fd) if (dup2(nullfd.n, fd) < 0) _exit(1);
    std::set<int> keep{listener, ack, lockfd, nullfd.n};
    for (const auto &pin : pins.files()) keep.insert(pin.fd);
    if (!close_unrelated(keep) || !pins.current()) _exit(1);
    prctl(PR_SET_NAME, "lh-path-keeper", 0, 0, 0);
    if (!write_path_runtime("paths.log", log) || !write_path_runtime("paths.status", receipt)) _exit(1);
    const char ready = '1';
    if (send(ack, &ready, 1, MSG_NOSIGNAL) != 1) _exit(1);
    close(ack);
    // Blocking accept: no periodic scans, timers, wake locks or re-registration.
    while (true) {
        Fd client(accept4(listener, nullptr, nullptr, SOCK_CLOEXEC));
        if (client.n < 0) { if (errno == EINTR) continue; _exit(1); }
        ucred peer{}; socklen_t size = sizeof(peer);
        if (getsockopt(client.n, SOL_SOCKET, SO_PEERCRED, &peer, &size) < 0 || peer.uid != geteuid()) continue;
        timeval timeout{1, 0};
        if (setsockopt(client.n, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
            setsockopt(client.n, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) continue;
        char command[16];
        const auto n = recv(client.n, command, sizeof(command), MSG_TRUNC);
        if (n <= 0 || n > static_cast<ssize_t>(sizeof(command))) continue;
        const std::string_view request(command, n);
        if (request == "stop") {
            const char stopped[] = "keeper_protocol=1\npaths_lifetime=stopped\n";
            send(client.n, stopped, sizeof(stopped) - 1, MSG_NOSIGNAL);
            unlink(kSocket);
            _exit(0); // releases refs/lock; does not promise to clear kernel marks
        }
        if (request != "status") continue;
        const auto status = keeper_status(pins, config);
        send(client.n, status.data(), status.size(), MSG_NOSIGNAL);
    }
}
} // namespace

PathPins::~PathPins() { for (const auto &pin : files_) close(pin.fd); }

bool PathPins::acquire(const PathPlan &plan, std::string &error) {
    if (!files_.empty()) { error = "References already acquired"; return false; }
    std::set<std::string> paths(plan.paths.begin(), plan.paths.end());
    paths.insert(plan.maps.begin(), plan.maps.end());
    rlimit limit{};
    if (paths.size() > kMaxPins || getrlimit(RLIMIT_NOFILE, &limit) < 0 || paths.size() + 32 > limit.rlim_cur) {
        error = "Insufficient descriptor budget for path references"; return false;
    }
    for (const auto &path : paths) {
        Fd fd(open_pin(path));
        struct stat st{};
        if (fd.n < 0 || fstat(fd.n, &st) < 0) {
            error = "Cannot retain read-only regular file: " + path; return false;
        }
        files_.push_back({path, fd.n, st.st_dev, st.st_ino});
        fd.n = -1;
    }
    return true;
}

bool PathPins::matches(const std::string &path, std::string &error) const {
    const auto pin = std::find_if(files_.begin(), files_.end(), [&](const auto &p) { return p.path == path; });
    if (pin == files_.end() || !pin_current(*pin)) {
        error = "Path no longer names the retained inode: " + path; return false;
    }
    return true;
}

bool PathPins::current() const {
    for (const auto &pin : files_) if (!pin_current(pin)) return false;
    return true;
}

bool write_path_runtime(const char *name, const std::string &text) {
    std::string tmp = std::string(LH_RUNTIME_DIR) + "/.paths.XXXXXX";
    Fd fd(mkstemp(tmp.data()));
    if (fd.n < 0) return false;
    bool ok = fchmod(fd.n, 0600) == 0;
    size_t at = 0;
    while (ok && at < text.size()) {
        const auto n = write(fd.n, text.data() + at, text.size() - at);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        at += n;
    }
    if (ok) ok = fsync(fd.n) == 0;
    if (close(fd.n) != 0) ok = false;
    fd.n = -1;
    if (ok) ok = rename(tmp.c_str(), (std::string(LH_RUNTIME_DIR) + '/' + name).c_str()) == 0;
    if (!ok) unlink(tmp.c_str());
    return ok;
}

bool start_path_keeper(const PathPins &pins, int lockfd, const Config &config,
                       const std::string &receipt, const std::string &log, std::string &error) {
    Fd listener(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
    sockaddr_un addr{};
    struct stat old{};
    if (listener.n < 0 || !address(addr)) { error = "Cannot create keeper socket"; return false; }
    if (lstat(kSocket, &old) == 0) {
        if (!S_ISSOCK(old.st_mode) || old.st_uid != geteuid() || unlink(kSocket) < 0) {
            error = "Unsafe stale keeper socket"; return false;
        }
    } else if (errno != ENOENT) { error = "Cannot inspect keeper socket"; return false; }
    if (bind(listener.n, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 ||
        chmod(kSocket, 0600) < 0 || listen(listener.n, 4) < 0) {
        error = "Cannot listen for keeper status"; return false;
    }
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) < 0) { error = "Cannot create keeper handshake"; return false; }
    Fd parent(pair[0]), child(pair[1]);
    const pid_t pid = fork();
    if (pid < 0) { error = "Cannot start inode keeper"; return false; }
    if (pid == 0) {
        close(parent.n);
        serve(pins, listener.n, child.n, lockfd, config, receipt, log);
    }
    close(child.n); child.n = -1;
    pollfd ready{parent.n, POLLIN, 0};
    char answer = 0;
    if (poll(&ready, 1, 2000) > 0 && recv(parent.n, &answer, 1, MSG_DONTWAIT) == 1 && answer == '1') return true;
    kill(pid, SIGKILL);
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
    unlink(kSocket);
    error = "Inode keeper did not acknowledge startup";
    return false;
}

std::string native_path_status() {
    std::string receipt, live;
    if (!read_file(LH_RUNTIME_DIR "/paths.status", receipt, 4096)) receipt = "paths_status=not_started\n";
    if (!exchange("status", live)) {
        const bool retained = receipt.find("paths_lifetime_mode=inode_refs\n") != std::string::npos;
        const bool registered = receipt.find("paths_status=applied\n") != std::string::npos ||
                                receipt.find("paths_status=partial\n") != std::string::npos;
        const bool none = receipt.find("paths_lifetime_mode=none\n") != std::string::npos;
        live = std::string("paths_lifetime=") + (retained ? "unavailable" : registered && !none ? "unmanaged" : "inactive") + '\n';
    }
    return receipt + live + "paths_visibility=unverified\n";
}

bool stop_path_keeper(std::string &error) {
    std::string reply;
    if (!exchange("stop", reply) || reply.find("paths_lifetime=stopped\n") == std::string::npos) {
        error = "No responsive inode keeper"; return false;
    }
    return true;
}
} // namespace lh
