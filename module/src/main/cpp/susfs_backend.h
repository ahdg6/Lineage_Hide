#pragma once
#include "config.h"

namespace lh {
struct PathPlan {
    std::vector<std::string> paths, maps, missing;
    size_t scanned = 0;
};
// The optional roots argument exists for local filesystem tests. configctl
// always supplies the five fixed, read-only Android partition trees.
bool build_path_plan(const Config &config, PathPlan &plan, std::string &error,
                     const std::vector<std::string> &roots = {"/system", "/system_ext", "/product", "/vendor", "/odm"});
std::string serialize_path_plan(const Config &config, const PathPlan &plan);
bool apply_native_paths(const Config &config, std::string &error);
} // namespace lh
