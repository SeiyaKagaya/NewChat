//------------------------------------------------------------
// @file        chat_network.cpp
// @brief       チャット周り（パンチループ＋TCP完遂報告＋LAN判定対応）
//------------------------------------------------------------
#include "chat_network.h"
#include "main.h"

using json = nlohmann::json;



// -----------------------------
// ChatNetwork 実装
// -----------------------------
ChatNetwork::ChatNetwork()
    : m_peer(nullptr),
    m_isHost(false),
    m_port(0),
    m_running(false),
    m_canSend(false)
{
    m_peer = RakNet::RakPeerInterface::GetInstance();
}

ChatNetwork::~ChatNetwork()
{
    // ---------------------------
  // ① すべてのループを停止指令
  // ---------------------------
    m_running = false;

    // パンチループ終了
    StopPunchLoop();

    // ---------------------------
    // ② 監視スレッド停止
    // ---------------------------
    if (m_clientMonitorThread.joinable()) {
        if (m_clientMonitorActive) m_clientMonitorActive = false;
        m_clientMonitorThread.join();
    }

    if (m_hostMonitorThread.joinable()) {
        if (m_hostMonitorActive) m_hostMonitorActive = false;
        m_hostMonitorThread.join();
    }

    // ---------------------------
    // ③ ハートビート停止
    // ---------------------------
    StopHeartbeat();

    // ---------------------------
    // ④ TCP待ち受け終了
    // ---------------------------
    if (m_tcpWaiterActive)
    {
        m_tcpWaiterActive = false;
        if (m_tcpWaiterThread.joinable()) m_tcpWaiterThread.join();
    }

    // ---------------------------
    // ⑤ RakNetインスタンス破棄
    // ---------------------------
    if (m_peer)
        RakNet::RakPeerInterface::DestroyInstance(m_peer);
}

