//------------------------------------------------------------
// @file        udp_puncher.h
// @brief       
//------------------------------------------------------------
#ifndef _UDP_PUNCHER_H_
#define _UDP_PUNCHER_H_

#include "includemanager.h"



// UDP�z�[���p���`���O�Ǘ��N���X
class UDPPuncher {
public:
    UDPPuncher();
    ~UDPPuncher();

    // ���z�X�g/�N���C�A���g���ʂŌĂ�
    // ���[�J��or�O���ǂ�����g�����͈����œn��
    bool Start(const std::string& targetIp, unsigned short targetPort, bool isHost);

    // ����Ƀ��b�Z�[�W�𑗂�i�`���b�g�J�n��Ɏg����j
    bool SendMessage(const std::string& msg);

    bool IsConnected() const { return connected.load(); }
    SOCKET GetSocket() const { return sock; }
    sockaddr_in GetPeerAddr() const { return targetAddr; }

    void Stop();


private:
    SOCKET sock;
    sockaddr_in targetAddr;
    std::atomic<bool> connected;
    std::atomic<bool> running;
    std::thread recvThread;

    void ReceiveLoop();
// ------------------------------------------
// ��ChatSession�p
// ------------------------------------------
    std::vector<std::shared_ptr<ChatSession>> chatSessions; // �z�X�g�������N���C�A���g�Ή�
    std::shared_ptr<ChatSession> clientChat;               // �N���C�A���g��1��1
};


#endif