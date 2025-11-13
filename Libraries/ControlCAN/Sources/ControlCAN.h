#ifndef CONTROLCAN_H
#define CONTROLCAN_H

// Interface card type definitions
#define VCI_PCI5121         1
#define VCI_PCI9810         2
#define VCI_USBCAN1         3
#define VCI_USBCAN2         4
#define VCI_USBCAN2A        4
#define VCI_PCI9820         5
#define VCI_CAN232          6
#define VCI_PCI5110         7
#define VCI_CANLITE         8
#define VCI_ISA9620         9
#define VCI_ISA5420         10
#define VCI_PC104CAN        11
#define VCI_CANETUDP        12
#define VCI_CANETE          12
#define VCI_DNP9810         13
#define VCI_PCI9840         14
#define VCI_PC104CAN2       15
#define VCI_PCI9820I        16
#define VCI_CANETTCP        17
#define VCI_PEC9920         18
#define VCI_PCIE_9220       18
#define VCI_PCI5010U        19
#define VCI_USBCAN_E_U      20
#define VCI_USBCAN_2E_U     21
#define VCI_PCI5020U        22
#define VCI_EG20T_CAN       23
#define VCI_PCIE9221        24
#define VCI_WIFICAN_TCP     25
#define VCI_WIFICAN_UDP     26
#define VCI_PCIe9120        27
#define VCI_PCIe9110        28
#define VCI_PCIe9140        29
#define VCI_USBCAN_4E_U     31
#define VCI_CANDTU_200UR    32
#define VCI_CANDTU_MINI     33
#define VCI_USBCAN_8E_U     34
#define VCI_CANREPLAY       35
#define VCI_CANDTU_NET      36
#define VCI_CANDTU_100UR    37

// CAN error codes
#define ERR_CAN_OVERFLOW            0x0001  // CAN controller internal FIFO overflow
#define ERR_CAN_ERRALARM            0x0002  // CAN controller error warning
#define ERR_CAN_PASSIVE             0x0004  // CAN controller passive error
#define ERR_CAN_LOSE                0x0008  // CAN controller arbitration lost
#define ERR_CAN_BUSERR              0x0010  // CAN controller bus error
#define ERR_CAN_BUSOFF              0x0020  // Bus-off error
#define ERR_CAN_BUFFER_OVERFLOW     0x0040  // CAN controller internal buffer overflow

// General error codes
#define ERR_DEVICEOPENED            0x0100  // Device already opened
#define ERR_DEVICEOPEN              0x0200  // Device open error
#define ERR_DEVICENOTOPEN           0x0400  // Device not opened
#define ERR_BUFFEROVERFLOW          0x0800  // Buffer overflow
#define ERR_DEVICENOTEXIST          0x1000  // Device does not exist
#define ERR_LOADKERNELDLL           0x2000  // Failed to load driver DLL
#define ERR_CMDFAILED               0x4000  // Command execution failed
#define ERR_BUFFERCREATE            0x8000  // Insufficient memory

// CANET error codes
#define ERR_CANETE_PORTOPENED       0x00010000  // Port already opened
#define ERR_CANETE_INDEXUSED        0x00020000  // Device index already in use
#define ERR_REF_TYPE_ID             0x00030000  // RefType passed to SetReference/GetReference does not exist
#define ERR_CREATE_SOCKET           0x00030002  // Failed to create socket
#define ERR_OPEN_CONNECT            0x00030003  // Failed to open socket connection, device may already be connected
#define ERR_NO_STARTUP              0x00030004  // Device not started
#define ERR_NO_CONNECTED            0x00030005  // Device not connected
#define ERR_SEND_PARTIAL            0x00030006  // Only part of CAN frame sent
#define ERR_SEND_TOO_FAST           0x00030007  // Data sent too fast, socket buffer is full

// Function return status
#define STATUS_OK                   1
#define STATUS_ERR                  0

