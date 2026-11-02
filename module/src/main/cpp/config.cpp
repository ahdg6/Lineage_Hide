#include "config.h"
#include <algorithm>
#include <bitset>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace lh {
namespace {
Config current;
std::bitset<256> feature_lengths, resource_ids;
bool token(std::string_view s, size_t max) {
    return !s.empty() && s.size() <= max && std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
    });
}
bool contains(const std::vector<std::string> &list, std::string_view s) {
    return std::find(list.begin(), list.end(), s) != list.end();
}
}

Config preset(std::string_view name) {
    Config c;
    c.profile = name;
    c.service_keywords = {"lineage"};
    c.exact_services = {"profile"};
    c.feature_names = {"org.lineageos.livedisplay", "org.lineageos.profiles",
        "org.lineageos.hardware", "org.lineageos.globalactions", "org.lineageos.trust",
        "org.lineageos.health", "org.lineageos.android", "org.lineageos.settings"};
    c.broadcast_actions = {"lineageos.intent.action.SCREEN_CAMERA_GESTURE",
        "lineageos.intent.action.INITIALIZE_LINEAGE_HARDWARE",
        "lineageos.intent.action.INITIALIZE_LIVEDISPLAY", "lineageos.intent.action.UPDATE_PREFERENCE",
        "lineageos.intent.action.REFRESH_PREFERENCE", "lineageos.platform.intent.action.PROFILE_SELECTED",
        "lineageos.platform.intent.action.PROFILE_UPDATED",
        "lineageos.platform.intent.action.INTENT_ACTION_PROFILE_TRIGGER_STATE_CHANGED",
        "lineageos.platform.intent.action.UPDATE_TWILIGHT_STATE",
        "lineageos.platform.intent.action.CHARGING_CONTROL_CANCEL_ONCE"};
    c.resource_packages = {"lineageos.platform"};
    c.resource_ids = {"3f"};
    c.path_keywords = {"lineage", "gapps"};
    c.hide_paths = {"/system/framework/org.lineageos.platform-res.apk",
        "/system/framework/oat/arm64/org.lineageos.platform.odex",
        "/system/framework/oat/arm64/org.lineageos.platform.vdex",
        "/system/lib64/libstagefright.so"};
    c.map_paths = {"/system/framework/org.lineageos.platform-res.apk",
        "/system/lib64/libstagefright.so"};
    // EvoX inherits Lineage SDK surfaces. These additional service substrings
    // are starting rules, not a claim that every build exposes such services.
    if (name == "evox") {
        c.service_keywords = {"lineage", "evolution", "evox"};
        c.path_keywords = {"lineage", "gapps", "evolution", "evox"};
    }
    return c;
}

bool valid_package(std::string_view s) {
    if (!token(s, 255) || s.find('.') == std::string_view::npos) return false;
    bool start = true;
    for (char c : s) {
        if (c == '.') { if (start) return false; start = true; continue; }
        if (start && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) return false;
        if (c == '-') return false;
        start = false;
    }
    return !start;
}

bool valid_ro_path(std::string_view path) {
    if (path.empty() || path.size() > 255 || path.back() == '/') return false;
    bool root = false;
    for (const char *prefix : {"/system/", "/system_ext/", "/product/", "/vendor/", "/odm/"})
        if (path.rfind(prefix, 0) == 0) root = true;
    if (!root) return false;
    size_t start = 1;
    while (start < path.size()) {
        size_t slash = path.find('/', start);
        auto part = path.substr(start, slash == std::string_view::npos ? path.size() - start : slash - start);
        if (part.empty() || part == "." || part == ".." || !token(part, 255)) return false;
        if (slash == std::string_view::npos) return true;
        start = slash + 1;
    }
    return false;
}

