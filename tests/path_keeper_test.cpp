#include "path_keeper.h"
#include <cassert>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace lh;

size_t fd_count() {
    DIR *dir = opendir("/proc/self/fd"); assert(dir);
    size_t count = 0;
    while (auto *entry = readdir(dir)) if (entry->d_name[0] != '.') ++count;
    closedir(dir); return count;
}
pid_t keeper_pid(const std::string &status) {
    const std::string key = "paths_keeper_pid=";
    auto at = status.find(key); assert(at != std::string::npos);
    return std::stoi(status.substr(at + key.size()));
}
void expect(const std::string &status, const std::string &line) {
    assert(status.find(line + '\n') != std::string::npos);
}

int main() {
    const std::string root = std::string(LH_RUNTIME_DIR) + "/files";
    assert(mkdir(root.c_str(), 0700) == 0);
    const auto one = root + "/one", two = root + "/two", mapped = root + "/map-only";
    for (const auto &path : {one, two, mapped}) std::ofstream(path) << "ordinary data";
    assert(symlink(one.c_str(), (root + "/link").c_str()) == 0);
    assert(symlink(root.c_str(), (root + "/directory-link").c_str()) == 0);
    const auto baseline = fd_count();
    std::string error;
    for (const auto &bad : {root + "/link", root + "/directory-link/one", root + "/../files/one", root, root + "/missing", std::string("/dev/null")}) {
        PathPins pins; PathPlan plan; plan.paths = {one, bad};
        assert(!pins.acquire(plan, error));
    }
    assert(fd_count() == baseline);
    PathPlan plan; plan.paths = {one, two}; plan.maps = {one, mapped};
    // Fail before acquiring anything when the process cannot afford the pins.
    rlimit original{}; assert(getrlimit(RLIMIT_NOFILE, &original) == 0);
    rlimit small = original; small.rlim_cur = 16; assert(setrlimit(RLIMIT_NOFILE, &small) == 0);
    { PathPins pins; assert(!pins.acquire(plan, error)); assert(pins.files().empty()); }
    assert(setrlimit(RLIMIT_NOFILE, &original) == 0);

    Config config = preset("evox"); assert(save_config(config, error));
    const std::string receipt = "paths_status=applied\npaths_count=2\nmaps_count=2\npaths_lifetime_mode=inode_refs\n";
    auto start = [&]() {
        int lock = open(LH_RUNTIME_DIR "/paths.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        assert(lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0);
        PathPins pins; assert(pins.acquire(plan, error));
        assert(pins.files().size() == 3 && pins.current());
        for (const auto &pin : pins.files()) {
            assert(fcntl(pin.fd, F_GETFD) & FD_CLOEXEC);
            assert(fcntl(pin.fd, F_GETFL) & O_PATH);
            char byte; assert(read(pin.fd, &byte, 1) < 0 && errno == EBADF);
        }
        assert(start_path_keeper(pins, lock, config, receipt, "fixture registration receipt\n", error));
        close(lock);
        // Destruction of all parent copies here must not drop the child's refs.
    };
    start();
    assert(fd_count() == baseline);
    auto status = native_path_status();
    expect(status, "paths_lifetime=held"); expect(status, "paths_pinned_count=3");
    expect(status, "paths_visibility=unverified"); expect(status, "paths_config_current=yes");
    pid_t pid = keeper_pid(status);
    struct stat socket_mode{}; assert(lstat(LH_RUNTIME_DIR "/paths.sock", &socket_mode) == 0);
    assert((socket_mode.st_mode & 0777) == 0600);

    // A fresh reader after handoff sees a live keeper, without any reapply.
    pid_t observer = fork(); assert(observer >= 0);
    if (!observer) {
        auto first = native_path_status();
        expect(first, "paths_lifetime=held"); expect(first, "paths_visibility=unverified");
        _exit(0);
    }
    int result = 0; assert(waitpid(observer, &result, 0) == observer && result == 0);
    // Ongoing readers get the same keeper/generation. A second apply cannot
    // spawn another keeper or overwrite its successful registration receipt.
    for (int i = 0; i < 4; ++i) {
        assert(!apply_native_paths(config, error));
        status = native_path_status();
        expect(status, "paths_lifetime=held"); assert(keeper_pid(status) == pid);
        expect(status, "paths_count=2");
    }
    config.features = !config.features; assert(save_config(config, error));
    expect(native_path_status(), "paths_config_current=yes");
    config.path_keywords.push_back("changed"); assert(save_config(config, error));
    expect(native_path_status(), "paths_config_current=no");
    config.path_keywords.pop_back(); assert(save_config(config, error));

    // Hold evidence of the original object in the child after unlink/recreate.
    // This tests actual VFS refs; it does NOT pretend to emulate SUSFS filtering.
    struct stat previous{}, replacement{}; assert(stat(one.c_str(), &previous) == 0);
    assert(unlink(one.c_str()) == 0); std::ofstream(one) << "replacement";
    assert(stat(one.c_str(), &replacement) == 0 && replacement.st_ino != previous.st_ino);
    const auto directory = "/proc/" + std::to_string(pid) + "/fd";
    DIR *fds = opendir(directory.c_str()); assert(fds);
    bool retained_deleted_inode = false;
    while (auto *entry = readdir(fds)) {
        if (entry->d_name[0] == '.') continue;
        struct stat held{};
        if (stat((directory + '/' + entry->d_name).c_str(), &held) == 0 &&
            held.st_dev == previous.st_dev && held.st_ino == previous.st_ino && held.st_nlink == 0)
            retained_deleted_inode = true;
    }
    closedir(fds); assert(retained_deleted_inode);
    status = native_path_status(); expect(status, "paths_lifetime=changed");
    expect(status, "paths_visibility=unverified");
    assert(stop_path_keeper(error));
    assert(waitpid(pid, &result, 0) == pid && result == 0);
    expect(native_path_status(), "paths_lifetime=unavailable");

    // An abrupt loss cannot masquerade as a live keeper from its old receipt.
    start(); pid = keeper_pid(native_path_status());
    assert(kill(pid, SIGKILL) == 0); assert(waitpid(pid, &result, 0) == pid);
    status = native_path_status(); expect(status, "paths_status=applied");
    expect(status, "paths_lifetime=unavailable"); expect(status, "paths_visibility=unverified");
    start(); pid = keeper_pid(native_path_status()); // safely replace stale socket
    assert(stop_path_keeper(error)); assert(waitpid(pid, &result, 0) == pid);

    // Publication failure must not acknowledge success or leave a keeper alive.
    assert(unlink(LH_RUNTIME_DIR "/paths.log") == 0);
    assert(mkdir(LH_RUNTIME_DIR "/paths.log", 0700) == 0);
    { PathPins pins; assert(pins.acquire(plan, error));
      int lock = open(LH_RUNTIME_DIR "/paths.lock", O_RDWR | O_CLOEXEC);
      assert(lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0);
      assert(!start_path_keeper(pins, lock, config, receipt, "cannot publish", error)); close(lock); }
    expect(native_path_status(), "paths_lifetime=unavailable");
    assert(fd_count() == baseline);
    assert(write_path_runtime("paths.status", "paths_status=applied\npaths_count=64\nmaps_count=3\n"));
    expect(native_path_status(), "paths_lifetime=unmanaged");
    std::cout << "keeper lifecycle passed: handoff, fresh/ongoing status readers, duplicate start, stale config, real inode refs, replacement, exit, publication failure, legacy receipts\n";
}
