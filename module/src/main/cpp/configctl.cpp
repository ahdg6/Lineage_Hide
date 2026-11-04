#include "config.h"
#include "susfs_backend.h"
#include "path_keeper.h"
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    using namespace lh;
    if (argc < 2) return 2;
#ifndef LH_HOST_TEST
    if (geteuid() != 0) { std::cerr << "Root required\n"; return 1; }
#endif
    const std::string command = argv[1];
    Config c;
    std::string error;
    if (command == "preset" && argc == 3) {
        if (std::string(argv[2]) != "lineage" && std::string(argv[2]) != "evox") return 2;
        std::cout << serialize_config(preset(argv[2]));
        return 0;
    }
    if (command == "status" && argc == 2) {
        std::string raw;
        std::vector<std::string> systems;
        bool ready = read_file(kSystemPath, raw, kMaxSystemBytes) && parse_system_packages(raw, systems);
        std::cout << "system_list_ready=" << ready << '\n';
        std::string legacy;
        if (!read_file(LH_RUNTIME_DIR "/legacy_boot", legacy, 128)) legacy = "not_started\n";
        std::cout << "legacy_boot=" << legacy;
        std::cout << native_path_status();
        return 0;
    }
    if (command == "stop-paths" && argc == 2) {
        if (!stop_path_keeper(error)) { std::cerr << error << '\n'; return 1; }
        std::cout << "References released; existing kernel marks are not guaranteed cleared until reboot\n";
        return 0;
    }
    if ((command == "path-plan" && (argc == 2 || argc == 3)) ||
        (command == "apply-paths" && argc == 2)) {
        const bool loaded = argc == 3 ? parse_config(argv[2], c, error) : load_config(c, error);
        if (!loaded) { std::cerr << error << '\n'; return 1; }
        if (command == "apply-paths") {
            if (!apply_native_paths(c, error)) { std::cerr << error << '\n'; return 1; }
            return 0;
        }
        PathPlan plan;
        if (!build_path_plan(c, plan, error)) { std::cerr << error << '\n'; return 1; }
        std::cout << serialize_path_plan(c, plan);
        return 0;
    }
    if ((command == "set" || command == "validate") && argc == 3) {
        if (!parse_config(argv[2], c, error) || (command == "set" && !save_config(c, error))) {
            std::cerr << error << '\n'; return 1;
        }
    } else if (command == "init" && argc == 3) {
        if (std::string(argv[2]) != "lineage" && std::string(argv[2]) != "evox") return 2;
        struct stat st{};
        if (lstat(kConfigPath, &st) == 0) {
            if (!load_config(c, error)) { std::cerr << error << '\n'; return 1; }
        } else {
            c = preset(argv[2]);
            if (!save_config(c, error)) { std::cerr << error << '\n'; return 1; }
        }
    } else if ((command == "get" || command == "legacy-boot") && argc == 2) {
        if (!load_config(c, error)) { std::cerr << error << '\n'; return 1; }
        if (command == "legacy-boot") return c.enabled && c.legacy_boot ? 0 : 1;
    } else return 2;
    std::cout << serialize_config(c);
    return 0;
}
