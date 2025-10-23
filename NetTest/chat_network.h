//------------------------------------------------------------
// @file        chat_network.h
// @brief       �`���b�g����i�X�V�ρF�p���`�����{TCP�����񍐁{LAN����Ή��{�����Ď��X���b�h�ǉ��j
//------------------------------------------------------------
#ifndef _CHAT_NETWORK_H_
#define _CHAT_NETWORK_H_

#pragma once

#include "RakNet/Source/RakPeerInterface.h"
#include "RakNet/Source/RakNetTypes.h"
#include "RakNet/Source/BitStream.h"
#include "RakNet/Source/MessageIdentifiers.h"
#include "RakNet/Source/RakSleep.h"
#include "RakNet/Source/RakString.h"
#include "RakNet/Source/NatTypeDetectionClient.h"

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <windows.h>
#include <map>
#include <chrono>
#include <optional>
#include "room_manager.h"


#pragma comment(lib, "RakNetDLL.lib")

// �V�����p�P�b�gID��ǉ�
enum GAME_MESSAGES
{
    ID_GAME_MESSAGE = ID_USER_PACKET_ENUM + 1,
    ID_CLIENT_PROTOCOL,   // �N���C�A���g�v���g�R���ʒm�p
    ID_PUNCH_INFO,        // �N���C�A���g -> �z�X�g : JSON �ɂ��O��IP/port�ʒm
    ID_PUNCH_PACKET,      // �p���`�p�p�P�b�g�i�ʒm�^�p���`�j
    ID_LEAVE_NOTIFICATION,// �N���C�A���g/�z�X�g�ޏo�ʒm
    ID_HEARTBEAT          // �����m�F�p
};

struct ClientInfo {
    RakNet::SystemAddress address;
    std::string protocol;       // "OK4" / "OK6"
    std::string externalIp;
    unsigned short externalPort = 0;
    std::string localIp;        // �V�K: LAN���A�h���X
    unsigned short localPort = 0; // �V�K: LAN���|�[�g
    bool isSameLAN = false;     // �V�K: ����LAN����
    std::string userName;       // ���ǉ� (�o�C�i��/CP932 �ł�������)
    std::chrono::steady_clock::time_point connectedTime; // �� �ǉ�
};



class ChatNetwork
{
public:
    ChatNetwork();
    ~ChatNetwork();

    // ������
    bool Init(bool host, unsigned short port, const std::string& bindIp, const std::string& protocol, RoomManager& roomManager, const std::string& youExternalIp);

    // �z�X�g�֐ڑ�
    bool ConnectToHost(const std::string& hostIp, const std::string& hostProtocol, unsigned short hostPort);

    // ���b�Z�[�W���M
    void SendMessage(const std::string& message);

    // ��M���[�v
    void ReceiveLoop();

    // ���g�̃A�h���X�擾
    const RakNet::SystemAddress& GetMyAddress() const;

    // �I������
    void Stop();

    // �N���C�A���g�� STUN �œ����O���A�h���X���`���b�g�ɓn���i�L���[�i�[�j
    void SetPendingPunch(const std::string& extIp, unsigned short extPort,
        const std::string& localIp, unsigned short localPort,
        bool sameLAN, const std::string& userName);

    // �p���`���[�v����
    void StartPunchLoop(const std::string& targetIp, unsigned short targetPort, bool isHostSide);
    void StopPunchLoop();

    // TCP �������M
    void SendPunchDoneTCP(const std::string& targetIp, unsigned short port);

    // ���O�i�[
    void SetUserName(const std::string& name);

    void SetConsoleColor(WORD color);
    void ResetConsoleColor();

    std::string ToBase64(const std::string& input);
    std::string FromBase64(const std::string& input);

    void SendLeaveNotification();        // �N���C�A���g�p
    void BroadcastLeaveNotification();   // �z�X�g�p
    bool IsHost() const { return m_isHost; }

