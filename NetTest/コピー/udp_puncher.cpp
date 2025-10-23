
//------------------------------------------------------------
// @file        udp_puncher.cpp
//------------------------------------------------------------
#include "udp_puncher.h"
#include "chat_session.h"
#include <memory>
#include <vector>

UDPPuncher::UDPPuncher()
    : sock(INVALID_SOCKET), connected(false), running(false)
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

UDPPuncher::~UDPPuncher() {
    /*running = false;
    if (recvThread.joinable()) recvThread.join();

    if (sock != INVALID_SOCKET) closesocket(sock);
    
    WSACleanup();*/

    running = false;
    if (recvThread.joinable()) recvThread.join();
    if (sock != INVALID_SOCKET) closesocket(sock);
}

bool UDPPuncher::Start(const std::string& targetIp, unsigned short targetPort, bool isHost) {

    int timeout = 2000; // 2�b
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));


    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        SetConsoleColor(RED);
        std::cerr << "[UDP] �\�P�b�g�쐬���s" << std::endl;
        SetConsoleColor(WHITE);
        return false;
    }

    sockaddr_in localAddr{};
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(12345); // �Ҏ�|�[�g

    if (bind(sock, (sockaddr*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        SetConsoleColor(RED);
        std::cerr << "[UDP] bind���s" << std::endl;
        SetConsoleColor(WHITE);
        closesocket(sock);
        return false;
    }

    targetAddr.sin_family = AF_INET;
    inet_pton(AF_INET, targetIp.c_str(), &targetAddr.sin_addr);
    targetAddr.sin_port = htons(targetPort);






    running = true;
    recvThread = std::thread(&UDPPuncher::ReceiveLoop, this);

    SetConsoleColor(LIGHT_BLUE);
    std::cout << "[UDP] " << (isHost ? "�z�X�g" : "�N���C�A���g")
        << " ��: " << targetIp << ":" << targetPort << " �Ƀp���`�J�n..." << std::endl;
    SetConsoleColor(WHITE);

    const char* punchMsg = "PUNCH";
    const char* openMsg = "OPEN";

    // ���M���[�v�F�ߕ��׉���̂��� sleep ������ & �f�b�h���b�N���
    while (running && !connected) {
        // send a PUNCH packet
        sendto(sock, punchMsg, (int)strlen(punchMsg), 0,
            (sockaddr*)&targetAddr, sizeof(targetAddr));

        // �����҂i����� recv �ɕ��S�������Ȃ��j
        std::this_thread::sleep_for(std::chrono::milliseconds(120));

        // running �� false �ɂȂ����瑦������
        if (!running) {
            SetConsoleColor(RED);
            std::cout << "[UDP] ���f����܂���\n";
            SetConsoleColor(WHITE);
            break;
        }
    }



    if (connected) {

        sendto(sock, openMsg, (int)strlen(openMsg), 0,
            (sockaddr*)&targetAddr, sizeof(targetAddr));

        SetConsoleColor(LIGHT_BLUE);
        std::cout << "[UDP] �ʐM�J�ʁI" << std::endl;
        SetConsoleColor(WHITE);

        // -------------------------------------------------
        // ��ChatSession�N��
        // -------------------------------------------------
        sockaddr_in peerAddr{};
        peerAddr.sin_family = AF_INET;
        peerAddr.sin_port = htons(targetPort);
        inet_pton(AF_INET, targetIp.c_str(), &peerAddr.sin_addr);




        if (isHost) {
            // �z�X�g�͕����N���C�A���g�Ή�
            chatSessions.push_back(std::make_shared<ChatSession>(sock, peerAddr));
            chatSessions.back()->Start();
        }
        else {
            // �N���C�A���g��1��1
            clientChat = std::make_shared<ChatSession>(sock, peerAddr);
            clientChat->Start();
        }
    }
    else {
        SetConsoleColor(RED);
        std::cout << "[UDP] �^�C���A�E�g or ���f" << std::endl;
        SetConsoleColor(WHITE);
    }

    return connected;
}

void UDPPuncher::ReceiveLoop() {
    char buffer[512];
    sockaddr_in fromAddr{};
    int fromLen = sizeof(fromAddr);

    // �������g�̃��[�J���A�h���X�E�|�[�g�����擾���Ă���
    sockaddr_in selfAddr{};
    int selfAddrLen = sizeof(selfAddr);
    getsockname(sock, (sockaddr*)&selfAddr, &selfAddrLen);

    while (running) {
        int recvLen = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
            (sockaddr*)&fromAddr, &fromLen);

        if (recvLen <= 0) {
            // �^�C���A�E�g��m���u���b�L���O�Ȃ班���ҋ@
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        buffer[recvLen] = '\0';
        std::string msg(buffer);

        // IP�ƃ|�[�g�𕶎��񉻂��ă��O�o�͂ɂ��g��
        char fromIpStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &fromAddr.sin_addr, fromIpStr, sizeof(fromIpStr));
        unsigned short fromPort = ntohs(fromAddr.sin_port);

        // --- �����R���p�P�b�g�̏��O ---
        // ���M��IP�ƃ|�[�g���������g�ƈ�v����Ȃ疳��
        if (fromAddr.sin_addr.s_addr == selfAddr.sin_addr.s_addr &&
            fromAddr.sin_port == selfAddr.sin_port) {
            continue; // �����̃G�R�[
        }

        // --- �z��O�i�^�[�Q�b�g�O�j�p�P�b�g�̏��O ---
        bool fromIsTarget =
            (fromAddr.sin_addr.s_addr == targetAddr.sin_addr.s_addr) &&
            (fromAddr.sin_port == targetAddr.sin_port);

        if (!fromIsTarget) {
            // ���m�[�h��STUN�����̕��ꍞ�݂�h��
            SetConsoleColor(GRAY);
            std::cout << "[UDP:IGNORED] " << fromIpStr << ":" << fromPort
                << " ����̖��m�p�P�b�g \"" << msg << "\" �𖳎�" << std::endl;
            SetConsoleColor(WHITE);
            continue;
        }

        // --- ����������ۂ̃��b�Z�[�W���� ---
        if (msg == "PUNCH") {
            connected = true;

            // �����Ƃ��� OPEN ��Ԃ��i�񓯊��n���h�V�F�C�N�j
            const char* openMsg = "OPEN";
            sendto(sock, openMsg, (int)strlen(openMsg), 0,
                (sockaddr*)&fromAddr, fromLen);

            SetConsoleColor(LIGHT_BLUE);
            std::cout << "[UDP] ����(" << fromIpStr << ":" << fromPort
                << ") ���� PUNCH ����M �� OPEN�ԓ����ڑ�����" << std::endl;
            SetConsoleColor(WHITE);
        }
        else if (msg == "OPEN") {
            connected = true;
            SetConsoleColor(LIGHT_BLUE);
            std::cout << "[UDP] ����(" << fromIpStr << ":" << fromPort
                << ") ���� OPEN ����M �� �ڑ��m��" << std::endl;
            SetConsoleColor(WHITE);
        }
        else {
            // �ʏ탁�b�Z�[�W�i�`���b�g���j
            SetConsoleColor(CYAN);
            std::cout << "[���� " << fromIpStr << ":" << fromPort << "] " << msg << std::endl;
            SetConsoleColor(WHITE);
        }
    }

    SetConsoleColor(GRAY);
    std::cout << "[UDP] ReceiveLoop �I��" << std::endl;
    SetConsoleColor(WHITE);
}


bool UDPPuncher::SendMessage(const std::string& msg) {
    if (!connected) return false;
    sendto(sock, msg.c_str(), (int)msg.size(), 0,
        (sockaddr*)&targetAddr, sizeof(targetAddr));
    return true;
}

void UDPPuncher::Stop() {
    running = false;
    if (recvThread.joinable()) recvThread.join();
    if (sock != INVALID_SOCKET) closesocket(sock);
}