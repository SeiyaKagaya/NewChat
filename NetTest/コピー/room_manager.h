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
    ~RoomManager();

    // 部屋作成
    bool CreateRoom(const std::string& roomName,
        std::string& outHostIp,
        const std::string& ipMode,
        int maxPlayers,
        unsigned short natPort,
        const std::string& localIp,
        const std::string& hostName);


    // 部屋一覧取得
    bool GetRoomList(std::map<std::string, nlohmann::json>& outRooms);

    bool HttpGet(const std::string& url, std::string& outResponse);

    // Shift-JIS → UTF-8
    std::string CP932ToUTF8(const std::string& sjis);

    // URLエンコード
    std::string UrlEncode(const std::string& value);

    // UTF-8 → UTF-16
    std::wstring UTF8ToWString(const std::string& utf8);

 
    //サーバーへのリレー送信(クライアント発)
     bool SendRelayInfo(
        const std::string& roomName,
        const std::string& userName,
        const std::string& externalIp,
        unsigned short externalPort,
        const std::string& localIp,
        unsigned short localPort,
        bool sameLan);

    //サーバーからのリレー受信(ホスト受)
    void StartRelayListener(
        const std::string& roomName,
        std::function<void(const nlohmann::json&)> onClientJoin);



private:
    std::string serverUrl; // サーバーのURL格納
};

#endif
