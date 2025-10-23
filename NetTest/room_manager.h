//------------------------------------------------------------
// @file        room_manager.h
// @brief       部屋周り
//------------------------------------------------------------
#ifndef _ROOM_MANAGER_H_
#define _ROOM_MANAGER_H_

#include "includemanager.h"



// 追加
struct PendingClientInfo {
    std::string external_ip;
    int external_port;
    std::string local_ip;
    int local_port;
    std::string client_name;
};

class RoomManager {
public:
    //----------------------------------------------
    // コンストラクタ
    // serverUrl: 部屋情報を管理するサーバーのURL
    //----------------------------------------------
    RoomManager(const std::string& serverUrl);

    // 部屋作成
    bool CreateRoom(const std::string& roomName,
        std::string& outHostIp,
        const std::string& ipMode,
        int maxPlayers,
        unsigned short natPort,
        const std::string& localIp,
        const std::string& hostName);

    //// 部屋への入室通知（リレー用）
    //bool JoinRoom(const std::string& roomName,
    //    const std::string& userName,
    //    const std::string& extIp,
    //    unsigned short extPort,
    //    const std::string& localIp,
    //    unsigned short localPort);

    // 部屋一覧取得
    bool GetRoomList(std::map<std::string, nlohmann::json>& outRooms);

    bool HttpGet(const std::string& url, std::string& outResponse);

    // Shift-JIS → UTF-8
    std::string CP932ToUTF8(const std::string& sjis);

    // URLエンコード
    std::string UrlEncode(const std::string& value);

    // UTF-8 → UTF-16
    std::wstring UTF8ToWString(const std::string& utf8);

 
    std::optional<PendingClientInfo> GetPendingClientInfo(const std::string& hostExternalIp);

    bool RelayClientInfo(const std::string& hostExternalIp,
        const std::string& externalIp, unsigned short externalPort,
        const std::string& localIp, unsigned short localPort,
        const std::string& clientName);

private:
    std::string serverUrl; // サーバーのURL格納
};

#endif
