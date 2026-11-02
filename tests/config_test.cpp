#include "config.h"
#include "config_transport.h"
#include "service_filter.h"
#include "feature_filter.h"
#include "broadcast_filter.h"
#include "susfs_backend.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
using namespace lh;
int main() {
    Config c, unchanged = preset("lineage");
    std::string error, base = serialize_config(unchanged);
    assert(parse_config(base, c, error));
    assert(serialize_config(c) == base);
    assert(!c.legacy_boot);
    assert(c.native_paths);
    assert(valid_ro_path("/product/overlay/Ax__evolution_mumba.apk"));
    for (const char *bad : {"/data/local/tmp/key", "/system/../data/key", "/system//file",
        "/system/./file", "/system/", "/system", "/system/*lineage*", "/vendor/x;id", "/product/a b"})
        assert(!valid_ro_path(bad));
    for (const std::string &bad : std::vector<std::string>{"", "schema=1\n", base + "enabled=0\n", base + "typo=1\n",
        base + "package=org.app;id\n", base + "package=org.app:remote\n",
        base + "package=org..app\n", base + "package=org.1app\n", base + "resource_id=7f\n",
        base + "resource_id=01\n", base + "resource_id=00\n", base + "service_keyword=a\n",
        base + "service_keyword=lineage\n", base + "feature=evil$(id)\n",
        base + "hide_path=/data/local/tmp/foo\n", base + "map_path=/system/../vendor/foo\n",
        base + "path_keyword=ab\n", base + "path_keyword=lineage\n"}) {
        Config target = unchanged;
        assert(!parse_config(bad, target, error));
        assert(serialize_config(target) == base);
    }
    assert(!parse_config(base + std::string(65536, '#'), c, error));
    assert(!parse_config(base + std::string("\0", 1), c, error));
    // An import without its last LF must not normalize into an unreadable file.
    std::string boundary=base;
    for (int i=0; boundary.size()+264 < kMaxConfigBytes; ++i) {
        auto package="org.boundary.app"+std::to_string(i);
        package.append(255-package.size(), 'a');
        boundary += "package="+package+'\n';
    }
    const auto tail=kMaxConfigBytes+1-boundary.size()-9;
    assert(tail>=3 && tail<=255);
    boundary += "package=a."+std::string(tail-2, 'z')+'\n';
    assert(boundary.size()==kMaxConfigBytes+1);
    boundary.pop_back();
    assert(!parse_config(boundary, c, error));
    assert(error.find("Canonical")!=std::string::npos);
    boundary.pop_back();
    assert(parse_config(boundary, c, error));
    assert(serialize_config(c).size()==kMaxConfigBytes);
    assert(save_config(c, error));
    Config max_loaded; assert(load_config(max_loaded, error));
    c.packages.push_back("org.overflow");
    assert(!save_config(c, error));
    assert(load_config(max_loaded, error));
    assert(serialize_config(max_loaded).size()==kMaxConfigBytes);
    auto change = [&](std::string from, std::string to) { auto b = base; b.replace(b.find(from), from.size(), to); return b; };
    assert(!parse_config(change("mode=all", "mode=oops"), c, error));
    assert(!parse_config(change("enabled=1", "enabled=yes"), c, error));
    assert(!parse_config(change("schema=1", "schema=2"), c, error));
    assert(!parse_config(change("profile=lineage", "profile=../evil"), c, error));
    assert(parse_config(base + "service_keyword=EVOLUTION\n", c, error));
    assert(c.service_keywords.back() == "evolution");
    assert(parse_config(base + "path_keyword=EVOLUTION\n", c, error));
    assert(c.path_keywords.back() == "evolution");
    const std::string scanroot = std::string(LH_RUNTIME_DIR) + "/scan-test";
    assert(mkdir(scanroot.c_str(), 0700) == 0);
    std::ofstream(scanroot + "/a__Evolution.apk") << "match";
    std::ofstream(scanroot + "/ordinary.apk") << "no match";
    assert(symlink("a__Evolution.apk", (scanroot + "/lineage-link.apk").c_str()) == 0);
    Config scanconfig = preset("evox"); scanconfig.hide_paths.clear(); scanconfig.map_paths.clear();
    PathPlan plan;
    assert(build_path_plan(scanconfig, plan, error, {scanroot}));
    assert(plan.paths == std::vector<std::string>{scanroot + "/a__Evolution.apk"});
    assert(plan.maps.empty()); assert(plan.scanned == 3);
    Config explicit_only = scanconfig; explicit_only.path_keywords.clear();
    PathPlan empty_plan;
    assert(build_path_plan(explicit_only, empty_plan, error, {scanroot + "/ordinary.apk"}));
    assert(empty_plan.scanned == 0 && empty_plan.paths.empty());
    assert(serialize_path_plan(scanconfig, plan).find("paths_count=1\n") != std::string::npos);
    const auto overflowroot = scanroot + "/overflow";
    assert(mkdir(overflowroot.c_str(), 0700) == 0);
    for (int i = 0; i < 513; ++i) std::ofstream(overflowroot + "/lineage-" + std::to_string(i)) << "x";
    assert(!build_path_plan(scanconfig, plan, error, {overflowroot}));
    assert(error.find("512") != std::string::npos);
    std::vector<std::string> systems;
    assert(!parse_system_packages("", systems));
    assert(!parse_system_packages("com.android.settings\n", systems));
    assert(!parse_system_packages("android\npackage:broken\n", systems));
    assert(parse_system_packages("android\ncom.android.settings\norg.vendor.system\n", systems));
    c = preset("lineage");
    assert(should_inject(c, 10000, "org.example.app", systems));
    assert(should_inject(c, 10000, "com.android.chrome", systems));
    assert(should_inject(c, 1010000, "org.example.app", systems));
    assert(!should_inject(c, 1000, "org.example.app", systems));
    assert(!should_inject(c, 1001000, "org.example.app", systems));
    assert(!should_inject(c, 20000, "org.example.app", systems));
    assert(!should_inject(c, 99000, "org.example.app", systems));
    assert(!should_inject(c, -1, "org.example.app", systems));
    assert(!should_inject(c, 10000, "org.vendor.system", systems));
    assert(!should_inject(c, 10000, "com.android.settings", systems));
    assert(!should_inject(c, 10000, "org.lineageos.jelly", systems));
    assert(!should_inject(c, 10000, "", systems));
    assert(!should_inject(c, 10000, "org.example.app", {}));
    c.mode = "include";
    assert(!should_inject(c, 10000, "org.example.app", systems));
    c.packages = {"org.example.app", "com.android.settings", "org.vendor.system"};
    assert(should_inject(c, 10000, "org.example.app", systems));
    assert(!should_inject(c, 10000, "org.example.app2", systems));
    assert(!should_inject(c, 10000, "com.android.settings", systems));
    assert(!should_inject(c, 10000, "org.vendor.system", systems));
    c.mode = "exclude";
    assert(!should_inject(c, 10000, "org.example.app", systems));
    assert(should_inject(c, 10000, "org.example.app2", systems));
    c.packages.clear();
    assert(should_inject(c, 10000, "org.example.app", systems));
    c.enabled = false;
    assert(!should_inject(c, 10000, "org.example.app", systems));
    c = preset("evox"); set_active_config(c);
    assert(resource_id_matches(0x3f010001));
    assert(!resource_id_matches(0x01010001)); assert(!resource_id_matches(0x7f010001));
    c.resource_ids={"42"}; set_active_config(c);
    assert(!resource_id_matches(0x3f010001)); assert(resource_id_matches(0x42010001));
    c.resources=false; set_active_config(c); assert(!resource_id_matches(0x42010001));
    assert(hide_service("EVOLUTION.hardware"));
    assert(hide_service("lineage.test"));
    assert(hide_service("PROFILE"));
    assert(!hide_service("activity"));
    assert(hide_feature("org.lineageos.trust"));
    assert(!hide_feature("org.lineageos.trust.extra"));
    assert(hide_broadcast_action(c.broadcast_actions.front()));
    c.service_keywords = {"customrom"}; c.exact_services = {"CustomService"};
    c.feature_names = {"a.b", std::string(255, 'a')}; c.broadcast_actions = {"test.ACTION"};
    set_active_config(c);
    assert(!hide_service("lineage.test")); assert(hide_service("MyCustomROMService"));
    assert(hide_service("customservice")); assert(hide_feature("a.b"));
    assert(feature_length_matches(255)); assert(feature_length_matches(3));
    assert(!feature_length_matches(4)); assert(!feature_length_matches(256));
    assert(hide_feature(std::string(255, 'a'))); assert(hide_broadcast_action("test.ACTION"));
    c.services = c.features = c.broadcasts = false; set_active_config(c);
    assert(!hide_service("customrom")); assert(!hide_feature("a.b"));
    assert(!hide_broadcast_action("test.ACTION"));
    assert(save_config(c, error));
    Config loaded;
    assert(load_config(loaded, error)); assert(serialize_config(loaded) == serialize_config(c));
    std::ofstream(kSystemPath) << "android\ncom.android.settings\n";
    int sockets[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    std::thread server([&] { companion(sockets[1]); close(sockets[1]); });
    assert(receive_snapshot(sockets[0], loaded, systems)); close(sockets[0]); server.join();
    assert(serialize_config(loaded) == serialize_config(c));
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    uint32_t oversized = kMaxConfigBytes + 1;
    assert(write(sockets[1], &oversized, sizeof(oversized)) == sizeof(oversized));
    std::string frame; assert(!receive_frame(sockets[0], frame, kMaxConfigBytes));
    close(sockets[0]); close(sockets[1]);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    uint32_t truncated = 20; assert(write(sockets[1], &truncated, sizeof(truncated)) == sizeof(truncated));
    close(sockets[1]); assert(!receive_frame(sockets[0], frame, kMaxConfigBytes)); close(sockets[0]);
    // An incomplete/invalid file is never replaced by a broad default policy.
    std::ofstream(kConfigPath) << "schema=1\n";
    assert(!load_config(loaded, error));
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    companion(sockets[1]); close(sockets[1]);
    assert(!receive_snapshot(sockets[0], loaded, systems)); close(sockets[0]);
    std::cout << "config, scope, filters, storage and companion tests passed\n";
}
