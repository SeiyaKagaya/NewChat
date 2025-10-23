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
    std::cout << "\n[�`���b�g�J�n] ���b�Z�[�W����͂��đ��M���Ă��������B���őޏo�o���܂��B\n";
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

        // ���ʂȏI�����b�Z�[�W�Ή�
        if (msg == "/quit") {
            SetConsoleColor(RED);
            std::cout << "\n[���肪�ޏo���܂���]\n";
            SetConsoleColor(WHITE);
            running_ = false;
            break;
        }

        std::cout << "\r[����] " << msg << "\n> " << std::flush;
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
            std::cout << "[�`���b�g�I��]\n";
            SetConsoleColor(WHITE);
            running_ = false;
            m_bEndChat = true;
            break;
        }



        sendto(sock_, input.c_str(), (int)input.size(), 0,
            (sockaddr*)&peerAddr_, sizeof(peerAddr_));
    }
}