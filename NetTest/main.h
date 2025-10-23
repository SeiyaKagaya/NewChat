//------------------------------------------------------------
// @file        main.h
// @brief       
//------------------------------------------------------------
#ifndef _MAIN_H_
#define _MAIN_H_
#include "chat_network.h"


// ä÷êîêÈåæ
bool CheckServerIP();
bool HostFlow(ChatNetwork& chatNetwork, RoomManager& roomManager, std::string& hostIp, unsigned short natPort, const std::string& userName,const std::string&youExternalIp);
bool ClientFlow(ChatNetwork& chatNetwork, RoomManager& roomManager, std::string& hostIp, const std::string& userName, const std::string& youExternalIp);
void ChatLoop(ChatNetwork& chatNetwork);
std::string UTF8ToCP932(const std::string& utf8);
std::string GetLocalIPAddress();
bool IsSameLAN(const std::string& ip1, const std::string& ip2);



#endif