#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lh {
constexpr size_t kMaxConfigBytes = 65536;
constexpr size_t kMaxSystemBytes = 262144;
#ifndef LH_STATE_DIR
#define LH_STATE_DIR "/data/adb/lineage_hide"
#endif
#ifndef LH_RUNTIME_DIR
#define LH_RUNTIME_DIR "/dev/lineage_hide"
#endif
constexpr const char *kStateDir = LH_STATE_DIR;
constexpr const char *kConfigPath = LH_STATE_DIR "/config.conf";
constexpr const char *kSystemPath = LH_RUNTIME_DIR "/system_packages.txt";

struct Config {
    bool enabled = true, services = true, features = true, broadcasts = true;
    bool resources = true, reflection = true, force_unmount = true, legacy_boot = false;
    bool native_paths = true;
    std::string profile = "lineage", mode = "all";
    std::vector<std::string> packages, service_keywords, exact_services;
    std::vector<std::string> feature_names, broadcast_actions, resource_packages;
    std::vector<std::string> resource_ids;
    std::vector<std::string> path_keywords, hide_paths, map_paths;

    // Empty channels do not need hooks, even if their UI switch is on.
    bool has_service_rules() const { return services && (!service_keywords.empty() || !exact_services.empty()); }
    bool has_feature_rules() const { return features && !feature_names.empty(); }
    bool has_broadcast_rules() const { return broadcasts && !broadcast_actions.empty(); }
    bool has_resource_rules() const { return resources && (!resource_packages.empty() || !resource_ids.empty()); }
};
Config preset(std::string_view name);
bool parse_config(std::string_view text, Config &out, std::string &error);
std::string serialize_config(const Config &config);
bool valid_package(std::string_view name);
bool valid_ro_path(std::string_view path);
bool parse_system_packages(std::string_view text, std::vector<std::string> &out);
bool should_inject(const Config &config, int uid, std::string_view package,
                   const std::vector<std::string> &system_packages);
bool read_file(const char *path, std::string &out, size_t limit);
bool load_config(Config &out, std::string &error);
bool save_config(const Config &config, std::string &error);
// Set once in the forked process before installing hooks; immutable afterwards.
const Config &active_config();
void set_active_config(Config config);
bool resource_id_matches(uint32_t resource_id);
bool feature_length_matches(size_t length);
bool resource_package_length_matches(size_t length);
} // namespace lh