#define CMD_DESIP                   0
#define CMD_DESPORT                 1
#define CMD_CHGDESIPANDPORT         2
#define CMD_SRCPORT                 2
#define CMD_TCP_TYPE                4  // TCP mode: server=1 or client=0
#define TCP_CLIENT                  0
#define TCP_SERVER                  1
// Valid in server mode
#define CMD_CLIENT_COUNT            5  // Number of connected clients
#define CMD_CLIENT                  6  // Connected client info
#define CMD_DISCONN_CLINET          7  // Disconnect a client
#define CMD_SET_RECONNECT_TIME      8  // Enable auto-reconnect
// CANDTU_NET supports GPS
#define CMD_GET_GPS                 9
#define CMD_GET_GPS_NUM             10 // Number of GPS entries

typedef unsigned long       DWORD, ULONG;
typedef int                 INT;
typedef void*               HANDLE;
typedef unsigned char       BYTE;
typedef unsigned short      USHORT;
typedef char                CHAR;
typedef unsigned int        UINT;
typedef unsigned char       UCHAR;
typedef unsigned short      UINT16;
typedef void*               PVOID;

typedef struct tagRemoteClient{
    int     iIndex;
    DWORD   port;
    HANDLE  hClient;
    char    szip[32];
}REMOTE_CLIENT;

typedef struct _tagChgDesIPAndPort
{
    char    szpwd[10];
    char    szdesip[20];
    int     desport;
    BYTE    blistenonly;
}CHGDESIPANDPORT;

// 1. ZLGCAN series interface card information structure
typedef  struct  _VCI_BOARD_INFO{
    USHORT  hw_Version;
    USHORT  fw_Version;
    USHORT  dr_Version;
    USHORT  in_Version;
    USHORT  irq_Num;
    BYTE    can_Num;
    CHAR    str_Serial_Num[20];
    CHAR    str_hw_Type[40];
    USHORT  Reserved[4];
} VCI_BOARD_INFO,*PVCI_BOARD_INFO;

// 2. CAN frame structure
typedef  struct  _VCI_CAN_OBJ{
    UINT    ID;
    UINT    TimeStamp;
    BYTE    TimeFlag;
    BYTE    SendType;
    BYTE    RemoteFlag; // Indicates remote frame
    BYTE    ExternFlag; // Indicates extended frame
    BYTE    DataLen;
    BYTE    Data[8];
    BYTE    Reserved[3];    // Reserved[0] bit 0 indicates a special blank or highlight frame
}VCI_CAN_OBJ,*PVCI_CAN_OBJ;

// 3. CAN controller status structure
typedef struct _VCI_CAN_STATUS{
    UCHAR   ErrInterrupt;
    UCHAR   regMode;
    UCHAR   regStatus;
    UCHAR   regALCapture;
    UCHAR   regECCapture;
    UCHAR   regEWLimit;
    UCHAR   regRECounter;
    UCHAR   regTECounter;
    DWORD   Reserved;
}VCI_CAN_STATUS,*PVCI_CAN_STATUS;

// 4. Error information structure
typedef struct _VCI_ERR_INFO{
    UINT    ErrCode;
    BYTE    Passive_ErrData[3];
    BYTE    ArLost_ErrData;
} VCI_ERR_INFO,*PVCI_ERR_INFO;

// 5. CAN initialization configuration structure
typedef struct _VCI_INIT_CONFIG{
    DWORD    AccCode;
    DWORD    AccMask;
    DWORD    Reserved;
    UCHAR    Filter;
    UCHAR    Timing0;
    UCHAR    Timing1;
    UCHAR    Mode;
}VCI_INIT_CONFIG,*PVCI_INIT_CONFIG;

///////// new add struct for filter /////////
typedef struct _VCI_FILTER_RECORD{
    DWORD   ExtFrame;   // Whether this is an extended frame
    DWORD   Start;
    DWORD   End;
}VCI_FILTER_RECORD,*PVCI_FILTER_RECORD;

// Periodic auto-send structure
typedef struct _VCI_AUTO_SEND_OBJ{
    BYTE    Enable;     // Enable this message: 0=disable, 1=enable
    BYTE    Index;      // Message index, supports up to 32 messages
    DWORD   Interval;   // Transmission period in milliseconds
    VCI_CAN_OBJ obj;    // Message contents
}VCI_AUTO_SEND_OBJ,*PVCI_AUTO_SEND_OBJ;

