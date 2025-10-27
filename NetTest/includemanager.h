#ifndef _INCLUDE_MANAGE_H_
#define _INCLUDE_MANAGE_H_


#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <cstdio>
#include <sstream>
#include <memory>
#include <thread>
#include <random>
#include <conio.h> // _kbhit(), _getch()
#include "json.hpp" // nlohmann::json
#include <mutex>
#include <atomic>
#include <chrono>
#include <optional>
#include <DirectXMath.h>
using namespace DirectX;


#include "RakNet/Source/RakPeerInterface.h"
#include "RakNet/Source/RakNetTypes.h"
#include "RakNet/Source/BitStream.h"
#include "RakNet/Source/MessageIdentifiers.h"
#include "RakNet/Source/RakSleep.h"
#include "RakNet/Source/RakString.h"
#include "RakNet/Source/NatTypeDetectionClient.h"


#pragma comment(lib, "RakNetDLL.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")


#define popen _popen
#define pclose _pclose





enum ConsoleColor {
    BLACK = 0,
    BLUE = 1,
    GREEN = 2,
    CYAN = 3,
    RED = 4,
    MAGENTA = 5,
    YELLOW = 6,
    WHITE = 7,
    GRAY = 8,
    LIGHT_BLUE = 9,
    LIGHT_GREEN = 10,
    LIGHT_CYAN = 11,
    LIGHT_RED = 12,
    LIGHT_MAGENTA = 13,
    LIGHT_YELLOW = 14,
    BRIGHT_WHITE = 15
};

inline void SetConsoleColor(ConsoleColor color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, static_cast<WORD>(color));
}



#endif