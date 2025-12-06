// logging.cpp
// Logging functions for ControlCAN
// C++20
// ---------------------------------------------------------------------------

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#endif

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <string>
#include <format>

#include "logging.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static std::FILE* g_logFile   = nullptr;
static bool       g_logEnabled = false;
static std::mutex g_logMutex;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string FormatTimestamp()
{
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};

#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    return std::format(
        "{:02}:{:02}:{:02}.{:03}",
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec,
        static_cast<int>(ms.count())
    );
}

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

void Log(const char* fmt, ...)
{
    if (!g_logEnabled || !g_logFile)
        return;

    std::lock_guard lock(g_logMutex);

    std::fprintf(g_logFile, "%s  ", FormatTimestamp().c_str());

    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_logFile, fmt, args);
    va_end(args);

    std::fprintf(g_logFile, "\n");
    std::fflush(g_logFile);
}

void InitLog(const char* controlCanLogFileEnvName)
{
    if (g_logEnabled) {
        return;
    }

    const char* env = std::getenv(controlCanLogFileEnvName);
    if (!env || std::strcmp(env, "1") != 0) {
        g_logEnabled = false;
        return;
    }

    g_logFile = std::fopen("ControlCAN.log", "w");
    if (!g_logFile) {
        g_logEnabled = false;
        return;
    }

    // Disable stdio buffering for the log file (unbuffered logging)
    setvbuf(g_logFile, nullptr, _IONBF, 0);
    g_logEnabled = true;

    Log("Logging enabled");
}

void LogCANFrame(const char* prefix, const VCI_CAN_OBJ& f)
{
    if (!g_logEnabled || !g_logFile)
        return;

    std::lock_guard lock(g_logMutex);

    std::fprintf(
        g_logFile,
        "%s  %s ID=0x%08X %s %s DLC=%u DATA:",
        FormatTimestamp().c_str(),
        prefix,
        f.ID,
        f.ExternFlag ? "EXT" : "STD",
        f.RemoteFlag ? "RTR" : "DATA",
        f.DataLen
    );

    for (unsigned i = 0; i < f.DataLen && i < 8; ++i)
        std::fprintf(g_logFile, " %02X", f.Data[i]);

    std::fprintf(g_logFile, "\n");
    std::fflush(g_logFile);
}