bool ChatNetwork::Init(bool host, unsigned short port, const std::string& bindIp, const std::string& protocol,
    RoomManager& roomManager, const std::string& youExternalIp)
{
    m_isHost = host;
    m_port = port;
    m_clientProtocol = protocol;
    if (host) m_hostProtocol = protocol;

    // RakNet 起動
    RakNet::SocketDescriptor socketDescriptor(port, bindIp.c_str());
    int maxConnections = host ? 32 : 1;
    RakNet::StartupResult result = m_peer->Startup(maxConnections, &socketDescriptor, 1);
    if (result != RakNet::RAKNET_STARTED)
    {
        SetConsoleColor(4);
        std::cout << "RakNet Startup失敗: " << result << std::endl;
        ResetConsoleColor();
        return false;
    }

    if (m_isHost)
    {
        m_peer->SetMaximumIncomingConnections(8);

        if (!m_tcpWaiterActive)
        {
            m_tcpWaiterActive = true;
            m_tcpWaiterThread = std::thread([this]()
                {
                    const unsigned short listenPort = 55555; // TCP待ち受けポート
                    WSADATA wsa;
                    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
                    {
                        std::cerr << "[TCP Waiter] WSAStartup failed\n";
                        m_tcpWaiterActive = false;
                        return;
                    }

                    // 一度だけリトライして安全に止める
                    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                    if (listener == INVALID_SOCKET)
                    {
                        std::cerr << "[TCP Waiter] socket() failed, WSAGetLastError: " << WSAGetLastError() << std::endl;
                        WSACleanup();
                        m_tcpWaiterActive = false;
                        return;
                    }

                    BOOL reuse = TRUE;
                    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

                    sockaddr_in addr{};
                    addr.sin_family = AF_INET;
                    addr.sin_addr.s_addr = INADDR_ANY;
                    addr.sin_port = htons(listenPort);

                    if (bind(listener, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
                    {
                        std::cerr << "[TCP Waiter] bind() failed, WSAGetLastError: " << WSAGetLastError() << std::endl;
                        closesocket(listener);
                        WSACleanup();
                        m_tcpWaiterActive = false;
                        return;
                    }

                    if (listen(listener, SOMAXCONN) == SOCKET_ERROR)
                    {
                        std::cerr << "[TCP Waiter] listen() failed, WSAGetLastError: " << WSAGetLastError() << std::endl;
                        closesocket(listener);
                        WSACleanup();
                        m_tcpWaiterActive = false;
                        return;
                    }

                    SetConsoleColor(2);
                    std::cout << "[TCP] パンチ完遂待受中 (port=" << listenPort << ")...\n";
                    ResetConsoleColor();

                    fd_set readfds;
                    timeval tv;
                    tv.tv_sec = 2;
                    tv.tv_usec = 0;

                    while (m_tcpWaiterActive)
                    {
                        FD_ZERO(&readfds);
                        FD_SET(listener, &readfds);
                        int sel = select(static_cast<int>(listener + 1), &readfds, nullptr, nullptr, &tv);

                        if (sel > 0)
                        {
                            SOCKET client = accept(listener, nullptr, nullptr);
                            if (client != INVALID_SOCKET)
                            {
                                char buf[128] = {};
                                int r = recv(client, buf, sizeof(buf) - 1, 0);
                                if (r > 0)
                                {
                                    std::string s(buf, buf + r);
                                    if (s.find("PUNCH_DONE") != std::string::npos)
                                    {
                                        SetConsoleColor(2);
                                        std::cout << "[TCP] パンチ完遂通知受信 -> m_canSend = true\n";
                                        ResetConsoleColor();
                                        {
                                            std::lock_guard<std::mutex> lk(m_canSendMutex);
                                            m_canSend = true;
                                        }
                                        StopPunchLoop();
                                        SetConsoleColor(3);
                                        std::cout << "\n++++++++++++++新規ユーザーに送信可+++++++++++++++\n";
                                        ResetConsoleColor();
                                    }
                                }
                                closesocket(client);
                            }
                        }
                        else if (sel < 0)
                        {
                            std::cerr << "[TCP Waiter] select error: " << WSAGetLastError() << std::endl;
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }

                    closesocket(listener);
                    WSACleanup();
                    m_tcpWaiterActive = false;
                });
        }
    }

    // 監視スレッド・受信スレッド開始
    m_running = true;
    StartHeartbeat();
    if (m_isHost)
    {
        StartClientMonitor();
        StartRelayPollThread(roomManager, youExternalIp);
    }
    else
    {
        StartHostMonitor();
    }

    m_receiveThread = std::thread(&ChatNetwork::ReceiveLoop, this);
    return true;
}


bool ChatNetwork::ConnectToHost(const std::string& hostIp, const std::string& hostProtocol, unsigned short hostPort)
{
    if (m_isHost) return false;
    if (hostProtocol != "BOTH" && hostProtocol != m_clientProtocol)
    {
        SetConsoleColor(4);
        std::cout << "ホストの通信方式と互換性がありません\n";
        ResetConsoleColor();
        return false;
    }

    m_hostIp = hostIp;
    m_hostPort = hostPort;

    RakNet::ConnectionAttemptResult r = m_peer->Connect(hostIp.c_str(), hostPort, nullptr, 0);


    if (r == RakNet::CONNECTION_ATTEMPT_STARTED)
    {
        SetConsoleColor(2);
        std::cout << "[Client] クライアント→ホストパンチループ開始: " << hostIp << ":" << hostPort << std::endl;
        ResetConsoleColor();

        // ホストのハートビート初期値を登録
        m_lastHeartbeat[m_peer->GetSystemAddressFromIndex(0)] = std::chrono::steady_clock::now();

        StartPunchLoop(hostIp, hostPort, false);
        return true;
    }


    SetConsoleColor(4);
    std::cout << "接続失敗\n";
    ResetConsoleColor();
    return false;
}


void ChatNetwork::ReceiveLoop()
{
    while (m_running)
    {
        for (RakNet::Packet* packet = m_peer->Receive(); packet; m_peer->DeallocatePacket(packet), packet = m_peer->Receive())
        {
            switch (packet->data[0])
            {
            case ID_NEW_INCOMING_CONNECTION:
            {
                SetConsoleColor(6);
                std::cout << "新規接続: " << packet->systemAddress.ToString() << std::endl;
                ResetConsoleColor();

                if (m_isHost)
                {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);

                    // 古い同一 SystemAddress があれば削除
                    m_clients.erase(std::remove_if(m_clients.begin(), m_clients.end(),
                        [&](const ClientInfo& c) { return c.address == packet->systemAddress; }),
                        m_clients.end());

                    ClientInfo info;
                    info.address = packet->systemAddress;
                    info.userName = "";
                    info.localIp = "";
                    info.localPort = 0;
                    info.isSameLAN = false;
                    info.connectedTime = std::chrono::steady_clock::now();

                    m_clients.push_back(info);

                    // 再接続時のパンチループ準備
                    {
                        std::lock_guard<std::mutex> lk(m_canSendMutex);
                        m_canSend = false; // 再接続なので再度パンチ完遂まで待機
                    }

                    // ハートビートも初期化
                    {
                        std::lock_guard<std::mutex> hbLock(m_heartbeatMutex);
                        m_lastHeartbeat[packet->systemAddress] = std::chrono::steady_clock::now();
                    }
                }
                break;
            }

            case ID_PUNCH_PACKET:
            {
                RakNet::BitStream bs(packet->data, packet->length, false);

                // 読み捨て：ID
                RakNet::MessageID pid;
                bs.Read(pid);

                // ラベル（HOST_PUNCH / CLIENT_PUNCH）
                RakNet::RakString rmsg;
                bs.Read(rmsg);
                std::string payload = rmsg.C_String();

                SetConsoleColor(2);
                std::cout << "[Punch] from " << packet->systemAddress.ToString()<< " : " << payload << std::endl;
                ResetConsoleColor();

                // クライアント側
                if (!m_isHost && payload == "HOST_PUNCH")
                {
                    SetConsoleColor(2);
                    std::cout << "[Client] ホストパンチ受信 -> TCP完遂送信\n";
                    ResetConsoleColor();
                    StopPunchLoop();
                    SendPunchDoneTCP(m_hostIp, 55555);
                }

                // ホスト側
                if (m_isHost && payload == "CLIENT_PUNCH")
                {
                    SetConsoleColor(2);
                    std::cout << "[Host]クライアントのUDPパンチ受信\n";
                    ResetConsoleColor();

                    // 追加情報を受信（長さ付きバイナリ）
                    std::string userName = "名無し"; // デフォルト

                    // 残っているデータがあれば JSON 長さ→JSONデータ を読む
                    if (bs.GetNumberOfUnreadBits() > 0)
                    {
                        unsigned int jsonLen = 0;
                        if (bs.Read(jsonLen) && jsonLen > 0)
                        {
                            std::string jsonStr;
                            jsonStr.resize(jsonLen);
                            bs.Read(&jsonStr[0], jsonLen);

                            try {
                                json j = json::parse(jsonStr);

                                // user_name_b64 を復号して userName にする
                                std::string encodedName = j.value("user_name_b64", "");
                                if (!encodedName.empty()) {
                                    userName = FromBase64(encodedName);
                                }
                            }
                            catch (const std::exception& e) {
                                SetConsoleColor(4);
                                std::cerr << "[Host] JSON parse error in PUNCH packet: " << e.what() << std::endl;
                                ResetConsoleColor();
                            }
                        }
                    }
                }

                break;
            }
            case ID_LEAVE_NOTIFICATION:
            {
                if (!m_isHost) {
                    SetConsoleColor(4);
                    std::cout << "[Info] ホストが退出しました。Enterで最初に戻ります...\n";
                    ResetConsoleColor();
                    m_forceExit = true;
                    Stop();  // クライアントは最初に戻る
                    m_running = false;
                }
                else {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);
                    auto it = std::find_if(m_clients.begin(), m_clients.end(),
                        [&](const ClientInfo& c) { return c.address == packet->systemAddress; });
                    if (it != m_clients.end()) {
                        SetConsoleColor(4);
                        std::cout << "[Info] クライアント " << it->userName << " が退出しました。\n";
                        ResetConsoleColor();

                        // 心拍管理からも削除
                        {
                            std::lock_guard<std::mutex> hbLock(m_heartbeatMutex);
                            m_lastHeartbeat.erase(it->address);
                        }

                        // m_canSend リセット
                        {
                            std::lock_guard<std::mutex> lk(m_canSendMutex);
                            m_canSend = false;
                        }

                        // 退出クライアント情報を即削除
                        m_clients.erase(it);
                    }
                }
                break;
            }
            case ID_HEARTBEAT:
            {
                if (m_isHost) {
                    std::lock_guard<std::mutex> lock(m_heartbeatMutex);
                    m_lastHeartbeat[packet->systemAddress] = std::chrono::steady_clock::now();
                }
                else {
                    m_lastHeartbeat[m_peer->GetSystemAddressFromIndex(0)] = std::chrono::steady_clock::now();
                }
                break;
            }
            case ID_GAME_MESSAGE:
            {
                RakNet::BitStream bs(packet->data, packet->length, false);
                RakNet::MessageID msgId; bs.Read(msgId);

                unsigned int len = 0;
                bs.Read(len);
                if (len == 0) break;

                std::string msg(len, '\0');
                bs.Read(&msg[0], len);

                if (m_isHost)
                {
                    RelayPacket(RelayType::Chat, packet->systemAddress, bs);
                }

                size_t sep = msg.find("::");
                if (sep != std::string::npos)
                {
                    std::string name = msg.substr(0, sep);
                    std::string body = msg.substr(sep + 2);
                    SetConsoleColor(15);
                    std::cout << "[" << name << "] " << body << std::endl;
                    ResetConsoleColor();
                }
                break;
            }


            // 🎮 入力系受信
            case ID_GAME_INPUT:
            {
                RakNet::BitStream bsIn(packet->data, packet->length, false);
                bsIn.IgnoreBytes(sizeof(RakNet::MessageID));

                AnyTime input;
                bsIn.Read(input.playerId);
                bsIn.Read(input.inputFlags);
                bsIn.Read(input.timeStamp);

                //なお、入力にリレーは存在しない (クライアントしか送信しない)

                // TODO: ここでゲームロジックに渡す
                break;
            }

            case ID_GAME_REGULAR_UPDATE:
            {
                RakNet::BitStream bsIn(packet->data, packet->length, false);
                bsIn.IgnoreBytes(sizeof(RakNet::MessageID));

                Regular reg;
                bsIn.Read(reg.objectID);
                bsIn.Read(reg.position.x);
                bsIn.Read(reg.position.y);
                bsIn.Read(reg.position.z);
                bsIn.Read(reg.rotation.x);
                bsIn.Read(reg.rotation.y);
                bsIn.Read(reg.rotation.z);
                bsIn.Read(reg.rotation.w);
                bsIn.Read(reg.linerVelocity.x);
                bsIn.Read(reg.linerVelocity.y);
                bsIn.Read(reg.linerVelocity.z);
                bsIn.Read(reg.angularVelocity.x);
                bsIn.Read(reg.angularVelocity.y);
                bsIn.Read(reg.angularVelocity.z);

                if (m_isHost)
                {
                    //ホストはみんなにすでに送ってる。ここに入るのは想定外
                    // クライアントから届いた定期更新を全員へ中継
                    //RelayPacket(RelayType::RegularUpdate, packet->systemAddress, bsIn);
                }

                // クライアントはゲームロジック反映
                break;
            }

            case ID_VOICE_PACKET:
            {
                const char* audioData = reinterpret_cast<const char*>(&packet->data[1]);
                int dataSize = packet->length - 1;

                if (m_isHost)
                {
                    RakNet::BitStream bs;
                    bs.Write((RakNet::MessageID)ID_VOICE_PACKET);
                    bs.Write(audioData, dataSize);
                    RelayPacket(RelayType::Voice, packet->systemAddress, bs);
                }

                // 音声再生
                // DecodeAndPlay(audioData, dataSize);
                break;
            }




            default:
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}


void ChatNetwork::SetPendingPunch(const std::string& extIp, unsigned short extPort,
    const std::string& localIp, unsigned short localPort,
    bool sameLAN, const std::string& userName)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingPunchIp = extIp;
    m_pendingPunchPort = extPort;
    m_pendingLocalIp = localIp;
    m_pendingLocalPort = localPort;
    m_pendingSameLAN = sameLAN;
    m_pendingUserName = userName; // raw bytes as provided (could be CP932)
    m_hasPendingPunch = true;
}

void ChatNetwork::StartPunchLoop(const std::string& targetIp, unsigned short targetPort, bool isHostSide)
{
    bool expected = false;
    if (!m_punchLoopActive.compare_exchange_strong(expected, true)) return;

    m_punchThread = std::thread([this, targetIp, targetPort, isHostSide]()
        {
            RakNet::SystemAddress addr(targetIp.c_str(), targetPort);

            while (m_punchLoopActive)
            {
                RakNet::BitStream bs;
                bs.Write((RakNet::MessageID)ID_PUNCH_PACKET);

                // 最初に簡単なラベルを送る（ASCII）
                RakNet::RakString msg(isHostSide ? "HOST_PUNCH" : "CLIENT_PUNCH");
                bs.Write(msg);

                // 追加情報を同時に送信（クライアント->ホスト用）
                if (!isHostSide)
                {
                    json j;
                    j["local_ip"] = m_pendingLocalIp;
                    j["local_port"] = m_pendingLocalPort;
                    j["same_lan"] = m_pendingSameLAN;

                    // user_name を Base64 化して入れる（ASCII のみ）
                    j["user_name_b64"] = ToBase64(m_pendingUserName);

                    std::string jsonStr = j.dump(); // ここは ASCII のみを含むため安全

                    // 長さ付きバイナリで書き込む（UTF-8を維持）
                    unsigned int jsonLen = static_cast<unsigned int>(jsonStr.size());
                    bs.Write(jsonLen);
                    if (jsonLen > 0) bs.Write(jsonStr.c_str(), jsonLen);
                }

                unsigned int packetSize = bs.GetNumberOfBytesUsed();
                std::cout << "[DEBUG] Sending UDP packet of size: " << packetSize << " bytes" << std::endl;

                m_peer->Send(&bs, HIGH_PRIORITY, UNRELIABLE, 0, addr, false);

                // 送信直後のエラー確認
                int err = WSAGetLastError();
                if (err != 0) {
                    SetConsoleColor(4);
                    std::cout << "[DEBUG] sendto failed with WSAGetLastError(): " << err
                        << " (packet size: " << packetSize << " bytes)" << std::endl;
                    ResetConsoleColor();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
}

void ChatNetwork::StopPunchLoop()
{
    bool expected = true;
    if (!m_punchLoopActive.compare_exchange_strong(expected, false)) return;

    if (m_punchThread.joinable())
        m_punchThread.join();
}

void ChatNetwork::SendPunchDoneTCP(const std::string& targetIp, unsigned short port)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        SetConsoleColor(4);
        std::cerr << "[TCP] WSAStartup failed\n";
        ResetConsoleColor();
        return; 
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        SetConsoleColor(4);
        std::cerr << "[TCP] socket() failed\n";
        ResetConsoleColor();
        WSACleanup();
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, targetIp.c_str(), &addr.sin_addr) <= 0) { closesocket(sock); WSACleanup(); return; }

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0)
    {
        const char* payload = "PUNCH_DONE";
        send(sock, payload, static_cast<int>(strlen(payload)), 0);

        SetConsoleColor(2);
        std::cout << "[TCP] PUNCH_DONE送信 -> m_canSend = true\n";
        ResetConsoleColor();
        {
            std::lock_guard<std::mutex> lk(m_canSendMutex);
            m_canSend = true; // ★ クライアント側: TCP送信完了でtrue
        }
        SetConsoleColor(3);
        std::cout << "\n++++++++++++++チャット送信可+++++++++++++++\n";
        ResetConsoleColor();
    }
    else
    {
        SetConsoleColor(4);
        std::cerr << "[TCP] connect failed to " << targetIp << ":" << port << "\n";
        ResetConsoleColor();
    }

    closesocket(sock);
    WSACleanup();
}



void ChatNetwork::Stop()
{
    m_running = false;
    StopPunchLoop();

    if (m_tcpWaiterActive)
    {
        m_tcpWaiterActive = false;
        if (m_tcpWaiterThread.joinable() && m_tcpWaiterThread.get_id() != std::this_thread::get_id())
            m_tcpWaiterThread.join();
    }

    if (m_clientMonitorThread.joinable() && m_clientMonitorThread.get_id() != std::this_thread::get_id())
        m_clientMonitorThread.join(); // ★★修正：自己join回避

    if (m_hostMonitorThread.joinable() && m_hostMonitorThread.get_id() != std::this_thread::get_id())
        m_hostMonitorThread.join();   // ★★修正：自己join回避

    if (m_receiveThread.joinable() && m_receiveThread.get_id() != std::this_thread::get_id()) {
        m_receiveThread.join();
    }
}

const RakNet::SystemAddress& ChatNetwork::GetMyAddress() const
{
    return m_peer->GetMyBoundAddress();
}
void ChatNetwork::SetUserName(const std::string& name)
{
    m_userName = name;
}
void ChatNetwork::SetConsoleColor(WORD color)
{

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}
void ChatNetwork::ResetConsoleColor()
{
    SetConsoleColor(7); // 標準グレー
}




// -----------------------------
// Base64 ユーティリティ
// -----------------------------
std::string ChatNetwork::ToBase64(const std::string& input)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            output.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) output.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (output.size() % 4) output.push_back('=');
    return output;
}

std::string ChatNetwork::FromBase64(const std::string& input)
{
    static int T[256];
    static bool init = false;
    if (!init) {
        init = true;
        for (int i = 0; i < 256; ++i) T[i] = -1;
        for (int i = 'A'; i <= 'Z'; ++i) T[i] = i - 'A';
        for (int i = 'a'; i <= 'z'; ++i) T[i] = i - 'a' + 26;
        for (int i = '0'; i <= '9'; ++i) T[i] = i - '0' + 52;
        T[(unsigned char)'+'] = 62;
        T[(unsigned char)'/'] = 63;
    }

    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

void ChatNetwork::SendLeaveNotification()
{
    if (m_peer->NumberOfConnections() == 0) return;

    RakNet::BitStream bs;
    bs.Write((RakNet::MessageID)ID_LEAVE_NOTIFICATION);
    m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_peer->GetSystemAddressFromIndex(0), false);
}

void ChatNetwork::BroadcastLeaveNotification()
{
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    for (auto& c : m_clients)
    {
        RakNet::BitStream bs;
        bs.Write((RakNet::MessageID)ID_LEAVE_NOTIFICATION);
        m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, c.address, false);
    }
}

