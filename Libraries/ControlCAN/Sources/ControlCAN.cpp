// ControlCAN.cpp
// Wrapper for ZLG ControlCAN.dll -> CAN API V3 (SerialCAN)
// C++20, no WinAPI (only std C/C++), unbuffered logging, clean style.
// Exported API matches exactly the ZLG ControlCAN interface.
//
// Implemented functions:
//   VCI_OpenDevice
//   VCI_CloseDevice
//   VCI_InitCAN
//   VCI_StartCAN
//   VCI_Transmit
//   VCI_Receive
//   VCI_ClearBuffer
//   VCI_SetReference
//   VCI_GetReference
//   VCI_ReadErrInfo
//   VCI_ReadBoardInfo
//   VCI_ReadCANStatus
//   VCI_GetReceiveNum
//   VCI_ResetCAN
//

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

// ZLG ControlCAN header
#include "ControlCAN.h"

// CAN API V3 – driver mode
#define OPTION_CANAPI_DRIVER 1
#include "can_api.h"
#include "can_btr.h"
#include "CANAPI_Defines.h"
#include "SerialCAN_Defines.h"

// -----------------------------------------------------------------------------
// Environment variables
// -----------------------------------------------------------------------------

static constexpr const char* ENV_CONTROLCAN_LOG {"CONTROLCAN_LOG"};
static constexpr const char* ENV_SLCAN_PORT     {"CONTROLCAN_SLCAN_PORT"};

// -----------------------------------------------------------------------------
// Logging system
// -----------------------------------------------------------------------------

static std::FILE* g_logFile   = nullptr;
static bool       g_logEnabled = false;
static std::mutex g_logMutex;

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

static void Log(const char* fmt, ...)
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