bool parse_config(std::string_view text, Config &out, std::string &error) {
    auto fail = [&](const std::string &message) { error = message; return false; };
    if (text.empty() || text.size() > kMaxConfigBytes) return fail("Configuration must be 1..65536 bytes");
    for (unsigned char c : text)
        if ((c < 32 && c != '\n' && c != '\r') || c > 126) return fail("Only printable ASCII and newlines are supported");
    Config c;
    std::map<std::string, bool *> flags = {{"enabled", &c.enabled}, {"services", &c.services},
        {"features", &c.features}, {"broadcasts", &c.broadcasts}, {"resources", &c.resources},
        {"reflection", &c.reflection}, {"force_unmount", &c.force_unmount}, {"legacy_boot", &c.legacy_boot},
        {"native_paths", &c.native_paths}};
    std::map<std::string, std::vector<std::string> *> lists = {{"package", &c.packages},
        {"service_keyword", &c.service_keywords}, {"exact_service", &c.exact_services},
        {"feature", &c.feature_names}, {"broadcast", &c.broadcast_actions},
        {"resource_package", &c.resource_packages}, {"resource_id", &c.resource_ids},
        {"path_keyword", &c.path_keywords}, {"hide_path", &c.hide_paths}, {"map_path", &c.map_paths}};
    std::set<std::string> seen;
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) return fail("Expected key=value");
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        auto list = lists.find(key);
        if (list != lists.end()) {
            std::string checked = value;
            if (key == "exact_service")
                for (char &ch : checked) if (ch == '/' || ch == ':' || ch == '@') ch = '_';
            const bool path = key == "hide_path" || key == "map_path";
            if (path ? !valid_ro_path(value) : !token(checked, 255)) return fail("Invalid value for " + key);
            if (key == "package" && !valid_package(value)) return fail("Invalid package name");
            if (key == "resource_id") {
                if (value.size() != 2 || value.find_first_not_of("0123456789abcdef") != std::string::npos ||
                    value == "00" || value == "01" || value == "7f") return fail("Resource id must be two lowercase hex digits, excluding 00/01/7f");
            }
            if (key == "service_keyword" || key == "path_keyword") {
                if (value.size() < 3) return fail("Keywords need at least 3 characters");
                std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
                    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
                });
            }
            auto &items = *list->second;
            if (items.size() >= (key == "package" || path ? 512u : 128u)) return fail("Too many entries for " + key);
            if (contains(items, value)) return fail("Duplicate entry for " + key);
            items.push_back(value);
            continue;
        }
        if (!seen.insert(key).second) return fail("Duplicate key: " + key);
        auto flag = flags.find(key);
        if (flag != flags.end()) {
            if (value != "0" && value != "1") return fail("Expected 0 or 1 for " + key);
            *flag->second = value == "1";
        } else if (key == "schema") {
            if (value != "1") return fail("Unsupported configuration schema");
        } else if (key == "profile") {
            if (value != "lineage" && value != "evox" && value != "custom") return fail("Unknown profile");
            c.profile = value;
        } else if (key == "mode") {
            if (value != "all" && value != "include" && value != "exclude") return fail("Unknown app mode");
            c.mode = value;
        } else return fail("Unknown key: " + key);
    }
    for (const auto &flag : flags) if (!seen.count(flag.first)) return fail("Missing key: " + flag.first);
    for (const char *key : {"schema", "profile", "mode"}) if (!seen.count(key)) return fail(std::string("Missing key: ") + key);
    // Canonical output may gain a final newline compared with an imported file.
    if (serialize_config(c).size() > kMaxConfigBytes) return fail("Canonical configuration exceeds 65536 bytes");
    out = std::move(c);
    error.clear();
    return true;
}

std::string serialize_config(const Config &c) {
    std::ostringstream out;
    out << "schema=1\nprofile=" << c.profile << "\nenabled=" << c.enabled << "\nmode=" << c.mode
        << "\nservices=" << c.services << "\nfeatures=" << c.features << "\nbroadcasts=" << c.broadcasts
        << "\nresources=" << c.resources << "\nreflection=" << c.reflection
        << "\nforce_unmount=" << c.force_unmount << "\nlegacy_boot=" << c.legacy_boot
        << "\nnative_paths=" << c.native_paths << '\n';
    auto list = [&](const char *key, const auto &values) { for (const auto &v : values) out << key << '=' << v << '\n'; };
    list("package", c.packages); list("service_keyword", c.service_keywords); list("exact_service", c.exact_services);
    list("feature", c.feature_names); list("broadcast", c.broadcast_actions);
    list("resource_package", c.resource_packages); list("resource_id", c.resource_ids);
    list("path_keyword", c.path_keywords); list("hide_path", c.hide_paths); list("map_path", c.map_paths);
    return out.str();
}

