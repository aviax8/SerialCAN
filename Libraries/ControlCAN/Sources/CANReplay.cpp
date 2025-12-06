// CANReplay.cpp
// Simulates receiving CAN frames (which are read from a file defined CONTROLCAN_REPLAY_FILE env variable).
// C++20.
// -----------------------------------------------------------------------------
//
// CONTROLCAN_REPLAY_FILE format:
//
// # ID and data are hex without 0x, space separated
// # ID   DLC  DATA...
// 123    8    11 22 33 44 55 66 77 88
// 1AB    4    DE AD BE EF
//
// Format:
//   * simple standard frame:
//      ID DLC B0 B1 ... B7   (hex numbers, ID and data bytes in hex)
//   * extended frame with comment at end:
//      18DAF110 X 8 01 02 03 04 05 06 07 08
//   *  remote frame:
//      200 R 0
//
// -----------------------------------------------------------------------------

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#endif

#include <sstream>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <vector>

#include "logging.h"
#include "CANReplay.h"

// -----------------------------------------------------------------------------
// Replay-from-file support for VCI_Receive
// -----------------------------------------------------------------------------

static std::string              g_replayFilePath;
static std::vector<VCI_CAN_OBJ> g_replayFrames;
static std::size_t              g_replayIndex = 0;
static std::mutex               g_replayMutex;

// Parse one text line into VCI_CAN_OBJ.
// Format:
//   * simple standard frame:
//      ID DLC B0 B1 ... B7   (hex numbers, ID and data bytes in hex)
//   * extended frame with comment at end:
//      18DAF110 X 8 01 02 03 04 05 06 07 08
//   *  remote frame:
//      200 R 0
static bool ParseReplayLine(std::string line, VCI_CAN_OBJ& out)
{
    // Remove comments (# or ;)
    const auto posHash = line.find('#');
    const auto posSemi = line.find(';');
    const auto pos =
        (posHash == std::string::npos) ? posSemi :
        (posSemi == std::string::npos) ? posHash :
        std::min(posHash, posSemi);

    if (pos != std::string::npos)
        line.erase(pos);

    // Trim whitespace
    auto trim = [](std::string& s) {
        while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
        while (!s.empty() && std::isspace((unsigned char)s.back()))  s.pop_back();
    };

    trim(line);
    if (line.empty())
        return false;

    // Tokenize
    std::istringstream iss(line);
    std::vector<std::string> tok;
    std::string t;
    while (iss >> t)
        tok.push_back(t);

    if (tok.size() < 2)
        return false;

    std::memset(&out, 0, sizeof(out));

    // ID (hex)
    out.ID = static_cast<UINT>(std::stoul(tok[0], nullptr, 16));

    size_t idx = 1;

    // Optional flags
    while (idx < tok.size()) {
        if (tok[idx] == "X" || tok[idx] == "x") {
            out.ExternFlag = 1;
            idx++;
        } else if (tok[idx] == "R" || tok[idx] == "r") {
            out.RemoteFlag = 1;
            idx++;
        } else {
            break;
        }
    }

    if (idx >= tok.size())
        return false;

    // DLC
    out.DataLen = static_cast<BYTE>(std::stoi(tok[idx++]));
    if (out.DataLen > 8)
        out.DataLen = 8;

    // DATA
    for (BYTE i = 0; i < out.DataLen && idx < tok.size(); ++i, ++idx) {
        out.Data[i] = static_cast<BYTE>(std::stoul(tok[idx], nullptr, 16));
    }

    return true;
}

// Load replay file into memory (vector of frames).
static bool LoadReplayFile()
{
    if (!IsReceiveReplayActive()) {
        return false;
    }

    Log("Replay: loading file '%s'", g_replayFilePath.c_str());

    std::ifstream ifs(g_replayFilePath);
    if (!ifs.is_open()) {
        Log("Replay: failed to open file");
        g_replayFrames.clear();
        g_replayIndex = 0;
        return false;
    }

    std::vector<VCI_CAN_OBJ> frames;
    std::string line;
    while (std::getline(ifs, line)) {
        VCI_CAN_OBJ obj{};
        if (ParseReplayLine(line, obj)) {
            frames.push_back(obj);
        }
    }

    if (frames.empty()) {
        Log("Replay: no usable frames in file");
        g_replayFrames.clear();
        g_replayIndex = 0;
        return false;
    }

    g_replayFrames = std::move(frames);
    g_replayIndex  = 0;

    Log("Replay: loaded %zu frame(s)", g_replayFrames.size());
    return true;
}

// ---------------------------------------------------------------------------

// Initialize frame replay if replayFileEnvName environment variable is defined.
void InitReceiveReplay(const char* replayFileEnvName)
{
    if (const char* env = std::getenv(replayFileEnvName)) {
        if (*env) {
            g_replayFilePath = std::string(env);
            Log("Replay: CAN adapter will not be used - Replay active with file '%s'", g_replayFilePath.c_str());
        }
    }
}

// Check if receive replay is active.
bool IsReceiveReplayActive()
{
    return !g_replayFilePath.empty();
}


DWORD VCI_ReceiveReplay(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd,
                            PVCI_CAN_OBJ out, DWORD maxCount, INT waitTime)
{
    (void)waitTime; // not used in replay mode

    if (!out || maxCount == 0)
        return 0;

    // We always send at most 2 frames per call (or less if file shorter).
    const DWORD framesPerCall = 2;
    const DWORD toSend = (maxCount < framesPerCall) ? maxCount : framesPerCall;

    std::lock_guard lock(g_replayMutex);

    // If no frames loaded yet or we've reached the end, reload file.
    if (g_replayFrames.empty() || g_replayIndex >= g_replayFrames.size()) {
        if (!LoadReplayFile()) {
            // Nothing to send
            return 0;
        }
    }

    DWORD sent = 0;

    for (DWORD i = 0; i < toSend; ++i) {
        if (g_replayIndex >= g_replayFrames.size()) {
            // End of current file contents, next call will reload again.
            break;
        }

        const VCI_CAN_OBJ& src = g_replayFrames[g_replayIndex++];
        out[sent] = src;
        LogCANFrame("  RX-REPLAY:", out[sent]);
        ++sent;
    }

    return sent;
}
