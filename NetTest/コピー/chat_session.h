//------------------------------------------------------------
// @file        chat_session.h
// @brief       
//------------------------------------------------------------
#ifndef _CHAT_SESSSION_H_
#define _CHAT_SESSSION_H_

#include "includemanager.h"

class ChatSession {
public:
    ChatSession(SOCKET sock, sockaddr_in peerAddr);
    ~ChatSession();

    void Start();
    void Stop();

    bool GetChatState() {return m_bEndChat;}

private:
    void ReceiveLoop();
    void SendLoop();

    SOCKET sock_;
    sockaddr_in peerAddr_;
    std::atomic<bool> running_;
    std::thread recvThread_;
    std::thread sendThread_;
    bool m_bEndChat = false;
};

#endif