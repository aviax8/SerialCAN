// ControlCAN.cpp
// Wrapper ControlCAN.dll → CAN API V3 (SerialCAN.dll)
// C++20, Windows, all comments in English

#include <windows.h>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <mutex>

#include "ControlCAN.h"
#include "can_api.h"       // from CAN API V3
#include "CANAPI_Types.h"  // CAN message types

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

// CAN interface handle from CAN API V3
static int g_canHandle = CANAPI_HANDLE;

// SerialCAN.dll module handle
static HMODULE g_hSerialCanDll = nullptr;

// Mutex for receive operations
static std::mutex g_rxMutex;

// Cached COM port string (from environment variable)
static std::string g_comPort;

// ---------------------------------------------------------------------------
// Helper: load SerialCAN.dll dynamically
// ---------------------------------------------------------------------------

static bool LoadSerialCanDll()
{
    if (g_hSerialCanDll)
        return true;

    g_hSerialCanDll = LoadLibraryA("SerialCAN.dll");

    return (g_hSerialCanDll != nullptr);
}

// ---------------------------------------------------------------------------
// Helper: fetch COM port from environment variable SLCAN_PORT
// ---------------------------------------------------------------------------

static bool FetchComPort()
{
    char* env = nullptr;
    size_t len = 0;

    if (_dupenv_s(&env, &len, "SLCAN_PORT") == 0 && env != nullptr)
    {
        g_comPort = env;
        free(env);
    }
    else
    {
        // default fallback (optional)
        g_comPort = "COM1";
    }

    return true;
}

// ---------------------------------------------------------------------------
// Helper: convert VCI_CAN_OBJ → can_message_t
// ---------------------------------------------------------------------------

static void ConvertToCANAPI(const VCI_CAN_OBJ& in, can_message_t& out)
{
    memset(&out, 0, sizeof(out));

    out.id = in.ID;
    out.xtd = (in.ExternFlag != 0);
    out.rtr = (in.RemoteFlag != 0);
    out.len = in.DataLen;

    memcpy(out.data, in.Data, in.DataLen);
}

// ---------------------------------------------------------------------------
// Helper: convert can_message_t → VCI_CAN_OBJ
// ---------------------------------------------------------------------------

static void ConvertFromCANAPI(const can_message_t& in, VCI_CAN_OBJ& out)
{
    memset(&out, 0, sizeof(out));

    out.ID = in.id;
    out.ExternFlag = in.xtd ? 1 : 0;
    out.RemoteFlag = in.rtr ? 1 : 0;
    out.DataLen    = in.len;

    memcpy(out.Data, in.data, in.len);
}

// ---------------------------------------------------------------------------
// VCI_OpenDevice
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport)
DWORD __stdcall VCI_OpenDevice(DWORD DeviceType, DWORD DeviceInd, DWORD Reserved)
{
    // Load SerialCAN.dll dynamically
    if (!LoadSerialCanDll())
        return STATUS_ERR;

    // Fetch COM port from environment variable
    FetchComPort();

    // Prepare CAN API param structure
    can_sio_attr_t sio{};
    sio.device = g_comPort.c_str();
    sio.baud = 115200;   // typical for SLCAN adapters
    sio.mode = CANMODE_DEFAULT;

    // Initialize channel: library = SERIALCAN_LIBRARY_ID, channel=0
    int result = can_init(SERIALCAN_LIBRARY_ID, 0, CANMODE_DEFAULT, &sio);
    if (result < 0)
        return STATUS_ERR;

    g_canHandle = result;
    return STATUS_OK;
}

// ---------------------------------------------------------------------------
// VCI_CloseDevice
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport)
DWORD __stdcall VCI_CloseDevice(DWORD DeviceType, DWORD DeviceInd)
{
    if (g_canHandle >= 0)
    {
        can_exit(g_canHandle);
        g_canHandle = CANAPI_HANDLE;
    }

    if (g_hSerialCanDll)
    {
        FreeLibrary(g_hSerialCanDll);
        g_hSerialCanDll = nullptr;
    }

    return STATUS_OK;
}

