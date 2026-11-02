#include "binder_hook.h"
#include "config_transport.h"
#include <unistd.h>
#include "log.h"
#include "reflection_filter.h"
#include "resource_hook.h"
#include "service_cache.h"
#include "zygisk.hpp"

#include <jni.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
std::array<char, 256> g_process_name{};
bool g_enabled_for_process = false;

std::string jstr_to_str(JNIEnv *env, jstring value) {
    if (!env || !value) return {};
    const char *raw = env->GetStringUTFChars(value, nullptr);
    if (!raw) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return {};
    }
    try {
        std::string out = raw;
        env->ReleaseStringUTFChars(value, raw);
        return out;
    } catch (...) {
        env->ReleaseStringUTFChars(value, raw);
        return {};
    }
}

// app_data_dir identifies the owning package even for android:process custom
// names. Do not guess from nice_name when the data directory is unavailable.
std::string package_from_data_dir(const std::string &dir) {
    if (dir.empty() || dir[0] != '/') return {};
    const auto slash = dir.rfind('/');
    return dir.substr(slash + 1);
}
} // namespace

class LineageHideModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        env_ = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        g_enabled_for_process = false;
        resource_hook_ready_ = false;
        g_process_name.fill('\0');
        if (!args) return;

        if (args->uid < 0 || args->uid % 100000 < 10000 || args->uid % 100000 > 19999) {
            if (api_) api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        const std::string process_name = jstr_to_str(env_, args->nice_name);
        if (!process_name.empty()) {
            const size_t count = (process_name.size() < g_process_name.size() - 1)
                                     ? process_name.size()
                                     : g_process_name.size() - 1;
            std::memcpy(g_process_name.data(), process_name.data(), count);
            g_process_name[count] = '\0';
        }
        lh::Config config;
        std::vector<std::string> systems;
        bool ready = false;
        int fd = api_ ? api_->connectCompanion() : -1;
        if (fd >= 0) {
            try { ready = lh::receive_snapshot(fd, config, systems); } catch (...) { ready = false; }
            close(fd);
        }
        const auto package = package_from_data_dir(jstr_to_str(env_, args->app_data_dir));
        if (!ready || !lh::should_inject(config, args->uid, package, systems)) {
            if (api_) api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        lh::set_active_config(std::move(config));
        const auto &settings = lh::active_config();
        g_enabled_for_process = true;
        if (api_ && settings.force_unmount) api_->setOption(zygisk::Option::FORCE_DENYLIST_UNMOUNT);
        set_feature_filtering(settings.features);
        set_broadcast_filtering(settings.broadcasts);
        // The Zygisk API is guaranteed to be live in preAppSpecialize.  Hook
        // the boot-class native methods here, before post-specialization API
        // calls become implementation-defined.
        try {
            if (settings.services || settings.features || settings.broadcasts) install_jni_hook(env_, api_);
        } catch (...) {
            log_error("JNI hook setup failed; Binder filtering is skipped");
        }
        try {
            resource_hook_ready_ = settings.resources && install_resource_hook(env_, api_);
        } catch (...) {
            resource_hook_ready_ = false;
            log_error("resource hook setup failed; resources stay visible");
        }
        try {
            if (settings.reflection) install_field_hooks(env_, api_);
        } catch (...) {
            log_error("field hook setup failed; LINEAGE_APK_PATH stays visible");
        }
        log_info("enabled uid=%d process=%s", args->uid, g_process_name.data());
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!g_enabled_for_process) return;
        try {
            if (lh::active_config().services) clear_cache(env_);
        } catch (...) {
            log_error("cache cleanup failed; continuing with binder instrumentation");
        }
        log_info("active for %s (resource hide=%d)", g_process_name.data(),
                 resource_hook_ready_ ? 1 : 0);
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *) override {
        // Lineage Hide is app-scoped. Do not leave the module resident in
        // system_server, where no filtering is needed and a native mapping
        // would only add observable surface.
        if (api_) api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    bool resource_hook_ready_ = false;
};

REGISTER_ZYGISK_MODULE(LineageHideModule)

REGISTER_ZYGISK_COMPANION(lh::companion)
