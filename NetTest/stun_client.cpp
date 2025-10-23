//------------------------------------------------------------
// @file        stun_client.cpp
// @brief       
//------------------------------------------------------------
#include "stun_client.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>



bool GetExternalAddress(std::string& outIP, unsigned short& outPort)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    // --- タイムアウト設定（3秒） ---
    int timeout = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    // --- STUNサーバーのDNS解決 ---
    sockaddr_in stunServer{};
    addrinfo hints{}, * resinfo = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo("stun.l.google.com", "19302", &hints, &resinfo) != 0) {
        closesocket(sock);
        WSACleanup();
        return false;
    }
    memcpy(&stunServer, resinfo->ai_addr, resinfo->ai_addrlen);
    freeaddrinfo(resinfo);

    // --- STUN Binding Request (RFC5389) ---
    unsigned char req[20] = {
        0x00, 0x01, // Binding Request
        0x00, 0x00, // Message Length
        0x21, 0x12, 0xA4, 0x42, // Magic Cookie
        0x63, 0x29, 0x00, 0x00, 0x12, 0x12, 0x00, 0x01, 0x53, 0x66 // Transaction ID
    };

    int sendBytes = sendto(sock, (const char*)req, sizeof(req), 0, (sockaddr*)&stunServer, sizeof(stunServer));
    if (sendBytes <= 0) {
        closesocket(sock);
        WSACleanup();
        return false;
    }

    unsigned char res[1024];
    sockaddr_in from{};
    int fromlen = sizeof(from);
    int len = recvfrom(sock, (char*)res, sizeof(res), 0, (sockaddr*)&from, &fromlen);

    if (len <= 0) {
        std::cout << "[STUN] 応答なし（タイムアウト）" << std::endl;
        closesocket(sock);
        WSACleanup();
        return false;
    }

    // --- 応答解析 ---
    for (int i = 0; i < len - 4; ++i) {
        if (res[i] == 0x00 && res[i + 1] == 0x20) { // XOR-MAPPED-ADDRESS attribute
            unsigned short port = (res[i + 6] ^ 0x21) << 8 | (res[i + 7] ^ 0x12);
            char ipStr[64];
            sprintf(ipStr, "%d.%d.%d.%d",
                res[i + 8] ^ 0x21,
                res[i + 9] ^ 0x12,
                res[i + 10] ^ 0xA4,
                res[i + 11] ^ 0x42);
            outIP = ipStr;
            outPort = port;
            closesocket(sock);
            WSACleanup();
            return true;
        }
    }

    closesocket(sock);
    WSACleanup();
    return false;
}