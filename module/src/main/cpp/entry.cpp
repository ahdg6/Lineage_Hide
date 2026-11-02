#include "binder_hook.h"
#include "config_transport.h"
#include <unistd.h>
#include "log.h"
#include "reflection_filter.h"
#include "resource_hook.h"
#include "service_cache.h"
#include "zygisk.hpp"

#include <jni.h>
#include <string>

namespace {
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
        if (!args) return;

        if (args->uid < 0 || args->uid % 100000 < 10000 || args->uid % 100000 > 19999) {
            if (api_) api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        const auto package = package_from_data_dir(jstr_to_str(env_, args->app_data_dir));
        if (!lh::valid_package(package)) {
            if (api_) api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        lh::Config config;
        std::vector<std::string> systems;
        bool ready = false;
        int fd = api_ ? api_->connectCompanion() : -1;
        if (fd >= 0) {
            try { ready = lh::receive_snapshot(fd, config, systems); } catch (...) { ready = false; }
            close(fd);
        }
        if (!ready || !lh::should_inject(config, args->uid, package, systems)) {
            if (api_) api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        lh::set_active_config(std::move(config));
        const auto &settings = lh::active_config();
        if (api_ && settings.force_unmount) api_->setOption(zygisk::Option::FORCE_DENYLIST_UNMOUNT);
        const bool binder_rules = settings.has_service_rules() || settings.has_feature_rules() || settings.has_broadcast_rules();
        if (!binder_rules && !settings.has_resource_rules() && !settings.reflection) {
            if (api_) api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        g_enabled_for_process = true;
        bool binder_ready = false, resource_ready = false, reflection_ready = false;
        // The Zygisk API is guaranteed to be live in preAppSpecialize.  Hook
        // the boot-class native methods here, before post-specialization API
        // calls become implementation-defined.
        try {
            if (binder_rules) binder_ready = install_jni_hook(env_, api_);
        } catch (...) {
            log_error("JNI hook setup failed; Binder filtering is skipped");
        }
        try {
            resource_ready = settings.has_resource_rules() && install_resource_hook(env_, api_);
        } catch (...) {
            log_error("resource hook setup failed; resources stay visible");
        }
        try {
            if (settings.reflection) reflection_ready = install_field_hooks(env_, api_);
        } catch (...) {
            log_error("field hook setup failed; LINEAGE_APK_PATH stays visible");
        }
        const auto process_name = jstr_to_str(env_, args->nice_name);
        log_info("enabled uid=%d process=%.255s hooks binder=%d resources=%d reflection=%d",
                 args->uid, process_name.c_str(), binder_ready, resource_ready, reflection_ready);
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!g_enabled_for_process) return;
        try {
            if (lh::active_config().has_service_rules()) clear_cache(env_);
        } catch (...) {
            log_error("cache cleanup failed; continuing with binder instrumentation");
        }
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
};

REGISTER_ZYGISK_MODULE(LineageHideModule)

REGISTER_ZYGISK_COMPANION(lh::companion)
