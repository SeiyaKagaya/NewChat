//------------------------------------------------------------
// @file        room_manager.h
// @brief       ��������
//------------------------------------------------------------
#ifndef _ROOM_MANAGER_H_
#define _ROOM_MANAGER_H_

#include "includemanager.h"


// �ǉ�
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
    // �R���X�g���N�^
    // serverUrl: ���������Ǘ�����T�[�o�[��URL
    //----------------------------------------------
    RoomManager(const std::string& serverUrl);
    ~RoomManager();

    // �����쐬
    bool CreateRoom(const std::string& roomName,
        std::string& outHostIp,
        const std::string& ipMode,
        int maxPlayers,
        unsigned short natPort,
        const std::string& localIp,
        const std::string& hostName);


    // �����ꗗ�擾
    bool GetRoomList(std::map<std::string, nlohmann::json>& outRooms);

    bool HttpGet(const std::string& url, std::string& outResponse);

    // Shift-JIS �� UTF-8
    std::string CP932ToUTF8(const std::string& sjis);

    // URL�G���R�[�h
    std::string UrlEncode(const std::string& value);

    // UTF-8 �� UTF-16
    std::wstring UTF8ToWString(const std::string& utf8);

 
    //�T�[�o�[�ւ̃����[���M(�N���C�A���g��)
     bool SendRelayInfo(
        const std::string& roomName,
        const std::string& userName,
        const std::string& externalIp,
        unsigned short externalPort,
        const std::string& localIp,
        unsigned short localPort,
        bool sameLan);

    //�T�[�o�[����̃����[��M(�z�X�g��)
    void StartRelayListener(
        const std::string& roomName,
        std::function<void(const nlohmann::json&)> onClientJoin);



private:
    std::string serverUrl; // �T�[�o�[��URL�i�[
};

#endif