// Indicator LED control structure
typedef struct _VCI_INDICATE_LIGHT{
    BYTE    Indicate;             // LED index
    BYTE    AttribRedMode:2;      // Red LED off/on/blink/auto
    BYTE    AttribGreenMode:2;    // Green LED off/on/blink/auto
    BYTE    AttribReserved:4;     // Reserved
    BYTE    FrequenceRed:2;       // Red LED blink frequency
    BYTE    FrequenceGreen:2;     // Green LED blink frequency
    BYTE    FrequenceReserved:4;  // Reserved
} VCI_INDICATE_LIGHT,*PVCI_INDICATE_LIGHT;

// Frame forwarding structure
typedef struct _VCI_CAN_OBJ_REDIRECT{
    BYTE    Action;               // Enable or disable forwarding
    BYTE    DestCanIndex;         // Destination CAN channel
} VCI_CAN_OBJ_REDIRECT,*PVCI_CAN_OBJ_REDIRECT;

typedef struct _CANDTUTIME {
    UINT16 wYear;
    UINT16 wMonth;
    UINT16 wDay;
    UINT16 wHour;
    UINT16 wMinute;
    UINT16 wSecond;
} CANDTUTIME;

// GPS data structure
typedef struct _tagCANDTUGPSData
{
    float       fLatitude;  // Latitude
    float       fLongitude; // Longitude
    float       fSpeed;     // Speed
    CANDTUTIME  candtuTime;
}CANDTUGPSData, *PCANDTUGPSData;

// GPS container structure
typedef struct _VCI_CANDTU_GPS_DATA
{
    PCANDTUGPSData pGPSData;    // Buffer address provided by user
    ULONG          nGPSDataCnt; // Number of GPS data entries the buffer can hold
}VCI_CANDTU_GPS_DATA, *PVCI_CANDTU_GPS_DATA;

#ifdef __cplusplus
#define EXTERNC extern "C" __declspec(dllexport)
#define DEF(a) = a
#else
#define EXTERNC __declspec(dllexport)
#define DEF(a)
#endif

EXTERNC DWORD __stdcall VCI_OpenDevice(DWORD DeviceType,DWORD DeviceInd,DWORD Reserved);
EXTERNC DWORD __stdcall VCI_CloseDevice(DWORD DeviceType,DWORD DeviceInd);
EXTERNC DWORD __stdcall VCI_InitCAN(DWORD DeviceType, DWORD DeviceInd, DWORD CANInd, PVCI_INIT_CONFIG pInitConfig);

EXTERNC DWORD __stdcall VCI_ReadBoardInfo(DWORD DeviceType,DWORD DeviceInd,PVCI_BOARD_INFO pInfo);
EXTERNC DWORD __stdcall VCI_ReadErrInfo(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd,PVCI_ERR_INFO pErrInfo);
EXTERNC DWORD __stdcall VCI_ReadCANStatus(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd,PVCI_CAN_STATUS pCANStatus);

EXTERNC DWORD __stdcall VCI_GetReference(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd,DWORD RefType,PVOID pData);
EXTERNC DWORD __stdcall VCI_SetReference(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd,DWORD RefType,PVOID pData);

EXTERNC ULONG __stdcall VCI_GetReceiveNum(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd);
EXTERNC DWORD __stdcall VCI_ClearBuffer(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd);

EXTERNC DWORD __stdcall VCI_StartCAN(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd);
EXTERNC DWORD __stdcall VCI_ResetCAN(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd);

EXTERNC ULONG __stdcall VCI_Transmit(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd,PVCI_CAN_OBJ pSend,ULONG Len);
EXTERNC ULONG __stdcall VCI_Receive(DWORD DeviceType,DWORD DeviceInd,DWORD CANInd,PVCI_CAN_OBJ pReceive,ULONG Len,INT WaitTime DEF(-1));

#endif