void ChatNetwork::StartHeartbeat()
{
    bool expected = false;
    if (!m_heartbeatActive.compare_exchange_strong(expected, true)) return;

    m_heartbeatThread = std::thread([this]()
        {
            while (m_heartbeatActive)
            {
                if (m_isHost)
                {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);
                    for (auto& c : m_clients)
                    {
                        RakNet::BitStream bs;
                        bs.Write((RakNet::MessageID)ID_HEARTBEAT);
                        m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, c.address, false);
                    }
                }
                else
                {
                    if (m_peer->NumberOfConnections() > 0)
                    {
                        RakNet::BitStream bs;
                        bs.Write((RakNet::MessageID)ID_HEARTBEAT);
                        m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_peer->GetSystemAddressFromIndex(0), false);
                    }
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
}

void ChatNetwork::StopHeartbeat()
{
    bool expected = true;
    if (!m_heartbeatActive.compare_exchange_strong(expected, false)) return;
    if (m_heartbeatThread.joinable()) m_heartbeatThread.join();
}
RakNet::SystemAddress ChatNetwork::GetMyHostAddress() const
{
    if (!m_isHost && m_peer->NumberOfConnections() > 0)
    {
        return m_peer->GetSystemAddressFromIndex(0);
    }
    return RakNet::UNASSIGNED_SYSTEM_ADDRESS;
}
std::chrono::steady_clock::time_point ChatNetwork::GetLastHeartbeat(RakNet::SystemAddress addr)
{
    std::lock_guard<std::mutex> lock(m_heartbeatMutex);
    auto it = m_lastHeartbeat.find(addr);
    if (it != m_lastHeartbeat.end())
        return it->second;
    return std::chrono::steady_clock::now() - std::chrono::seconds(100); // 過去時間を返す（タイムアウト扱い）
}
void ChatNetwork::CheckClientTimeouts()
{
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_clientsMutex);

    for (auto it = m_clients.begin(); it != m_clients.end();)
    {
        auto last = GetLastHeartbeat(it->address);

        // ★★★ 新規追加: 接続直後はスキップ
        auto connectedAgo = std::chrono::duration_cast<std::chrono::seconds>(now - it->connectedTime);
        if (connectedAgo < std::chrono::seconds(5)) {
            ++it;
            continue;
        }

        if (std::chrono::duration_cast<std::chrono::seconds>(now - last) > m_heartbeatTimeout)
        {
            SetConsoleColor(4);
            std::cout << "[Info] クライアント " << it->userName << " が応答なし。退出扱いにします。\n";
            ResetConsoleColor();

            // 他クライアントに退出通知送信
            RakNet::BitStream bs;
            bs.Write((RakNet::MessageID)ID_LEAVE_NOTIFICATION);
            for (auto& c : m_clients)
            {
                if (c.address != it->address) {
                    m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, c.address, false);
                }
            }

            // heartbeat も削除
            {
                std::lock_guard<std::mutex> hbLock(m_heartbeatMutex);
                m_lastHeartbeat.erase(it->address);
            }

            // ★ ここに追加
            {
                std::lock_guard<std::mutex> lk(m_canSendMutex);
                m_canSend = false;
            }

            it = m_clients.erase(it);
        }
        else
        {
            ++it;
        }
    }
}


