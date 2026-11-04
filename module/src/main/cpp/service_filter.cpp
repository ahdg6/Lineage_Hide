#include "service_filter.h"
#include "config.h"

#include <cstring>

namespace {
char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

bool equals_ci(std::string_view value, const char *needle) {
    if (!needle) return false;
    std::size_t i = 0;
    for (; needle[i] != '\0'; ++i) {
        if (i >= value.size() || ascii_lower(value[i]) != ascii_lower(needle[i])) return false;
    }
    return i == value.size();
}

bool contains_ci(std::string_view value, const char *needle) {
    if (!needle || *needle == '\0') return false;
    const std::size_t needle_size = std::char_traits<char>::length(needle);
    if (needle_size > value.size()) return false;
    for (std::size_t i = 0; i + needle_size <= value.size(); ++i) {
        std::size_t j = 0;
        for (; j < needle_size; ++j) {
            if (ascii_lower(value[i + j]) != needle[j]) break;
        }
        if (j == needle_size) return true;
    }
    return false;
}
} // namespace

bool hide_service(std::string_view service_name) {
    if (!lh::active_config().services) return false;
    for (const auto &service : lh::active_config().exact_services) {
        if (equals_ci(service_name, service.c_str())) return true;
    }

    for (const auto &keyword : lh::active_config().service_keywords) {
        if (contains_ci(service_name, keyword.c_str())) return true;
    }
    return false;
}
