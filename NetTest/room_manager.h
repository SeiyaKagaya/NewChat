//------------------------------------------------------------
// @file        room_manager.h
// @brief       ��������
//------------------------------------------------------------
#ifndef _ROOM_MANAGER_H_
#define _ROOM_MANAGER_H_

#include "includemanager.h"
#include "nat_checker.h"



// �ǉ�
struct PendingClientInfo {
    std::string external_ip;
    int external_port;
    std::string local_ip;
    int local_port;
    std::string client_name;
    ConnectionMode connection_mode; // �ǉ�
};

class RoomManager {
public:
    //----------------------------------------------
    // �R���X�g���N�^
    // serverUrl: ���������Ǘ�����T�[�o�[��URL
    //----------------------------------------------
    RoomManager(const std::string& serverUrl);

    // �����쐬
    bool CreateRoom(const std::string& roomName,
        std::string& outHostIp,
        const std::string& ipMode,
        int maxPlayers,
        unsigned short natPort,
        const std::string& localIp,
        const std::string& hostName,
        ConnectionMode mode);

    //// �����ւ̓����ʒm�i�����[�p�j
    //bool JoinRoom(const std::string& roomName,
    //    const std::string& userName,
    //    const std::string& extIp,
    //    unsigned short extPort,
    //    const std::string& localIp,
    //    unsigned short localPort);

    // �����ꗗ�擾
    bool GetRoomList(std::map<std::string, nlohmann::json>& outRooms);

    bool static HttpGet(const std::string& url, std::string& outResponse);

    // Shift-JIS �� UTF-8
    std::string static CP932ToUTF8(const std::string& sjis);

    // URL�G���R�[�h
    std::string static UrlEncode(const std::string& value);

    // UTF-8 �� UTF-16
    std::wstring static UTF8ToWString(const std::string& utf8);

 
    std::optional<PendingClientInfo> GetPendingClientInfo(const std::string& hostExternalIp);

    //bool RelayClientInfo(const std::string& hostExternalIp,
    //    const std::string& externalIp, unsigned short externalPort,
    //    const std::string& localIp, unsigned short localPort,
    //    const std::string& clientName);


    bool RelayClientInfo(const std::string& hostExIP, const std::string& userName, const std::string& externalIp, unsigned short externalPort, const std::string& localIp, unsigned short localPort, bool sameLan);


    bool RelaySendData(const std::string& hostIp,
        const std::string& fromName,
        const std::string& payloadType,
        const std::string& payload);

private:
    std::string serverUrl; // �T�[�o�[��URL�i�[
};

#endif
