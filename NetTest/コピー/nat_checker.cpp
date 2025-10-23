//------------------------------------------------------------
// @file        nat_checker.cpp
// @brief       
//------------------------------------------------------------
#include"nat_checker.h"


NATChecker::NATChecker() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
}

NATChecker::~NATChecker() {
    WSACleanup();
}

void NATChecker::generateTransactionID(unsigned char* id) {
    std::random_device rd;
    for (int i = 0; i < 12; i++)
        id[i] = rd() % 256;
}

std::vector<unsigned char> NATChecker::createStunRequest() {
    std::vector<unsigned char> buf(20, 0);
    buf[0] = 0x00; buf[1] = 0x01; // Binding Request
    unsigned char tid[12];
    generateTransactionID(tid);
    for (int i = 0; i < 12; i++) buf[8 + i] = tid[i];
    return buf;
}

ExternalAddress NATChecker::parseStunResponse(char* response, int len) {
    ExternalAddress addr{};
    if (len < 20) return addr;

    const unsigned char* data = reinterpret_cast<unsigned char*>(response);
    const uint32_t magic_cookie = 0x2112A442;
    const unsigned char* transaction_id = data + 8; // 12バイト

    int pos = 20;
    while (pos + 4 <= len) {
        uint16_t attrType = (data[pos] << 8) | data[pos + 1];
        uint16_t attrLen = (data[pos + 2] << 8) | data[pos + 3];
        pos += 4;

        if (pos + attrLen > len) break;

        if ((attrType == 0x0001 || attrType == 0x0020) && attrLen >= 8) {
            unsigned char family = data[pos + 1];
            uint16_t port = (data[pos + 2] << 8) | data[pos + 3];
            uint32_t ip = 0;

            if (attrType == 0x0020) {
                // XOR-MAPPED-ADDRESS
                port ^= (magic_cookie >> 16) & 0xFFFF;
                ip = (data[pos + 4] << 24) | (data[pos + 5] << 16) |
                    (data[pos + 6] << 8) | data[pos + 7];
                ip ^= magic_cookie;
            }
            else {
                // MAPPED-ADDRESS
                ip = (data[pos + 4] << 24) | (data[pos + 5] << 16) |
                    (data[pos + 6] << 8) | data[pos + 7];
            }

            unsigned char* p = reinterpret_cast<unsigned char*>(&ip);
            addr.ip = std::to_string(p[3]) + "." + std::to_string(p[2]) + "." +
                std::to_string(p[1]) + "." + std::to_string(p[0]);
            addr.port = port;

            SetConsoleColor(GRAY);
            // デバッグ出力
            std::cout << "  [Debug] Parsed Attr=" << std::hex << attrType
                << " Family=" << (int)family
                << " Port=" << std::dec << port
                << " IP=" << addr.ip << std::endl;
            SetConsoleColor(WHITE);

            return addr;
        }

        pos += attrLen;
    }

    SetConsoleColor(RED);
    std::cout << "  [Debug] No valid MAPPED-ADDRESS found (len=" << len << ")" << std::endl;
    SetConsoleColor(WHITE);
    return addr;
}




ExternalAddress NATChecker::getExternalAddress(const std::string& stunHost, int stunPort) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        SetConsoleColor(RED);
        std::cerr << "[!] socket() failed\n";
        SetConsoleColor(WHITE);
        return {};
    }

    int timeout = 2000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(stunPort);

    // --- getaddrinfo() を使ってホスト名解決 ---
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* result = nullptr;
    if (getaddrinfo(stunHost.c_str(), nullptr, &hints, &result) != 0 || !result) {
        SetConsoleColor(RED);
        std::cerr << "[!] DNS lookup failed for " << stunHost << "\n";
        SetConsoleColor(WHITE);
        closesocket(sock);
        return {};
    }
    sockaddr_in* addr_in = (sockaddr_in*)result->ai_addr;
    server.sin_addr = addr_in->sin_addr;
    freeaddrinfo(result);

    auto req = createStunRequest();
    int sent = sendto(sock, (char*)req.data(), (int)req.size(), 0, (sockaddr*)&server, sizeof(server));
    if (sent == SOCKET_ERROR) {
        SetConsoleColor(RED);
        std::cerr << "[!] sendto() failed\n";
        SetConsoleColor(WHITE);
        closesocket(sock);
        return {};
    }

    char response[1024];
    sockaddr_in from{};
    int fromlen = sizeof(from);
    int recvlen = recvfrom(sock, response, sizeof(response), 0, (sockaddr*)&from, &fromlen);

    ExternalAddress addr{};
    if (recvlen > 0) {
        addr = parseStunResponse(response, recvlen);

        char from_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(from.sin_addr), from_ip, sizeof(from_ip));
        SetConsoleColor(GRAY);
        std::cout << "  -> Response received from " << from_ip << "\n";
        SetConsoleColor(WHITE);
    }
    else {
        SetConsoleColor(RED);
        std::cout << "  -> [!] Timeout or no response from " << stunHost << ":" << stunPort << "\n";
        SetConsoleColor(WHITE);
    }

    closesocket(sock);
    return addr;
}



