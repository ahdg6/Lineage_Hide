#include "broadcast_filter.h"
#include "config.h"
#include <algorithm>

bool hide_broadcast_action(std::string_view value) {
    const auto &c = lh::active_config();
    return c.broadcasts && std::find(c.broadcast_actions.begin(), c.broadcast_actions.end(), value) != c.broadcast_actions.end();
}
