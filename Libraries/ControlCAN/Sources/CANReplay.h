// CANReplay.h
// Simulates receiving CAN frames (which are read from a file defined CONTROLCAN_REPLAY_FILE env variable).
// C++20.
// -----------------------------------------------------------------------------

// ZLG ControlCAN header
#include "ControlCAN.h"

// -----------------------------------------------------------------------------

// Initialize frame replay if replayFileEnvName environment variable is defined.
void InitReceiveReplay(const char* replayFileEnvName);

// Check if receive replay is active.
bool IsReceiveReplayActive();

DWORD VCI_ReceiveReplay(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd,
                            PVCI_CAN_OBJ out, DWORD maxCount, INT waitTime);