std::string NATChecker::classify(const ExternalAddress& addr1, const ExternalAddress& addr2, const ExternalAddress& addr3) {
    if (addr1.port != addr2.port)
        return "Symmetric NAT";
    if (addr1.port == addr3.port)
        return "Full Cone NAT";
    return "Restricted / Port-Restricted Cone NAT";
}

std::string NATChecker::detectNATType() {
    std::vector<std::pair<std::string, int>> stunServers = {
        {"stun.l.google.com", 19302},
        {"stun1.l.google.com", 19302}
    };

    SetConsoleColor(BLUE);
    std::cout << "=== NATタイプ診断ツール ===\n";
    SetConsoleColor(WHITE);

    // --- Test I ---
    SetConsoleColor(MAGENTA);
    std::cout << "[*] テスト I: STUNサーバ1 ポート " << stunServers[0].second << "\n";
    SetConsoleColor(WHITE);
    std::cout << "  -> このテストは UDP 通信可能かを確認します\n";
    SetConsoleColor(WHITE);
    ExternalAddress test1 = getExternalAddress(stunServers[0].first, stunServers[0].second);

    // --- Test II ---
    SetConsoleColor(MAGENTA);
    std::cout << "[*] テスト II: STUNサーバ1 ポート " << stunServers[0].second + 1 << "\n";
    SetConsoleColor(WHITE);
    std::cout << "  -> このテストは NAT がポートを固定するか、宛先ごとに変化するかを確認します\n";
    SetConsoleColor(WHITE);
    ExternalAddress test2 = getExternalAddress(stunServers[0].first, stunServers[0].second + 1);

    // --- Test III ---
    SetConsoleColor(MAGENTA);
    std::cout << "[*] テスト III: STUNサーバ2 ポート " << stunServers[1].second << "\n";
    SetConsoleColor(WHITE);
    std::cout << "  -> このテストは NAT が宛先IPに依存してポートを変えるかどうかを確認します\n";
    SetConsoleColor(WHITE);
    ExternalAddress test3 = getExternalAddress(stunServers[1].first, stunServers[1].second);

    // --- 結果表示 ---
    SetConsoleColor(LIGHT_BLUE);
    std::cout << "\n=== 結果 ===\n";
    std::cout << "テスト1 => " << test1.ip << ":" << test1.port << "\n";
    std::cout << "テスト2 => " << test2.ip << ":" << test2.port << "\n";
    std::cout << "テスト3 => " << test3.ip << ":" << test3.port << "\n\n";
    SetConsoleColor(WHITE);

    std::string natType = classify(test1, test2, test3);
    SetConsoleColor(GREEN);
    std::cout << "判定された NAT タイプ: " << natType << "\n";
    SetConsoleColor(WHITE);
    // --- NATタイプごとの補足説明 ---
    if (natType == "Full Cone NAT") {
        SetConsoleColor(WHITE);
        std::cout << "  ・Full Cone NAT: 外部ポートは固定され、P2P通信が可能です。\n";
        SetConsoleColor(LIGHT_GREEN);
        std::cout << "    ホストとしても非常に向いています \n";
        SetConsoleColor(WHITE);
    }
    else if (natType == "Restricted / Port-Restricted Cone NAT") {
        SetConsoleColor(WHITE);
        std::cout << "  ・Restricted / Port-Restricted Cone NAT: 外部ポートは固定されますが、宛先IP/ポートの制限があります。\n";
        SetConsoleColor(LIGHT_YELLOW);
        std::cout << "    ホストとしてはある程度可能ですが、条件によっては難しい \n";
        SetConsoleColor(WHITE);
    }
    else if (natType == "Symmetric NAT") {
        SetConsoleColor(WHITE);
        std::cout << "  ・Symmetric NAT: 外部ポートは宛先ごとに変化します。\n";
        SetConsoleColor(MAGENTA);
        std::cout << "    通常のP2P通信はほぼ不可能で、リレー必須です \n";
        SetConsoleColor(LIGHT_RED);
        std::cout << "    ホストとしては不向きです\n";
        SetConsoleColor(WHITE);
    }
    else {
        SetConsoleColor(LIGHT_RED);
        std::cout << "  ・不明なNATタイプです。P2P接続の可否は不明です。\n";
        SetConsoleColor(WHITE);
    }

    return natType;
}