bool parse_system_packages(std::string_view text, std::vector<std::string> &out) {
    if (text.size() > kMaxSystemBytes) return false;
    std::vector<std::string> result;
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        if (line != "android" && !valid_package(line)) return false;
        result.push_back(line);
    }
    // 'android' is always a system package. Missing/partial PM output must not
    // turn an empty system list into permission to inject everything.
    if (!contains(result, "android")) return false;
    out = std::move(result);
    return true;
}

bool should_inject(const Config &c, int uid, std::string_view package,
                   const std::vector<std::string> &systems) {
    const int app_id = uid % 100000;
    if (!c.enabled || uid < 0 || app_id < 10000 || app_id > 19999 ||
        !valid_package(package) || systems.empty() || contains(systems, package)) return false;
    for (const char *prefix : {"android.", "com.android.systemui", "com.android.settings",
        "com.android.permissioncontroller", "com.android.providers.", "com.android.externalstorage",
        "com.android.documentsui", "com.android.phone", "com.android.server.telecom",
        "com.android.bluetooth", "com.android.nfc", "com.android.inputmethod.",
        "org.lineageos.", "com.google.android.permissioncontroller"})
        if (package.rfind(prefix, 0) == 0) return false;
    bool listed = contains(c.packages, package);
    return c.mode == "all" || (c.mode == "include" && listed) || (c.mode == "exclude" && !listed);
}

bool read_file(const char *path, std::string &out, size_t limit) {
    out.clear();
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return false;
    struct stat st{};
    bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 && static_cast<size_t>(st.st_size) <= limit;
    char buffer[4096];
    while (ok) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ok = false; break; }
        if (n == 0) break;
        out.append(buffer, n);
        if (out.size() > limit) ok = false;
    }
    close(fd);
    if (!ok) out.clear();
    return ok;
}

bool load_config(Config &out, std::string &error) {
    std::string raw;
    if (!read_file(kConfigPath, raw, kMaxConfigBytes)) {
        error = "Configuration unavailable; open WebUI or reinstall to initialize";
        return false;
    }
    return parse_config(raw, out, error);
}

bool save_config(const Config &c, std::string &error) {
    const auto data = serialize_config(c);
    if (data.size() > kMaxConfigBytes) { error = "Configuration exceeds 65536 bytes"; return false; }
    if (mkdir(kStateDir, 0700) < 0 && errno != EEXIST) { error = "Cannot create state directory"; return false; }
    struct stat st{};
    if (lstat(kStateDir, &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022)) {
        error = "Unsafe state directory"; return false;
    }
    std::string pattern = std::string(kStateDir) + "/.config.XXXXXX";
    int fd = mkstemp(pattern.data());
    if (fd < 0) { error = "Cannot create temporary configuration"; return false; }
    size_t pos = 0;
    bool ok = fchmod(fd, 0600) == 0;
    while (ok && pos < data.size()) {
        ssize_t n = write(fd, data.data() + pos, data.size() - pos);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        pos += n;
    }
    if (ok) ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    if (ok) ok = rename(pattern.c_str(), kConfigPath) == 0;
    if (!ok) { unlink(pattern.c_str()); error = "Atomic save failed"; return false; }
    int dir = open(kStateDir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir >= 0) { fsync(dir); close(dir); }
    return true;
}
const Config &active_config() { return current; }
void set_active_config(Config c) {
    feature_lengths.reset(); resource_ids.reset();
    for (const auto &id : c.resource_ids) resource_ids.set(std::strtoul(id.c_str(), nullptr, 16));
    for (const auto &name : c.feature_names) if (name.size() < 256) feature_lengths.set(name.size());
    current = std::move(c);
}
bool resource_id_matches(uint32_t id) { return current.resources && resource_ids[id >> 24]; }
bool feature_length_matches(size_t length) { return length < 256 && feature_lengths[length]; }
} // namespace lh
