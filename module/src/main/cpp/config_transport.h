#pragma once
#include "config.h"

namespace lh {
// Bounded, length-prefixed frames over the private Zygisk companion socket.
bool send_frame(int fd, const std::string &data);
bool receive_frame(int fd, std::string &data, size_t limit);
void companion(int fd);
bool receive_snapshot(int fd, Config &config, std::vector<std::string> &systems);
} // namespace lh
