#pragma once

void log_info(const char *fmt, ...);
void log_error(const char *fmt, ...);

// Per-hook setup details are useful during porting, but need not produce
// several logd writes on every application start in a normal build.
#ifdef LH_VERBOSE_LOGS
#define log_debug(...) log_info(__VA_ARGS__)
#else
#define log_debug(...) ((void)0)
#endif