std::optional<std::chrono::steady_clock::time_point> ChatNetwork::GetLastHeartbeatOpt(RakNet::SystemAddress addr)
{
    std::lock_guard<std::mutex> lock(m_heartbeatMutex);
    auto it = m_lastHeartbeat.find(addr);
    if (it != m_lastHeartbeat.end())
        return it->second;
    return std::nullopt;
}


void ChatNetwork::StartRelayPollThread(RoomManager& roomManager,const std::string& hostExternalIp)
{// UDP待機スレッド開始時などで呼ばれる

    SetConsoleColor(3);
    std::cout << "\nサーバーからjoin受付開始\n";
    ResetConsoleColor();

    running = true; // スレッドループ制御用フラグ

  // 引数をラムダでキャプチャ
    std::thread([this, &roomManager, hostExternalIp]() {
        while (running) {
            auto infoOpt = roomManager.GetPendingClientInfo(hostExternalIp);

            if (infoOpt.has_value()) {
                auto info = infoOpt.value();
                std::cout << "[Relay] Client info received via server relay:\n";
                std::cout << "  external_ip: " << info.external_ip << "\n";
                std::cout << "  external_port: " << info.external_port << "\n";
          
                std::cout << "  local_ip: " << info.local_ip << "\n";
                std::cout << "  local_port: " << info.local_port << "\n";
                std::cout << "  client_name: " << info.client_name << "\n";

                //--------------------------------------------------
                // 🔍 同一LAN判定 (シンプルで確実なロジック)
                //--------------------------------------------------
                bool sameLan = false;

                // 自分のローカルIPを取得
                std::string myLocalIp = GetLocalIPAddress(); // ← 既にある関数を想定（なければ下に作る）

                if (IsSameLAN(myLocalIp,info.local_ip))
                {
                    SetConsoleColor(1);
                    std::cout << "[Host] 同一LAN検出: " << info.local_ip << ":" << info.local_port << " でパンチ開始\n";
                    ResetConsoleColor();

                    StartPunchLoop(info.local_ip, info.local_port, true);
                }
                else
                {
                    SetConsoleColor(1);
                    std::cout << "[Host] グローバル接続: " << info.external_ip << ":" << info.external_port << " でパンチ開始\n";
                    ResetConsoleColor();

                    StartPunchLoop(info.external_ip, info.external_port, true);
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        }).detach();
}
//
//void ChatNetwork::StartClientRelayPoll(RoomManager& roomManager, const std::string& roomName, bool isSameLan, const std::string& hostExternalIp)
//{
//    m_clientRelayPollThread = std::thread([this, &roomManager, roomName]() {
//        while (m_running) {
//            auto info = roomManager.GetPendingClientInfo(roomName, hostExternalIp); // ← インスタンス経由で呼ぶ
//            if (info.has_value()) {
//                const std::string& clientIp = info->external_ip;
//                unsigned short clientPort = static_cast<unsigned short>(info->external_port);
//
//                printf("[Relay] New client info via server: %s:%hu\n", clientIp.c_str(), clientPort);
//
//                // ここでホスト→クライアントにパンチ開始
//                StartPunchLoop(clientIp, clientPort, true);
//            }
//            std::this_thread::sleep_for(std::chrono::seconds(1));
//        }
//        });
//}


// ============================================================
// 新規追加：ホスト／クライアントの生存監視スレッド
// ============================================================
void ChatNetwork::StartClientMonitor()
{
    if (m_clientMonitorActive || !m_isHost) return;
    m_clientMonitorActive = true;

    m_clientMonitorThread = std::thread([this]()
        {
            while (m_running)
            {
                CheckClientTimeouts();
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            m_clientMonitorActive = false;
        });
}

void ChatNetwork::StartHostMonitor()
{
    if (m_hostMonitorActive || m_isHost) return;
    m_hostMonitorActive = true;

    m_hostMonitorThread = std::thread([this]()
        {
            while (m_running)
            {
                auto now = std::chrono::steady_clock::now();
                auto lastOpt = GetLastHeartbeatOpt(GetMyHostAddress());
                if (lastOpt && std::chrono::duration_cast<std::chrono::seconds>(now - *lastOpt) > std::chrono::seconds(3))
                {
                    SetConsoleColor(4);
                    std::cout << "[Client] ホスト応答なし。Enterで最初に戻ります...\n";
                    ResetConsoleColor();
                    m_forceExit = true;     // ★追加7
                    Stop();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            m_hostMonitorActive = false;
        });
}

//--------------------------------------------------------------------------------------
// ++++++++++++++++++++++++++++++ここから下は送信関係+++++++++++++++++++++++++++++++++++
//--------------------------------------------------------------------------------------

void ChatNetwork::RelayPacket(RelayType type, const RakNet::SystemAddress& sender, const RakNet::BitStream& data)
{
    if (!m_isHost) return;

    PacketPriority priority = HIGH_PRIORITY;
    PacketReliability reliability = RELIABLE_ORDERED;
    unsigned char channel = 0;

    switch (type)
    {
    case RelayType::RegularUpdate:
        priority = HIGH_PRIORITY;
        reliability = RELIABLE_ORDERED_WITH_ACK_RECEIPT;
        channel = 1;
        break;

    case RelayType::Chat:
        priority = HIGH_PRIORITY;
        reliability = RELIABLE_ORDERED;
        channel = 2;
        break;

    case RelayType::Voice:
        priority = HIGH_PRIORITY;
        reliability = UNRELIABLE_SEQUENCED;
        channel = 3;
        break;
    }

    // 送信先リスト構築
    std::vector<RakNet::SystemAddress> targets;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& c : m_clients)
        {
            // RegularUpdate は全員、それ以外は送信者以外
            if (type == RelayType::RegularUpdate || c.address != sender)
                targets.push_back(c.address);
        }
    }

    // 各ターゲットに送信
    for (auto& addr : targets)
    {
        m_peer->Send(&data, priority, reliability, channel, addr, false);
    }
}

// ------------------------------------
// 入力系送信（随時）クライアントのみ送信
// ------------------------------------
void ChatNetwork::SendGameInput(const AnyTime& inputData)
{
    if (!m_peer) return;

    RakNet::BitStream bs;
    bs.Write((RakNet::MessageID)ID_GAME_INPUT);
    bs.Write(inputData.playerId);
    bs.Write(inputData.inputFlags);
    bs.Write(inputData.timeStamp);

    // 軽量で確実性不要：UNRELIABLE
    m_peer->Send(&bs, HIGH_PRIORITY, UNRELIABLE, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);
}

// ------------------------------------
// 定期更新送信（ホスト ）
// ------------------------------------
void ChatNetwork::SendRegularUpdate(const Regular& update)
{
    if (!m_peer) return;

    RakNet::BitStream bs;
    bs.Write((RakNet::MessageID)ID_GAME_REGULAR_UPDATE);
    bs.Write(update.objectID);
    bs.Write(update.position.x);
    bs.Write(update.position.y);
    bs.Write(update.position.z);
    bs.Write(update.rotation.x);
    bs.Write(update.rotation.y);
    bs.Write(update.rotation.z);
    bs.Write(update.rotation.w);
    bs.Write(update.linerVelocity.x);
    bs.Write(update.linerVelocity.y);
    bs.Write(update.linerVelocity.z);
    bs.Write(update.angularVelocity.x);
    bs.Write(update.angularVelocity.y);
    bs.Write(update.angularVelocity.z);

    if (m_isHost)
    {
        // ホストなら中継
        RelayPacket(RelayType::RegularUpdate, RakNet::UNASSIGNED_SYSTEM_ADDRESS, bs);
    }
    else if (m_peer->NumberOfConnections() > 0)
    {
        //おかしい
        // クライアント → ホスト
       // m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED_WITH_ACK_RECEIPT, 1, m_peer->GetSystemAddressFromIndex(0), false);
    }
}



// ------------------------------------
// ボイスデータ送信（ホスト or クライアント共通）
// ------------------------------------
void ChatNetwork::SendVoicePacket(const char* audioData, int dataSize)
{
    if (!m_peer || dataSize <= 0) return;

    RakNet::BitStream bs;
    bs.Write((RakNet::MessageID)ID_VOICE_PACKET);
    bs.Write(audioData, dataSize);

    if (m_isHost)
    {
        RelayPacket(RelayType::Voice, RakNet::UNASSIGNED_SYSTEM_ADDRESS, bs);
    }
    else if (m_peer->NumberOfConnections() > 0)
    {
        m_peer->Send(&bs, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 3, m_peer->GetSystemAddressFromIndex(0), false);
    }
}



void ChatNetwork::SendMessage(const std::string& message)
{
    RakNet::BitStream bs;
    bs.Write((RakNet::MessageID)ID_GAME_MESSAGE);

    std::string senderName = m_userName.empty() ? "匿名" : m_userName;
    std::string payload = senderName + "::" + message;
    unsigned int len = (unsigned int)payload.size();
    bs.Write(len);
    bs.Write(payload.c_str(), len);

    if (m_isHost)
    {
        RelayPacket(RelayType::Chat, RakNet::UNASSIGNED_SYSTEM_ADDRESS, bs);
    }
    else if (m_peer->NumberOfConnections() > 0)
    {
        m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 2, m_peer->GetSystemAddressFromIndex(0), false);
    }
}
