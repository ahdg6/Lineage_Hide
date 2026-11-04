#pragma once

#include <jni.h>

#include "zygisk.hpp"

// Installs the preferred BinderProxy.transactNative hook. This path receives
// the framework-owned Java Parcel objects directly, does not take native
// ownership, and leaves libbinder's PLT/GOT relocation tables untouched.
// Returns false when the framework API is unavailable, in which case Binder
// filtering is skipped for this process.
// Uses the immutable active configuration set before installation.
bool install_jni_hook(JNIEnv *env, zygisk::Api *api);