static void InitLog()
{
    if (g_logEnabled) {
        return;
    }

    const char* env = std::getenv(ENV_CONTROLCAN_LOG);
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

static void LogCANFrame(const char* prefix, const VCI_CAN_OBJ& f)
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

// -----------------------------------------------------------------------------
// CAN globals
// -----------------------------------------------------------------------------

// Single CAN handle (one device)
static int           g_canHandle  = CANAPI_HANDLE;

// Last used bitrate (for potential restart in ClearBuffer)
static can_bitrate_t g_lastBitrate{};

// Indicates that can_start has been called
static bool          g_canStarted = false;

// Mutex to serialize VCI_Receive access (CAN API is usually thread-safe,
// ale aplikace ControlCAN to typicky volá z jednoho threadu – i tak ochráníme)
static std::mutex    g_rxMutex;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Get serial port from CONTROLCAN_SLCAN_PORT, default "\\.\COM1"
static std::string GetSerialPort()
{
    if (const char* env = std::getenv(ENV_SLCAN_PORT)) {
        if (*env)
            return std::string(R"(\\.\)") + env;
    }
    return R"(\\.\COM1)";
}

static void ConvertToCANAPI(const VCI_CAN_OBJ& in, can_message_t& out)
{
    std::memset(&out, 0, sizeof(out));

    out.id  = in.ID;
    out.xtd = in.ExternFlag ? 1U : 0U;
    out.rtr = in.RemoteFlag ? 1U : 0U;
    out.sts = 0U;

    out.dlc = (in.DataLen > 8U ? 8U : in.DataLen);
    std::memcpy(out.data, in.Data, out.dlc);
}

static void ConvertFromCANAPI(const can_message_t& in, VCI_CAN_OBJ& out)
{
    std::memset(&out, 0, sizeof(out));

    out.ID         = in.id;
    out.ExternFlag = in.xtd ? 1U : 0U;
    out.RemoteFlag = in.rtr ? 1U : 0U;
    out.DataLen    = (in.dlc > 8U ? 8U : in.dlc);
    out.TimeFlag   = 0;       // not using timestamp* here
    out.SendType   = 0;

    std::memcpy(out.Data, in.data, out.DataLen);
}

static const char* BitrateIndex2String(btr_index_t idx)
{
    switch (idx) {
    case CANBTR_INDEX_1M:   return "1M";
    case CANBTR_INDEX_800K: return "800K";
    case CANBTR_INDEX_500K: return "500K";
    case CANBTR_INDEX_250K: return "250K";
    case CANBTR_INDEX_125K: return "125K";
    case CANBTR_INDEX_100K: return "100K";
    case CANBTR_INDEX_50K:  return "50K";
    case CANBTR_INDEX_20K:  return "20K";
    case CANBTR_INDEX_10K:  return "10K";
    default:                return "UNKNOWN";
    }
}

// -----------------------------------------------------------------------------
// Exported C API
// -----------------------------------------------------------------------------

extern "C" {

// -------------------------------------------------------------------------
// VCI_OpenDevice
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_OpenDevice(DWORD DeviceType, DWORD DeviceInd, DWORD Reserved)
{
    (void)Reserved;

    InitLog();
    Log("VCI_OpenDevice: DeviceType=%lu  DeviceIndex=%lu  CANInd=%lu", DeviceType, DeviceInd);

    if (g_canHandle >= 0) {
        Log("  Device already open, handle=%d", g_canHandle);
        return STATUS_OK;
    }

    const std::string port = GetSerialPort();
    Log("  Serial port = %s", port.c_str());

    can_sio_param_t param{};
    param.name          = const_cast<char*>(port.c_str());
    param.attr.protocol = CANSIO_CANABLE;
    param.attr.baudrate = CANSIO_BD57600;   // typical SLCAN UART speed (WeAct cangaroo sets CANSIO_BD1000000)
    param.attr.bytesize = CANSIO_8DATABITS;
    param.attr.parity   = CANSIO_NOPARITY;
    param.attr.stopbits = CANSIO_1STOPBIT;

    const int h = can_init(
        CAN_BOARD(CANLIB_SERIALCAN, CANDEV_SERIAL),
        CANMODE_DEFAULT,
        &param
    );

    Log("  can_init() -> %d", h);

    if (h < 0) {
        return STATUS_ERR;
    }

    g_canHandle  = h;
    g_lastBitrate = {};
    g_lastBitrate.index = CANBTR_INDEX_250K; // default, will be overridden by VCI_InitCAN

    return STATUS_OK;
}

// -------------------------------------------------------------------------
// VCI_CloseDevice
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_CloseDevice(DWORD DeviceType, DWORD DeviceInd)
{
    Log("VCI_CloseDevice: DeviceType=%lu  DeviceIndex=%lu", DeviceType, DeviceInd);

    if (g_canHandle >= 0) {
        const int r = can_exit(g_canHandle);
        Log("  can_exit(handle=%d) -> %d", g_canHandle, r);
    }

    g_canHandle  = CANAPI_HANDLE;
    g_canStarted = false;
    return STATUS_OK;
}

// -------------------------------------------------------------------------
// VCI_InitCAN
//   Map ZLG Timing0/Timing1 (SJA1000 BTR) to CANBTR_INDEX_XXX.
//   If no matching index is found, return STATUS_ERR.
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_InitCAN(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd,
                            PVCI_INIT_CONFIG cfg)
{
    if (!cfg) {
        Log("VCI_InitCAN: cfg == NULL");
        return STATUS_ERR;
    }

    Log("VCI_InitCAN: DeviceType=%lu  DeviceIndex=%lu  CANInd=%lu  AccCode=0x%08lX  AccMask=0x%08lX  Filter=%u  Timing0=0x%02X  Timing1=0x%02X  Mode=%u",
        DeviceType, DeviceInd, CANInd, cfg->AccCode, cfg->AccMask, cfg->Filter, cfg->Timing0, cfg->Timing1, cfg->Mode);

    // Build SJA1000 BTR register from Timing0/1
    const uint16_t btr0btr1 =
        static_cast<uint16_t>(cfg->Timing0) << 8 |
        static_cast<uint16_t>(cfg->Timing1);

    Log("  Combined BTR value: 0x%04X", btr0btr1);

    can_bitrate_t br{};
    int r = btr_sja10002bitrate(btr0btr1, &br);
    Log("  btr_sja10002bitrate() -> %d", r);
    if (r < 0) {
        Log("  No matching SJA1000 bit timing for 0x%04X", btr0btr1);
        return STATUS_ERR;
    }

    btr_index_t brIndex{};
    r = btr_bitrate2index(&br, &brIndex);
    Log("  btr_bitrate2index() -> %d", r);
    if (r < 0) {
        Log("  No matching CANBTR_INDEX for 0x%04X", btr0btr1);
        return STATUS_ERR;
    }

    g_lastBitrate = {};
    g_lastBitrate.index = brIndex;

    Log("  BTR matched index=%d (%s)", brIndex, BitrateIndex2String(brIndex));

    return STATUS_OK;
}

// -------------------------------------------------------------------------
// VCI_StartCAN
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_StartCAN(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd)
{
    Log("VCI_StartCAN: DeviceType=%lu  DeviceIndex=%lu  CANInd=%lu", DeviceType, DeviceInd, CANInd);

    if (g_canHandle < 0) {
        Log("  No CAN handle (device not open)");
        return STATUS_ERR;
    }

    const int r = can_start(g_canHandle, &g_lastBitrate);
    Log("  can_start(handle=%d, index=%d) -> %d",
        g_canHandle, g_lastBitrate.index, r);

    if (r < 0)
        return STATUS_ERR;

    g_canStarted = true;
    return STATUS_OK;
}

// -------------------------------------------------------------------------
// VCI_ResetCAN
//   Stop controller (maps to can_reset).
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_ResetCAN(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd)
{
    Log("VCI_ResetCAN: DeviceType=%lu  DeviceIndex=%lu  CANInd=%lu", DeviceType, DeviceInd, CANInd);

    if (g_canHandle < 0)
        return STATUS_ERR;

    const int r = can_reset(g_canHandle);
    Log("  can_reset(handle=%d) -> %d", g_canHandle, r);

    if (r < 0 && r != CANERR_OFFLINE)
        return STATUS_ERR;

    g_canStarted = false;
    return STATUS_OK;
}

// -------------------------------------------------------------------------
// VCI_Transmit
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_Transmit(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd,
                             PVCI_CAN_OBJ frames, DWORD count)
{
    Log("VCI_Transmit: DeviceType=%lu  DeviceIndex=%lu  CANInd=%lu  sending %lu frame(s)", DeviceType, DeviceInd, CANInd, count);

    if (g_canHandle < 0 || !frames || count == 0)
        return 0;

    DWORD sent = 0;

    for (DWORD i = 0; i < count; ++i) {
        LogCANFrame("  TX:", frames[i]);

        can_message_t msg{};
        ConvertToCANAPI(frames[i], msg);

        int r;
        do {
            r = can_write(g_canHandle, &msg, 0);
        } while (r == CANERR_TX_BUSY);

        if (r < CANERR_NOERROR) {
            Log("  can_write failed at frame %lu, r=%d", i, r);
            break;
        }

        ++sent;
    }

    return sent;
}

// -------------------------------------------------------------------------
// VCI_Receive
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_Receive(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd,
                            PVCI_CAN_OBJ out, DWORD maxCount, INT waitTime)
{
    Log("VCI_Receive: DeviceType=%lu  DeviceIndex=%lu  CANInd=%lu  request %lu frame(s), waitTime=%d", DeviceType, DeviceInd, CANInd, maxCount, waitTime);

    if (g_canHandle < 0 || !out || maxCount == 0)
        return 0;


    DWORD received = 0;
    std::lock_guard lock(g_rxMutex);

    for (DWORD i = 0; i < maxCount; ++i) {
        can_message_t msg{};
        const uint16_t timeout =
            (waitTime < 0) ? CANWAIT_INFINITE :
            (waitTime == 0) ? 0U :
            static_cast<uint16_t>(waitTime);

        const int r = can_read(g_canHandle, &msg, timeout);

        if (r == CANERR_RX_EMPTY) {
            break;
        }
        if (r < CANERR_NOERROR) {
            Log("  can_read error r=%d", r);
            break;
        }

        ConvertFromCANAPI(msg, out[received]);
        LogCANFrame("  RX:", out[received]);
        ++received;
    }

    return received;
}

// -------------------------------------------------------------------------
// VCI_ClearBuffer
//   Reset controller and optionally restart with last bitrate.
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_ClearBuffer(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd)
{
    Log("VCI_ClearBuffer: DeviceType=%lu  DeviceIndex=%lu  CANInd=%lu", DeviceType, DeviceInd, CANInd);

    if (g_canHandle < 0)
        return STATUS_ERR;

    const int r1 = can_reset(g_canHandle);
    Log("  can_reset(handle=%d) -> %d", g_canHandle, r1);

    int r2 = 0;
    if (g_canStarted) {
        r2 = can_start(g_canHandle, &g_lastBitrate);
        Log("  can_start(handle=%d, index=%d) -> %d",
            g_canHandle, g_lastBitrate.index, r2);
    }

    if ((r1 < 0 && r1 != CANERR_OFFLINE) || r2 < 0)
        return STATUS_ERR;

    return STATUS_OK;
}

// -------------------------------------------------------------------------
// VCI_SetReference
//   Simple pass-through to can_property (write).
//   NOTE: assumes 4-byte values (DWORD) for most RefTypes.
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_SetReference(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd,
                                 DWORD RefType, PVOID data)
{
    Log("VCI_SetReference: DeviceType=%lu  DeviceIndex=%lu  CANInd=%lu  RefType=%lu", DeviceType, DeviceInd, CANInd, RefType);

    if (g_canHandle < 0 || !data)
        return STATUS_ERR;

    // We don't know size; many ZLG refs are DWORD, thus defaults to 4 bytes.
    const int r = can_property(
        g_canHandle,
        static_cast<uint16_t>(RefType),
        data,
        4U
    );

    Log("  can_property(SET, RefType=%lu) -> %d", RefType, r);

    return (r < 0) ? STATUS_ERR : STATUS_OK;
}

// -------------------------------------------------------------------------
// VCI_GetReference
//   Simple pass-through to can_property (read).
//   NOTE: assumes 4-byte values (DWORD) for most RefTypes.
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_GetReference(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd,
                                 DWORD RefType, PVOID data)
{
    Log("VCI_GetReference: DeviceType=%lu  DeviceIndex=%lu  CANInd=%lu  RefType=%lu", DeviceType, DeviceInd, CANInd, RefType);

    if (g_canHandle < 0 || !data)
        return STATUS_ERR;

    const int r = can_property(
        g_canHandle,
        static_cast<uint16_t>(RefType),
        data,
        4U
    );

    Log("  can_property(GET, RefType=%lu) -> %d", RefType, r);

    return (r < 0) ? STATUS_ERR : STATUS_OK;
}

// -------------------------------------------------------------------------
// VCI_ReadErrInfo
//   Map basic error status bits from CAN API to ZLG error code.
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_ReadErrInfo(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd,
                                PVCI_ERR_INFO out)
{
    Log("VCI_ReadErrInfo: DeviceType=%lu  DeviceIndex=%lu  CANInd=%lu", DeviceType, DeviceInd, CANInd);

    if (g_canHandle < 0 || !out)
        return STATUS_ERR;

    std::memset(out, 0, sizeof(VCI_ERR_INFO));

    uint8_t status = 0;
    const int r = can_status(g_canHandle, &status);
    Log("  can_status(handle=%d) -> %d, status=0x%02X", g_canHandle, r, status);

    if (r < 0)
        return STATUS_ERR;

#ifdef CANSTAT_BUSOFF
    if (status & CANSTAT_BUSOFF)
        out->ErrCode |= ERR_CAN_BUSOFF;
#endif
#ifdef CANSTAT_ERRLIM
    if (status & CANSTAT_ERRLIM)
        out->ErrCode |= ERR_CAN_PASSIVE;
#endif
    // Additional bits (overflow, etc.) could be mapped here if needed.

    return STATUS_OK;
}

// -------------------------------------------------------------------------
// VCI_ReadBoardInfo
//   Fill VCI_BOARD_INFO with minimal, generic information.
//   Detailed mapping depends on available CAN API properties.
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_ReadBoardInfo(DWORD DeviceType, DWORD DeviceInd,
                                  PVCI_BOARD_INFO info)
{
    Log("VCI_ReadBoardInfo: DeviceType=%lu  DeviceIndex=%lu", DeviceType, DeviceInd);

    if (g_canHandle < 0 || !info)
        return STATUS_ERR;

    std::memset(info, 0, sizeof(VCI_BOARD_INFO));

    // We try to get device name via CAN API properties if available.
    char name[40] = {};
    if (can_property(g_canHandle, CANPROP_GET_DEVICE_NAME, name, sizeof(name)) >= 0) {
        std::strncpy(info->str_hw_Type, name, sizeof(info->str_hw_Type) - 1);
    } else {
        std::strncpy(info->str_hw_Type, "SerialCAN", sizeof(info->str_hw_Type) - 1);
    }


    std::strncpy(info->str_Serial_Num, "N/A", sizeof(info->str_Serial_Num) - 1);

    info->can_Num = 1;
    return STATUS_OK;
}

// -------------------------------------------------------------------------
// VCI_ReadCANStatus
//   Return basic controller status. We only map the main status byte.
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_ReadCANStatus(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd,
                                  PVCI_CAN_STATUS status)
{
    Log("VCI_ReadCANStatus: DeviceType=%lu  DeviceIndex=%lu  CANInd=%lu", DeviceType, DeviceInd, CANInd);

    if (g_canHandle < 0 || !status)
        return STATUS_ERR;

    std::memset(status, 0, sizeof(VCI_CAN_STATUS));

    uint8_t st = 0;
    const int r = can_status(g_canHandle, &st);
    Log("  can_status(handle=%d) -> %d, status=0x%02X", g_canHandle, r, st);

    if (r < 0)
        return STATUS_ERR;

    status->regStatus = st;
    return STATUS_OK;
}

// -------------------------------------------------------------------------
// VCI_GetReceiveNum
//   Try to use CAN API property for RX queue level if available.
//   If not supported, fall back to 0 (no information).
// -------------------------------------------------------------------------

__declspec(dllexport)
DWORD __stdcall VCI_GetReceiveNum(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd)
{
    Log("VCI_GetReceiveNum: DeviceType=%lu  DeviceIndex=%lu  CANInd=%lu", DeviceType, DeviceInd, CANInd);

    if (g_canHandle < 0)
        return 0;

    // Fallback: return 0 if not supported.
    Log("  Receive queue level not supported, returning 0");
    return 0;
}

} // extern "C"
