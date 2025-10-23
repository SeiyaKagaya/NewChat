
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

    int timeout = 2000; // 2秒
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));


    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        SetConsoleColor(RED);
        std::cerr << "[UDP] ソケット作成失敗" << std::endl;
        SetConsoleColor(WHITE);
        return false;
    }

    sockaddr_in localAddr{};
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(12345); // 待受ポート

    if (bind(sock, (sockaddr*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        SetConsoleColor(RED);
        std::cerr << "[UDP] bind失敗" << std::endl;
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
    std::cout << "[UDP] " << (isHost ? "ホスト" : "クライアント")
        << " 側: " << targetIp << ":" << targetPort << " にパンチ開始..." << std::endl;
    SetConsoleColor(WHITE);

    const char* punchMsg = "PUNCH";
    const char* openMsg = "OPEN";

    // 送信ループ：過負荷回避のため sleep を入れる & デッドロック回避
    while (running && !connected) {
        // send a PUNCH packet
        sendto(sock, punchMsg, (int)strlen(punchMsg), 0,
            (sockaddr*)&targetAddr, sizeof(targetAddr));

        // 少し待つ（相手の recv に負担をかけない）
        std::this_thread::sleep_for(std::chrono::milliseconds(120));

        // running が false になったら即抜ける
        if (!running) {
            SetConsoleColor(RED);
            std::cout << "[UDP] 中断されました\n";
            SetConsoleColor(WHITE);
            break;
        }
    }



    if (connected) {

        sendto(sock, openMsg, (int)strlen(openMsg), 0,
            (sockaddr*)&targetAddr, sizeof(targetAddr));

        SetConsoleColor(LIGHT_BLUE);
        std::cout << "[UDP] 通信開通！" << std::endl;
        SetConsoleColor(WHITE);

        // -------------------------------------------------
        // ★ChatSession起動
        // -------------------------------------------------
        sockaddr_in peerAddr{};
        peerAddr.sin_family = AF_INET;
        peerAddr.sin_port = htons(targetPort);
        inet_pton(AF_INET, targetIp.c_str(), &peerAddr.sin_addr);




        if (isHost) {
            // ホストは複数クライアント対応
            chatSessions.push_back(std::make_shared<ChatSession>(sock, peerAddr));
            chatSessions.back()->Start();
        }
        else {
            // クライアントは1対1
            clientChat = std::make_shared<ChatSession>(sock, peerAddr);
            clientChat->Start();
        }
    }
    else {
        SetConsoleColor(RED);
        std::cout << "[UDP] タイムアウト or 中断" << std::endl;
        SetConsoleColor(WHITE);
    }

    return connected;
}

void UDPPuncher::ReceiveLoop() {
    char buffer[512];
    sockaddr_in fromAddr{};
    int fromLen = sizeof(fromAddr);

    // 自分自身のローカルアドレス・ポート情報を取得しておく
    sockaddr_in selfAddr{};
    int selfAddrLen = sizeof(selfAddr);
    getsockname(sock, (sockaddr*)&selfAddr, &selfAddrLen);

    while (running) {
        int recvLen = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
            (sockaddr*)&fromAddr, &fromLen);

        if (recvLen <= 0) {
            // タイムアウトやノンブロッキングなら少し待機
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        buffer[recvLen] = '\0';
        std::string msg(buffer);

        // IPとポートを文字列化してログ出力にも使う
        char fromIpStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &fromAddr.sin_addr, fromIpStr, sizeof(fromIpStr));
        unsigned short fromPort = ntohs(fromAddr.sin_port);

        // --- 自分由来パケットの除外 ---
        // 送信元IPとポートが自分自身と一致するなら無視
        if (fromAddr.sin_addr.s_addr == selfAddr.sin_addr.s_addr &&
            fromAddr.sin_port == selfAddr.sin_port) {
            continue; // 自分のエコー
        }

        // --- 想定外（ターゲット外）パケットの除外 ---
        bool fromIsTarget =
            (fromAddr.sin_addr.s_addr == targetAddr.sin_addr.s_addr) &&
            (fromAddr.sin_port == targetAddr.sin_port);

        if (!fromIsTarget) {
            // 他ノードやSTUN応答の紛れ込みを防ぐ
            SetConsoleColor(GRAY);
            std::cout << "[UDP:IGNORED] " << fromIpStr << ":" << fromPort
                << " からの未知パケット \"" << msg << "\" を無視" << std::endl;
            SetConsoleColor(WHITE);
            continue;
        }

        // --- ここから実際のメッセージ処理 ---
        if (msg == "PUNCH") {
            connected = true;

            // 応答として OPEN を返す（非同期ハンドシェイク）
            const char* openMsg = "OPEN";
            sendto(sock, openMsg, (int)strlen(openMsg), 0,
                (sockaddr*)&fromAddr, fromLen);

            SetConsoleColor(LIGHT_BLUE);
            std::cout << "[UDP] 相手(" << fromIpStr << ":" << fromPort
                << ") から PUNCH を受信 → OPEN返答＆接続成立" << std::endl;
            SetConsoleColor(WHITE);
        }
        else if (msg == "OPEN") {
            connected = true;
            SetConsoleColor(LIGHT_BLUE);
            std::cout << "[UDP] 相手(" << fromIpStr << ":" << fromPort
                << ") から OPEN を受信 → 接続確立" << std::endl;
            SetConsoleColor(WHITE);
        }
        else {
            // 通常メッセージ（チャット等）
            SetConsoleColor(CYAN);
            std::cout << "[相手 " << fromIpStr << ":" << fromPort << "] " << msg << std::endl;
            SetConsoleColor(WHITE);
        }
    }

    SetConsoleColor(GRAY);
    std::cout << "[UDP] ReceiveLoop 終了" << std::endl;
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