    void StartHeartbeat();
    void StopHeartbeat();

    RakNet::SystemAddress GetMyHostAddress() const;
    std::chrono::steady_clock::time_point GetLastHeartbeat(RakNet::SystemAddress addr);
    void CheckClientTimeouts();

    std::optional<std::chrono::steady_clock::time_point> GetLastHeartbeatOpt(RakNet::SystemAddress addr);



    void StartRelayPollThread(RoomManager& roomManager, const std::string& hostExternalIp);

    //void ChatNetwork::StartHostToClientPunch(const PendingClientInfo& info);



    //void StartClientRelayPoll(RoomManager& roomManager, const std::string& roomName,bool isSameLan, const std::string& hostExternalIp);


    // ============================================================
    // ������ �V�K�ǉ��F�����Ď��X���b�h�@�\�i�z�X�g�^�N���C�A���g�j
    // ============================================================
    void StartClientMonitor();  // �z�X�g���N���C�A���g�����m�F
    void StartHostMonitor();    // �N���C�A���g���z�X�g�����m�F

    bool GetForceExit() { return m_forceExit; }

    // ChatNetwork.h �ɒǉ�
    //void StartJoinRequestPoll(RoomManager& roomManager, const std::string& roomName);





private:
    RakNet::RakPeerInterface* m_peer;
    bool m_isHost;
    unsigned short m_port;
    bool m_canSend;

    std::string m_clientProtocol;
    std::string m_hostProtocol;

    std::mutex m_clientsMutex;
    std::vector<ClientInfo> m_clients;
    std::atomic<bool> m_running{ true };

    // �p���`�Ǘ�
    std::string m_pendingPunchIp;
    unsigned short m_pendingPunchPort = 0;
    std::string m_pendingLocalIp;
    unsigned short m_pendingLocalPort = 0;
    bool m_pendingSameLAN = false;
    std::atomic<bool> m_hasPendingPunch{ false };
    std::mutex m_pendingMutex;
    std::string m_pendingUserName; // ���ǉ� (raw bytes as provided)

    std::string m_userName; // �����̃��[�U�[���i�N���C�A���g�p�j

    // �ڑ���z�X�g���i�N���C�A���g���Ɏg�p�j
    std::string m_hostIp;
    unsigned short m_hostPort = 0;

    // �p���`���[�v���s�p
    std::atomic<bool> m_punchLoopActive{ false };
    std::thread m_punchThread;

    // TCP �����Ҏ�p
    std::atomic<bool> m_tcpWaiterActive{ false };
    std::thread m_tcpWaiterThread;

    // m_canSend �̕ی�
    std::mutex m_canSendMutex;

    // �n�[�g�r�[�g�Ǘ�
    std::atomic<bool> m_heartbeatActive{ false };
    std::thread m_heartbeatThread;
    std::mutex m_heartbeatMutex;
    std::map<RakNet::SystemAddress, std::chrono::steady_clock::time_point> m_lastHeartbeat; // �z�X�g�p
    std::chrono::seconds m_heartbeatTimeout{ 10 };


    // ============================================================
    // ������ �V�K�ǉ��F�����Ď��X���b�h�֘A�����o
    // ============================================================
    std::atomic<bool> m_clientMonitorActive{ false };  // �z�X�g���Ď��L��
    std::thread m_clientMonitorThread;                 // �z�X�g���N���C�A���g�Ď��X���b�h

    std::atomic<bool> m_hostMonitorActive{ false };    // �N���C�A���g���Ď��L��
    std::thread m_hostMonitorThread;                   // �N���C�A���g���z�X�g�Ď��X���b�h

    std::thread m_receiveThread;

    std::atomic<bool> m_forceExit{ false }; // �N���C�A���g�����I���t���O


    // ChatNetwork �N���X�̃����o�Ƃ��Ēǉ�
    std::thread m_clientRelayPollThread;

    bool running = false;  // �X���b�h����p�t���O
};

#endif
