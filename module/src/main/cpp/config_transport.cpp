#include "config_transport.h"
#include <cerrno>
#include <sys/socket.h>

namespace lh {
namespace {
bool transfer(int fd, void *buffer, size_t size, bool writing) {
    auto *p = static_cast<char *>(buffer);
    while (size) {
        ssize_t n = writing ? send(fd, p, size, MSG_NOSIGNAL) : recv(fd, p, size, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n; size -= n;
    }
    return true;
}
void timeout(int fd) {
    timeval t{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(t));
}
}
bool send_frame(int fd, const std::string &data) {
    uint32_t size = data.size();
    return transfer(fd, &size, sizeof(size), true) && transfer(fd, const_cast<char *>(data.data()), size, true);
}
bool receive_frame(int fd, std::string &data, size_t limit) {
    uint32_t size = 0;
    if (!transfer(fd, &size, sizeof(size), false) || size == 0 || size > limit) return false;
    data.resize(size);
    return transfer(fd, data.data(), size, false);
}
void companion(int fd) {
    timeout(fd);
    try {
        Config c;
        std::string error, systems;
        std::vector<std::string> parsed;
        if (!load_config(c, error) || !read_file(kSystemPath, systems, kMaxSystemBytes) ||
            !parse_system_packages(systems, parsed)) { send_frame(fd, ""); return; }
        if (send_frame(fd, serialize_config(c))) send_frame(fd, systems);
    } catch (...) { send_frame(fd, ""); }
    // Zygisk owns the server-side descriptor.
}
bool receive_snapshot(int fd, Config &config, std::vector<std::string> &systems) {
    timeout(fd);
    std::string raw, list, error;
    return receive_frame(fd, raw, kMaxConfigBytes) && parse_config(raw, config, error) &&
           receive_frame(fd, list, kMaxSystemBytes) && parse_system_packages(list, systems);
}
} // namespace lh
