//------------------------------------------------------------
// @file        udp_puncher.h
// @brief       
//------------------------------------------------------------
#ifndef _UDP_PUNCHER_H_
#define _UDP_PUNCHER_H_

#include "includemanager.h"



// UDPホールパンチング管理クラス
class UDPPuncher {
public:
    UDPPuncher();
    ~UDPPuncher();

    // ★ホスト/クライアント共通で呼ぶ
    // ローカルor外部どちらを使うかは引数で渡す
    bool Start(const std::string& targetIp, unsigned short targetPort, bool isHost);

    // 相手にメッセージを送る（チャット開始後に使える）
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
// ★ChatSession用
// ------------------------------------------
    std::vector<std::shared_ptr<ChatSession>> chatSessions; // ホスト側複数クライアント対応
    std::shared_ptr<ChatSession> clientChat;               // クライアント側1対1
};


#endif