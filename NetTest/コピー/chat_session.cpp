//------------------------------------------------------------
// @file        chat_session.cpp
// @brief       
//------------------------------------------------------------
#include "chat_session.h"

ChatSession::ChatSession(SOCKET sock, sockaddr_in peerAddr)
    : sock_(sock), peerAddr_(peerAddr), running_(false)
{
}

ChatSession::~ChatSession() {
    Stop();
}

void ChatSession::Start() {
    running_ = true;

    recvThread_ = std::thread(&ChatSession::ReceiveLoop, this);
    sendThread_ = std::thread(&ChatSession::SendLoop, this);

    recvThread_.detach();
    sendThread_.detach();

    SetConsoleColor(WHITE);
    std::cout << "\n[チャット開始] メッセージを入力して送信してください。ｘで退出出来ます。\n";
    std::cout << "-------------------------------------------\n";
    SetConsoleColor(WHITE);
}

void ChatSession::Stop() {
    if (!running_) return;
    running_ = false;
    closesocket(sock_);
 
}

void ChatSession::ReceiveLoop() {
    char buffer[1024];
    while (running_) {
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);
        int recvLen = recvfrom(sock_, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&fromAddr, &fromLen);
        if (recvLen <= 0) continue;

        buffer[recvLen] = '\0';
        std::string msg(buffer);

        // 特別な終了メッセージ対応
        if (msg == "/quit") {
            SetConsoleColor(RED);
            std::cout << "\n[相手が退出しました]\n";
            SetConsoleColor(WHITE);
            running_ = false;
            break;
        }

        std::cout << "\r[相手] " << msg << "\n> " << std::flush;
    }
}

void ChatSession::SendLoop() {
    std::string input;
    while (running_) {
        std::cout << "> ";
        std::getline(std::cin, input);

        if (!running_) break;

        if (input == "/quit" || input == "x" || input == "X") {
            sendto(sock_, "/quit", 5, 0, (sockaddr*)&peerAddr_, sizeof(peerAddr_));
            SetConsoleColor(RED);
            std::cout << "[チャット終了]\n";
            SetConsoleColor(WHITE);
            running_ = false;
            m_bEndChat = true;
            break;
        }



        sendto(sock_, input.c_str(), (int)input.size(), 0,
            (sockaddr*)&peerAddr_, sizeof(peerAddr_));
    }
}