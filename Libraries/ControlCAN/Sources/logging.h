// logging.h
// Logging functions for ControlCAN
// C++20
// ---------------------------------------------------------------------------

// ZLG ControlCAN header
#include "ControlCAN.h"

// ---------------------------------------------------------------------------

void InitLog(const char* controlCanLogFileEnvName);

void Log(const char* fmt, ...);

void LogCANFrame(const char* prefix, const VCI_CAN_OBJ& f);
