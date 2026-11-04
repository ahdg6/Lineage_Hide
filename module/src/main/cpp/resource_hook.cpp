#include "resource_hook.h"
#include "config.h"
#include "jni_strings.h"
#include <algorithm>

#include <cstdint>

#include "log.h"

namespace {
using GetResourceIdentifierFn = jint (*)(JNIEnv *, jclass, jlong, jstring, jstring, jstring);
using GetResourceValueFn = jint (*)(JNIEnv *, jclass, jlong, jint, jshort, jobject, jboolean);
using GetResourceNameFn = jstring (*)(JNIEnv *, jclass, jlong, jint);

GetResourceIdentifierFn g_original_identifier = nullptr;
GetResourceValueFn g_original_value = nullptr;
GetResourceNameFn g_original_name = nullptr;
GetResourceNameFn g_original_package_name = nullptr;
GetResourceNameFn g_original_type_name = nullptr;
GetResourceNameFn g_original_entry_name = nullptr;
bool g_resource_hook_installed = false;

bool is_lineage_resource_id(jint res_id) {
    return lh::resource_id_matches(static_cast<uint32_t>(res_id));
}

bool is_lineage_def_package(JNIEnv *env, jstring def_package) {
    if (!env || !def_package) return false;
    const auto &packages = lh::active_config().resource_packages;
    if (packages.empty()) return false;
    const jsize length = env->GetStringLength(def_package);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    if (length <= 0 || !lh::resource_package_length_matches(static_cast<size_t>(length))) return false;
    char buffer[255];
    const auto name = lh::read_ascii(env, def_package, buffer, length);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return std::find(packages.begin(), packages.end(), name) != packages.end();
}

// Every successful native hook takes the original function pointer; a null or
// unchanged fnPtr means the method/signature was not registered and the entry
// was left untouched.
bool took_original(void *fn_ptr, void *hook) {
    return fn_ptr != nullptr && fn_ptr != hook;
}

jint hook_get_resource_identifier(JNIEnv *env, jclass clazz, jlong ptr, jstring name,
                                  jstring def_type, jstring def_package) {
    if (!g_original_identifier) return 0;
    if (is_lineage_def_package(env, def_package)) return 0;
    const jint result = g_original_identifier(env, clazz, ptr, name, def_type, def_package);
    return is_lineage_resource_id(result) ? 0 : result;
}

jint hook_get_resource_value(JNIEnv *env, jclass clazz, jlong ptr, jint res_id, jshort density,
                             jobject out_value, jboolean resolve_refs) {
    if (!g_original_value) return 0;
    if (is_lineage_resource_id(res_id)) return 0;
    return g_original_value(env, clazz, ptr, res_id, density, out_value, resolve_refs);
}

jstring hook_get_resource_name(JNIEnv *env, jclass clazz, jlong ptr, jint res_id) {
    if (!g_original_name || is_lineage_resource_id(res_id)) return nullptr;
    return g_original_name(env, clazz, ptr, res_id);
}

jstring hook_get_resource_package_name(JNIEnv *env, jclass clazz, jlong ptr, jint res_id) {
    if (!g_original_package_name || is_lineage_resource_id(res_id)) return nullptr;
    return g_original_package_name(env, clazz, ptr, res_id);
}

jstring hook_get_resource_type_name(JNIEnv *env, jclass clazz, jlong ptr, jint res_id) {
    if (!g_original_type_name || is_lineage_resource_id(res_id)) return nullptr;
    return g_original_type_name(env, clazz, ptr, res_id);
}

jstring hook_get_resource_entry_name(JNIEnv *env, jclass clazz, jlong ptr, jint res_id) {
    if (!g_original_entry_name || is_lineage_resource_id(res_id)) return nullptr;
    return g_original_entry_name(env, clazz, ptr, res_id);
}
} // namespace

bool install_resource_hook(JNIEnv *env, zygisk::Api *api) {
    if (g_resource_hook_installed) return true;
    if (!lh::active_config().has_resource_rules()) return false;
    if (!env || !api) return false;

    JNINativeMethod methods[] = {
        {"nativeGetResourceIdentifier", "(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
         reinterpret_cast<void *>(hook_get_resource_identifier)},
        {"nativeGetResourceValue", "(JISLandroid/util/TypedValue;Z)I",
         reinterpret_cast<void *>(hook_get_resource_value)},
        {"nativeGetResourceName", "(JI)Ljava/lang/String;",
         reinterpret_cast<void *>(hook_get_resource_name)},
        {"nativeGetResourcePackageName", "(JI)Ljava/lang/String;",
         reinterpret_cast<void *>(hook_get_resource_package_name)},
        {"nativeGetResourceTypeName", "(JI)Ljava/lang/String;",
         reinterpret_cast<void *>(hook_get_resource_type_name)},
        {"nativeGetResourceEntryName", "(JI)Ljava/lang/String;",
         reinterpret_cast<void *>(hook_get_resource_entry_name)},
    };
    api->hookJniNativeMethods(env, "android/content/res/AssetManager", methods,
                              static_cast<int>(sizeof(methods) / sizeof(methods[0])));

    const bool identifier_ready =
        took_original(methods[0].fnPtr, reinterpret_cast<void *>(hook_get_resource_identifier));
    const bool value_ready =
        took_original(methods[1].fnPtr, reinterpret_cast<void *>(hook_get_resource_value));
    if (identifier_ready) {
        g_original_identifier = reinterpret_cast<GetResourceIdentifierFn>(methods[0].fnPtr);
    }
    if (value_ready) {
        g_original_value = reinterpret_cast<GetResourceValueFn>(methods[1].fnPtr);
    }

    // Name lookups are only used for diagnostics and formatting; keep them as
    // best-effort so a signature change on an odd ROM cannot disable the value
    // filtering above.
    if (took_original(methods[2].fnPtr, reinterpret_cast<void *>(hook_get_resource_name))) {
        g_original_name = reinterpret_cast<GetResourceNameFn>(methods[2].fnPtr);
    }
    if (took_original(methods[3].fnPtr, reinterpret_cast<void *>(hook_get_resource_package_name))) {
        g_original_package_name = reinterpret_cast<GetResourceNameFn>(methods[3].fnPtr);
    }
    if (took_original(methods[4].fnPtr, reinterpret_cast<void *>(hook_get_resource_type_name))) {
        g_original_type_name = reinterpret_cast<GetResourceNameFn>(methods[4].fnPtr);
    }
    if (took_original(methods[5].fnPtr, reinterpret_cast<void *>(hook_get_resource_entry_name))) {
        g_original_entry_name = reinterpret_cast<GetResourceNameFn>(methods[5].fnPtr);
    }

    // Even a names-only partial installation must retain its originals.
    // Otherwise those installed callbacks would return null for every resource.
    g_resource_hook_installed = identifier_ready || value_ready || g_original_name ||
        g_original_package_name || g_original_type_name || g_original_entry_name;
    if (!g_resource_hook_installed) {
        log_error("AssetManager resource hook unavailable; resources stay visible");
        return false;
    }
    log_debug("AssetManager resource hook installed; configured resource packages hidden "
             "(identifier=%d value=%d name=%d)",
             identifier_ready ? 1 : 0, value_ready ? 1 : 0, g_original_name ? 1 : 0);
    return true;
}
