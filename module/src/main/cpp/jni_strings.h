#pragma once
#include <jni.h>
#include <string_view>

namespace lh {
// Read bounded names without pinning Java strings or allocating native strings.
// The caller owns the result buffer. Preserve the existing ASCII substitution
// for non-ASCII code units; configured rule names themselves are ASCII only.
template <size_t N>
std::string_view read_ascii(JNIEnv *env, jstring value, char (&buffer)[N], jsize length = -1) {
    if (!env || !value) return {};
    if (length < 0) length = env->GetStringLength(value);
    if (env->ExceptionCheck() || length <= 0 || static_cast<size_t>(length) > N) return {};
    jchar chars[N];
    env->GetStringRegion(value, 0, length, chars);
    if (env->ExceptionCheck()) return {};
    for (jsize i = 0; i < length; ++i) buffer[i] = chars[i] <= 0x7f ? static_cast<char>(chars[i]) : '?';
    return {buffer, static_cast<size_t>(length)};
}
} // namespace lh
