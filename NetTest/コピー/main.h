//------------------------------------------------------------
// @file        main.h
// @brief       
//------------------------------------------------------------
#ifndef _MAIN_H_
#define _MAIN_H_

#include "includemanager.h"
#include "room_manager.h"
#include "udp_puncher.h"

// ä÷êîêÈåæ
bool CheckServerIP();
//bool HostFlow(RoomManager& roomManager, std::string& hostIp, unsigned short natPort, const std::string& userName,const std::string&youExternalIp);
bool HostFlow(RoomManager& roomManager, std::string& hostIp, unsigned short natPort,
    const std::string& userName, const std::string& youExternalIp,
    std::vector<std::shared_ptr<UDPPuncher>>& activePunchers,
    std::vector<std::shared_ptr<ChatSession>>& activeChats);

bool ClientFlow(RoomManager& roomManager, std::string& hostIp,
    const std::string& userName, const std::string& youExternalIp,
    std::vector<std::shared_ptr<UDPPuncher>>& activePunchers,
    std::vector<std::shared_ptr<ChatSession>>& activeChats);


//bool ClientFlow( RoomManager& roomManager, std::string& hostIp, const std::string& userName, const std::string& youExternalIp);
//void ChatLoop(ChatNetwork& chatNetwork);

std::string UTF8ToCP932(const std::string& utf8);

std::string GetLocalIPAddress();
bool IsSameLAN(const std::string& ip1, const std::string& ip2);

void ResetAll(std::vector<std::shared_ptr<UDPPuncher>>& punchers,
    std::vector<std::shared_ptr<ChatSession>>& chats);





#endif