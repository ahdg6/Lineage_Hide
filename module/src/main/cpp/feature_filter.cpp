#include "feature_filter.h"
#include "config.h"
#include <algorithm>

bool hide_feature(std::string_view value) {
    const auto &c = lh::active_config();
    return c.features && std::find(c.feature_names.begin(), c.feature_names.end(), value) != c.feature_names.end();
}