// ---------------------------------------------------------------------------
// VCI_InitCAN → do nothing special (bitrate setup happens in VCI_StartCAN)
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport)
DWORD __stdcall VCI_InitCAN(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd, PVCI_INIT_CONFIG pInitConfig)
{
    // We only prepare bitrate in VCI_StartCAN.
    return STATUS_OK;
}

// ---------------------------------------------------------------------------
// VCI_StartCAN → can_start()
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport)
DWORD __stdcall VCI_StartCAN(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd)
{
    if (g_canHandle < 0)
        return STATUS_ERR;

    // CAN bitrate: 500 kbit/s default (just an example)
    can_bitrate_t bitrate{};
    bitrate.type = CANBTR_INDEX;
    bitrate.index = 6; // 500k in CANBTR_Defaults.h

    if (can_start(g_canHandle, &bitrate) < 0)
        return STATUS_ERR;

    return STATUS_OK;
}

// ---------------------------------------------------------------------------
// VCI_Transmit → can_write()
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport)
ULONG __stdcall VCI_Transmit(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd,
                             PVCI_CAN_OBJ pSend, ULONG Len)
{
    if (g_canHandle < 0 || !pSend)
        return 0;

    ULONG sent = 0;

    for (ULONG i = 0; i < Len; ++i)
    {
        can_message_t msg{};
        ConvertToCANAPI(pSend[i], msg);

        if (can_write(g_canHandle, &msg, 0) == 0)
            ++sent;
        else
            break;
    }

    return sent;
}

// ---------------------------------------------------------------------------
// VCI_Receive → can_read()
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport)
ULONG __stdcall VCI_Receive(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd,
                            PVCI_CAN_OBJ pReceive, ULONG Len, INT WaitTime)
{
    if (g_canHandle < 0 || !pReceive || Len == 0)
        return 0;

    std::lock_guard lock(g_rxMutex);

    ULONG count = 0;

    for (ULONG i = 0; i < Len; ++i)
    {
        can_message_t msg{};

        uint16_t timeout =
            (WaitTime < 0) ? CANWAIT_INFINITE :
            (WaitTime == 0) ? 0 :
            (uint16_t)WaitTime;

        int r = can_read(g_canHandle, &msg, timeout);
        if (r < 0)
            break;

        ConvertFromCANAPI(msg, pReceive[count]);
        ++count;
    }

    return count;
}

// ---------------------------------------------------------------------------
// VCI_ClearBuffer → can_reset() (resets controller, clears queues)
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport)
DWORD __stdcall VCI_ClearBuffer(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd)
{
    if (g_canHandle < 0)
        return STATUS_ERR;

    can_reset(g_canHandle);
    return STATUS_OK;
}

// ---------------------------------------------------------------------------
// VCI_SetReference → can_property()
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport)
DWORD __stdcall VCI_SetReference(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd,
                                 DWORD RefType, PVOID pData)
{
    if (g_canHandle < 0)
        return STATUS_ERR;

    // Size is unknown → assume 4 bytes unless user wants otherwise
    int r = can_property(g_canHandle, (uint16_t)RefType, pData, 4);
    return (r < 0) ? STATUS_ERR : STATUS_OK;
}

// ---------------------------------------------------------------------------
// VCI_ReadErrInfo → queries status via can_status()
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport)
DWORD __stdcall VCI_ReadErrInfo(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd,
                                PVCI_ERR_INFO pErrInfo)
{
    if (g_canHandle < 0 || !pErrInfo)
        return STATUS_ERR;

    uint8_t st = 0;
    if (can_status(g_canHandle, &st) < 0)
        return STATUS_ERR;

    memset(pErrInfo, 0, sizeof(VCI_ERR_INFO));

    // Map basic errors
    if (st & CANSTAT_BUSOFF)  pErrInfo->ErrCode |= ERR_CAN_BUSOFF;
    if (st & CANSTAT_ERRLIM)  pErrInfo->ErrCode |= ERR_CAN_PASSIVE;

    return STATUS_OK;
}

