//------------------------------------------------------------
// @file        chat_network.cpp
// @brief       チャット周り（パンチループ＋TCP完遂報告＋LAN判定対応）
//------------------------------------------------------------
#include "chat_network.h"
#include "main.h"
#include "room_manager.h"
#include <unordered_set>
#include "includemanager.h"
using json = nlohmann::json;



// -----------------------------
// コンストラクタ
// -----------------------------
ChatNetwork::ChatNetwork()
    : m_peer(nullptr),
    m_isHost(false),
    m_port(0),
    m_running(false),
    m_canSend(false),
    m_lastHostHeartbeatTime(std::chrono::steady_clock::now())
{
    m_peer = RakNet::RakPeerInterface::GetInstance();
}

//デストラクタ
ChatNetwork::~ChatNetwork()
{

    m_running = false;

    // パンチループ終了
    StopPunchLoop();

    // ---------------------------
    // ② 監視スレッド停止
    // ---------------------------
    if (m_clientMonitorThread.joinable())
    {
        if (m_clientMonitorActive)
        {
            m_clientMonitorActive = false;
            m_clientMonitorThread.join();
        }
    }

    if (m_hostMonitorThread.joinable())
    {
        if (m_hostMonitorActive)
        {
            m_hostMonitorActive = false;
            m_hostMonitorThread.join();
        }
    }

    if (m_tcpWaiterActive)
    {
        m_tcpWaiterActive = false;
        if (m_tcpWaiterThread.joinable())
        {
            m_tcpWaiterThread.join();
        }
    }

    if (m_peer)
    {
        RakNet::RakPeerInterface::DestroyInstance(m_peer);
    }
}

//init
bool ChatNetwork::Init(bool host, unsigned short port, const std::string& bindIp, const std::string& protocol,
    RoomManager& roomManager, const std::string& youExternalIp, ConnectionMode MyConnectMode)
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
        SetConsoleColor(RED);
        std::cout << "[エラータグ1]RakNet Startup失敗: " << result << std::endl;
        SetConsoleColor(WHITE);
        return false;
    }

    if (m_isHost)
    {
        m_peer->SetMaximumIncomingConnections(8);

        if (!m_tcpWaiterActive)
        {
            m_tcpWaiterActive = true;
            m_tcpWaiterThread = std::thread([this, MyConnectMode]()
                {
                    const unsigned short listenPort = 55555; // TCP待ち受けポート
                    WSADATA wsa;
                    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
                    {
                        std::cerr << "[エラータグ2][TCP Waiter] WSAStartup failed\n";
                        m_tcpWaiterActive = false;
                        return;
                    }


                    if (MyConnectMode != ConnectionMode::Relay)
                    {
                        // 一度だけリトライして安全に止める
                        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                        if (listener == INVALID_SOCKET)
                        {
                            std::cerr << "[エラータグ3][TCP Waiter] socket() failed, WSAGetLastError: " << WSAGetLastError() << std::endl;
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
                            std::cerr << "[エラータグ4][TCP Waiter] bind() failed, WSAGetLastError: " << WSAGetLastError() << std::endl;
                            closesocket(listener);
                            WSACleanup();
                            m_tcpWaiterActive = false;
                            return;
                        }

                        if (listen(listener, SOMAXCONN) == SOCKET_ERROR)
                        {
                            std::cerr << "[エラータグ5][TCP Waiter] listen() failed, WSAGetLastError: " << WSAGetLastError() << std::endl;
                            closesocket(listener);
                            WSACleanup();
                            m_tcpWaiterActive = false;
                            return;
                        }





                        SetConsoleColor(GREEN);
                        std::cout << "[TCP] パンチ完遂待受中 (port=" << listenPort << ")...\n";
                        SetConsoleColor(WHITE);

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
                                            SetConsoleColor(LIGHT_BLUE);
                                            std::cout << "[TCP] パンチ完遂通知受信 -> m_canSend = true\n";
                                            SetConsoleColor(WHITE);
                                            StartClientMonitorLoop();//クライアント監視開始
                                            SetSendOk();
                                        }
                                    }
                                    closesocket(client);
                                }
                            }
                            else if (sel < 0)
                            {
                                std::cerr << "[エラータグ6][TCP Waiter] select error: " << WSAGetLastError() << std::endl;
                                break;
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }

                        closesocket(listener);
                        WSACleanup();
                        m_tcpWaiterActive = false;
                    }
                });
        }
    }

    // 監視スレッド・受信スレッド開始
    m_running = true;

    //無条件に開通---ここでリレー通信を受け取る
    StartRelayReceiver(youExternalIp);

    if (m_isHost)
    {
        StartRelayPollThread(roomManager, youExternalIp, MyConnectMode);//ホスト1-リレー受信待機
    }
 
    m_receiveThread = std::thread(&ChatNetwork::ReceiveLoop, this);

    return true;
}

//クライアント側、ホストへの接続
bool ChatNetwork::ConnectToHost(const std::string& hostIp, const std::string& hostProtocol, unsigned short hostPort, ConnectionMode MyConnectMode)
{

    if (m_isHost)
    {
        return false;
    }
    if (hostProtocol != "BOTH" && hostProtocol != m_clientProtocol)
    {
        SetConsoleColor(RED);
        std::cout << "ホストの通信方式と互換性がありません\n";
        SetConsoleColor(WHITE);
        return false;
    }

    m_hostIp = hostIp;
    m_hostPort = hostPort;

    RakNet::ConnectionAttemptResult r = m_peer->Connect(hostIp.c_str(), hostPort, nullptr, 0);

    // 選択方式に応じて処理
    switch (MyConnectMode) 
    {
    case ConnectionMode::LocalP2P:
        //なんならここでTCP送信

        if (r == RakNet::CONNECTION_ATTEMPT_STARTED)
        {
            SetConsoleColor(LIGHT_YELLOW);
            std::cout << "ローカルP2Pのためパンチホールは行いません" << hostIp << ":" << hostPort << std::endl;
            SetConsoleColor(WHITE);

            // ホストのハートビート初期値を登録
            m_lastHeartbeat[m_peer->GetSystemAddressFromIndex(0)] = std::chrono::steady_clock::now();
            return true;
        }
        break;

    case ConnectionMode::P2P:
        
        if (r == RakNet::CONNECTION_ATTEMPT_STARTED)
        {
            SetConsoleColor(LIGHT_YELLOW);
            std::cout << "[Client] クライアント→ホストパンチループ開始: " << hostIp << ":" << hostPort << std::endl;
            SetConsoleColor(WHITE);

            // ホストのハートビート初期値を登録
            m_lastHeartbeat[m_peer->GetSystemAddressFromIndex(0)] = std::chrono::steady_clock::now();

            StartPunchLoop(hostIp, hostPort, false);
            return true;
        }
        break;

    case ConnectionMode::Relay:
   
        if (r == RakNet::CONNECTION_ATTEMPT_STARTED)
        {
            SetConsoleColor(LIGHT_GREEN);
            std::cout << "リレー通信のためパンチホールは行いません" << hostIp << ":" << hostPort << std::endl;
            SetConsoleColor(WHITE);

            return true;
        }
        break;
    }

    SetConsoleColor(RED);
    std::cout << "接続失敗\n";
    SetConsoleColor(WHITE);
    return false;
}


//UDP受信ループ
void ChatNetwork::ReceiveLoop()
{
    while (m_running)
    {
        for (RakNet::Packet* packet = m_peer->Receive(); packet; m_peer->DeallocatePacket(packet), packet = m_peer->Receive())
        {
            switch (packet->data[0])
            {
            case ID_NEW_INCOMING_CONNECTION:
            {//新規接続検知
                SetConsoleColor(YELLOW);
                std::cout << "新規接続: " << packet->systemAddress.ToString() << std::endl;
                SetConsoleColor(WHITE);

                break;
            }

            case ID_PUNCH_PACKET:
            {//UDPパンチ
                RakNet::BitStream bs(packet->data, packet->length, false);

                // 読み捨て：ID
                RakNet::MessageID pid;
                bs.Read(pid);

                // ラベル（HOST_PUNCH / CLIENT_PUNCH）
                RakNet::RakString rmsg;
                bs.Read(rmsg);
                std::string payload = rmsg.C_String();

                SetConsoleColor(BLUE);
                std::cout << "[Punch] from " << packet->systemAddress.ToString()<< " : " << payload << std::endl;
                SetConsoleColor(WHITE);

                // クライアント側
                if (!m_isHost && payload == "HOST_PUNCH")
                {
                    SetConsoleColor(LIGHT_BLUE);
                    std::cout << "[Client] ホストパンチ受信 -> TCP完遂送信\n";
                    SetConsoleColor(WHITE);
                    StopPunchLoop();
                    SendPunchDoneTCP(m_hostIp, 55555);
                }

                // ホスト側
                if (m_isHost && payload == "CLIENT_PUNCH")
                {
                    SetConsoleColor(LIGHT_BLUE);
                    std::cout << "[Host]クライアントのUDPパンチ受信\n";
                    SetConsoleColor(WHITE);
                }

                break;
            }
            case ID_LEAVE_NOTIFICATION:
            {//退席通知取得

                RakNet::BitStream bs(packet->data, packet->length, false);
                RakNet::MessageID msgId;
                bs.Read(msgId);

                // 名前（送信者）を受け取る
                unsigned int len = 0;
                bs.Read(len);
                std::string from(len, '\0');
                if (len > 0) bs.Read(&from[0], len);

                if (!m_isHost)
                { // クライアント側
                    if (from == "host" || from == m_hostIp)
                    {
                        // ホストが落ちた場合
                        SetConsoleColor(RED);
                        std::cout << "[Info] ホストが退出しました。Enterで最初に戻ります...\n";
                        SetConsoleColor(WHITE);

                        m_forceExit = true;
                        Stop();  // クライアントは最初に戻る
                        m_running = false;
                    }
                    else
                    {
                        // 他クライアントが退出した場合
                        SetConsoleColor(RED);
                        std::cout << "[Info] " << from << " が退出しました。\n";
                        SetConsoleColor(WHITE);
                    }
                }
                else
                { // ホスト側
                    std::lock_guard<std::mutex> lock(m_clientsMutex);

                    // 対象クライアントを検索（SystemAddress一致で）
                    auto it = std::find_if(m_clients.begin(), m_clients.end(),
                        [&](const ClientInfo& c) { return c.address == packet->systemAddress || c.userName == from; });

                    if (it != m_clients.end())
                    {
                        SetConsoleColor(RED);
                        std::cout << "[Info] クライアント " << it->userName << " が退出しました。\n";
                        SetConsoleColor(WHITE);

                        // 削除前に、他クライアントへ通知を送信
                        for (auto& c : m_clients)
                        {
                            if (c.guid == it->guid) continue; // 退出本人は除外

                            switch (c.connectionMode)
                            {
                            case ConnectionMode::Relay:
                                RelaySendDataToServer(c.externalIp, "system", "leave", it->userName);
                                break;

                            case ConnectionMode::P2P:
                            case ConnectionMode::LocalP2P:
                            {
                                RakNet::BitStream bsOut;
                                bsOut.Write((RakNet::MessageID)ID_LEAVE_NOTIFICATION);
                                unsigned int len2 = static_cast<unsigned int>(it->userName.size());
                                bsOut.Write(len2);
                                bsOut.Write(it->userName.c_str(), len2);

                                RakNet::SystemAddress targetAddr;
                                if (c.connectionMode == ConnectionMode::LocalP2P)
                                    targetAddr.FromStringExplicitPort(c.localIp.c_str(), c.localPort);
                                else
                                    targetAddr = c.address;

                                m_peer->Send(&bsOut, HIGH_PRIORITY, RELIABLE_ORDERED_WITH_ACK_RECEIPT, 2, targetAddr, false);
                                break;
                            }
                            }
                        }

                        // クライアントリストから削除
                        m_clients.erase(it);

                        // クライアントが0人になったら送信停止
                        if (m_clients.empty())
                        {
                            SetConsoleColor(RED);
                            std::cout << "[Info] クライアントが0人です。\n";
                            SetConsoleColor(WHITE);
                            std::lock_guard<std::mutex> lk(m_canSendMutex);
                            m_canSend = false;
                        }
                    }
                }

                break;
            }
            case ID_HEARTBEAT:
            {//相手の心拍取得
                RakNet::BitStream bs(packet->data, packet->length, false);
                RakNet::MessageID msgId; bs.Read(msgId);

                unsigned int len = 0; bs.Read(len);
                std::string msg(len, '\0');
                if (len > 0) bs.Read(&msg[0], len);

                if (m_isHost)
                {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);
                    auto it = std::find_if(m_clients.begin(), m_clients.end(),
                        [&](const ClientInfo& c) { return c.address == packet->systemAddress; });
                    if (it != m_clients.end())
                        it->lastHeartbeatTime = std::chrono::steady_clock::now();
                }
                else
                {
                    // ホストからのHeartbeat
                    m_lastHostHeartbeatTime = std::chrono::steady_clock::now();
                }

                break;
            }
            case ID_GAME_MESSAGE:
            {//ゲームチャット
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
                    SetConsoleColor(BRIGHT_WHITE);
                    std::cout << "[" << name << "] " << body << std::endl;
                    SetConsoleColor(WHITE);
                }
                break;
            }
            case ID_GAME_INPUT:
            {// 🎮 入力系受信

                SetConsoleColor(LIGHT_YELLOW);
                std::cout << "[UDP]ホストへのへ入力データ受信 " << std::endl;
                SetConsoleColor(WHITE);

                RakNet::BitStream bsIn(packet->data, packet->length, false);
                bsIn.IgnoreBytes(sizeof(RakNet::MessageID));

                AnyTime input;
                bsIn.Read(input.playerId);
                bsIn.Read(input.inputFlags);
                bsIn.Read(input.timeStamp);

                //なお、入力にリレーは存在しない (クライアントしか送信しない)
                // 🔍 デバッグ出力
                SetConsoleColor(LIGHT_CYAN);
                std::cout << "[Debug] 受信したAnyTimeデータ：" << std::endl;
                std::cout << "  playerId   : " << input.playerId << std::endl;
                std::cout << "  inputFlags : " << (input.inputFlags ? "true" : "false") << std::endl;
                std::cout << "  timeStamp  : " << input.timeStamp << " (ms)" << std::endl;
                SetConsoleColor(WHITE);
                // TODO: ここでゲームロジックに渡す
                break;
            }

            case ID_GAME_REGULAR_UPDATE:
            {//定期更新受信


                SetConsoleColor(LIGHT_YELLOW);
                std::cout << "[UDP]クライアントへの定期更新データ受信(testなのでホストも受け取るかも) " << std::endl;
                SetConsoleColor(WHITE);

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

                // 🔍 デバッグ出力
                SetConsoleColor(LIGHT_CYAN);
                std::cout << "[Debug] 受信したRegularデータ：" << std::endl;
                std::cout << "  ObjectID: " << reg.objectID << std::endl;
                std::cout << "  Position: (" << reg.position.x << ", " << reg.position.y << ", " << reg.position.z << ")" << std::endl;
                std::cout << "  Rotation: (" << reg.rotation.x << ", " << reg.rotation.y << ", " << reg.rotation.z << ", " << reg.rotation.w << ")" << std::endl;
                std::cout << "  LinearVelocity : (" << reg.linerVelocity.x << ", " << reg.linerVelocity.y << ", " << reg.linerVelocity.z << ")" << std::endl;
                std::cout << "  AngularVelocity: (" << reg.angularVelocity.x << ", " << reg.angularVelocity.y << ", " << reg.angularVelocity.z << ")" << std::endl;
                SetConsoleColor(WHITE);


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
            {//ボイチャ

                SetConsoleColor(LIGHT_YELLOW);
                std::cout << "[UDP]ボイスデータ受信 "  << std::endl;
                SetConsoleColor(WHITE);

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

//クライアントが自身の情報を格納
void ChatNetwork::SetPendingPunch(const std::string& extIp, unsigned short extPort,
    const std::string& localIp, unsigned short localPort,
    bool sameLAN, const std::string& userName, ConnectionMode  connectionMode)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingPunchIp = extIp;
    m_pendingPunchPort = extPort;
    m_pendingLocalIp = localIp;
    m_pendingLocalPort = localPort;
    m_pendingSameLAN = sameLAN;
    m_pendingUserName = userName; // raw bytes as provided (could be CP932)
    m_pendingConnectionMode = connectionMode; // 使用する通信方式

    m_hasPendingPunch = true;
}

//パンチループ開始部(ホスト/クライアント共通)
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

                    j["connection_mode"] = m_pendingConnectionMode; // ★追加

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
                    SetConsoleColor(RED);
                    std::cout << "[エラータグ7][DEBUG] sendto failed with WSAGetLastError(): " << err
                        << " (packet size: " << packetSize << " bytes)" << std::endl;
                    SetConsoleColor(WHITE);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
}

//パンチ止め
void ChatNetwork::StopPunchLoop()
{
    bool expected = true;
    if (!m_punchLoopActive.compare_exchange_strong(expected, false)) return;

    if (m_punchThread.joinable())
        m_punchThread.join();
}

//TCP完遂送信
void ChatNetwork::SendPunchDoneTCP(const std::string& targetIp, unsigned short port)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        SetConsoleColor(RED);
        std::cerr << "[エラータグ8][TCP] WSAStartup failed\n";
        SetConsoleColor(WHITE);
        return; 
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        SetConsoleColor(RED);
        std::cerr << "[エラータグ9][TCP] socket() failed\n";
        SetConsoleColor(WHITE);
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

        SetConsoleColor(GREEN);
        std::cout << "[TCP] PUNCH_DONE送信 -> m_canSend = true\n";
        SetConsoleColor(WHITE);
        StartHeartbeatLoop();
        StartHostMonitorLoop();//ホスト監視開始
        SetSendOk();//チャット可能に
    }
    else
    {
        SetConsoleColor(RED);
        std::cerr << "[エラータグ10][TCP] connect failed to " << targetIp << ":" << port << "\n";
        SetConsoleColor(WHITE);
    }

    closesocket(sock);
    WSACleanup();
}

//サーバーから初回リレー受信(ホスト側のみ)
void ChatNetwork::StartRelayPollThread(RoomManager& roomManager, const std::string& hostExternalIp, ConnectionMode MyConnectMode)
{

    SetConsoleColor(BLUE);
    std::cout << "\n[Host] サーバーからjoin受付開始\n";
    SetConsoleColor(WHITE);

    running = true;

    std::thread([this, &roomManager, hostExternalIp, MyConnectMode]()
        {
            // ★ 処理済みクライアントを記録するセット
            std::unordered_set<std::string> processedClients;

            while (running)
            {
                auto infoOpt = roomManager.GetPendingClientInfo(hostExternalIp);
                if (infoOpt.has_value())
                {
                    auto info = infoOpt.value();

                    std::string key = info.external_ip + ":" + std::to_string(info.external_port);
                    if (processedClients.find(key) != processedClients.end())
                    {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        continue;
                    }
                    processedClients.insert(key);


                    // --- デバッグ出力 ---
                    std::cout << "\n[Relay] 初回リレー伝達受信:\n";
                    std::cout << "  external_ip: " << info.external_ip << "\n";
                    std::cout << "  external_port: " << info.external_port << "\n";
                    std::cout << "  local_ip: " << info.local_ip << "\n";
                    std::cout << "  local_port: " << info.local_port << "\n";
                    //std::cout << "  client_name: " << info.client_name << "\n";
                    std::cout << "  client_connection_mode: " << static_cast<int>(info.connection_mode) << "\n";

                    bool sameLan = IsSameLAN(GetLocalIPAddress(), info.local_ip);
                    ConnectionMode clientMode = info.connection_mode;
                    ConnectionMode selectedMode;

                    if (sameLan)
                    {
                        selectedMode = ConnectionMode::LocalP2P;
                    }
                    else if (clientMode == ConnectionMode::P2P && MyConnectMode == ConnectionMode::P2P) 
                    {
                        selectedMode = ConnectionMode::P2P;
                    }
                    else if (MyConnectMode == ConnectionMode::Relay) {
                        selectedMode = ConnectionMode::Relay;
                    }
                    else {
                        selectedMode = ConnectionMode::Relay;
                    }

                    // --- クライアント情報の保存・更新 ---
                    {
                        std::lock_guard<std::mutex> lock(m_clientsMutex);

                        auto it = std::find_if(m_clients.begin(), m_clients.end(),
                            [&](const ClientInfo& c) {
                                if (sameLan)
                                    return c.localIp == info.local_ip && c.localPort == info.local_port;
                                else
                                    return c.externalIp == info.external_ip && c.externalPort == info.external_port;
                            });

                        if (it != m_clients.end())
                        {
                            it->connectionMode = selectedMode;
                            it->localIp = info.local_ip;
                            it->localPort = info.local_port;
                            it->isSameLAN = sameLan;
                            it->userName = info.client_name;
                        }
                        else
                        {
                            ClientInfo c;
                            RakNet::SystemAddress addr;
                            addr.FromStringExplicitPort(info.external_ip.c_str(), static_cast<unsigned short>(info.external_port));
                            c.address = addr;
                            c.externalIp = info.external_ip;
                            c.externalPort = info.external_port;
                            c.connectionMode = selectedMode;
                            c.localIp = info.local_ip;
                            c.localPort = info.local_port;
                            c.isSameLAN = sameLan;
                            c.userName = info.client_name;
                            // ✅ これを追加！
                            c.lastHeartbeatTime = std::chrono::steady_clock::now();

                            c.guid = m_peer->GetGuidFromSystemAddress(addr);
                            // ★ 接続順IDを割り当て
                            c.clientId = m_nextClientId++;

                            m_clients.push_back(c);
                        }
                    }

                    // --- デバッグ出力 ---
                    std::string modeStr;
                    switch (selectedMode) {
                    case ConnectionMode::LocalP2P: modeStr = "LocalP2P"; break;
                    case ConnectionMode::P2P: modeStr = "P2P"; break;
                    case ConnectionMode::Relay: modeStr = "Relay"; break;
                    }

                    // --- 接続モード別処理 ---
                    switch (selectedMode)
                    {
                    case ConnectionMode::LocalP2P:
                        SetConsoleColor(LIGHT_BLUE);
                        std::cout << "[Host] LocalP2P: " << info.local_ip << ":" << info.local_port << " で直接通信開始\n";
                        SetConsoleColor(WHITE);
                        StartHeartbeatLoop();
                        StartClientMonitorLoop();//クライアント監視開始
                        SetSendOk();
                        break;

                    case ConnectionMode::P2P:
                        SetConsoleColor(LIGHT_BLUE);
                        std::cout << "[Host] P2P: " << info.external_ip << ":" << info.external_port << " でUDPパンチ開始\n";
                        SetConsoleColor(WHITE);
                        StartPunchLoop(info.external_ip, info.external_port, true);
                        break;

                    case ConnectionMode::Relay:
                        SetConsoleColor(LIGHT_BLUE);
                        std::cout << "[Host] Relay: クライアントとリレー通信開始\n";
                        SetConsoleColor(WHITE);
                        StartHeartbeatLoop();
                        StartClientMonitorLoop();//クライアント監視開始
                        SetSendOk();
                        break;
                    }

                    // 初回リレーを送ってきたクライアントへリレー経由で返信
                    std::string replyMsg = "relay_ack_from_host:" + std::to_string(m_nextClientId - 1);
                    RelaySendReplyToServer(info.external_ip, m_userName, replyMsg);

                }

                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }).detach();
}



//uninit
void ChatNetwork::Stop()
{
    m_heartbeatActive = false;
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

//--------------------------------------------------------------------------------------
// ++++++++++++++++++++++++++++++ここから下は送信関係+++++++++++++++++++++++++++++++++++
//--------------------------------------------------------------------------------------

//ホストリレー補助関数(サーバーリレーではない)
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

// ===============================
// サーバーRelay送信関連
// ===============================
bool ChatNetwork::RelaySendDataToServer(
    const std::string& hostIp,
    const std::string& fromName,
    const std::string& payloadType,
    const std::string& payload)
{
    std::string url = "http://210.131.217.223:12345/server_relay.php?action=relay_send"
        + std::string("&host_ip=") + hostIp
        + "&from=" + RoomManager::UrlEncode(RoomManager::CP932ToUTF8(fromName))
        + "&payload_type=" + RoomManager::UrlEncode(payloadType)
        + "&payload=" + RoomManager::UrlEncode(payload);

    std::string response;
    if (!RoomManager::HttpGet(url, response)) {
        std::cerr << "[エラータグ12][Relay送信失敗] type=" << payloadType << std::endl;
        return false;
    }
    else
    {
        std::cout << "[サーバーリレー送信] " << std::endl;
    }
    return true;
}
// ===============================
// サーバーRelay受信関連
// ===============================
void ChatNetwork::StartRelayReceiver(const std::string& hostExternalIp)
{
    if (m_relayReceiverActive) return;
    m_relayReceiverActive = true;

    std::thread([this, hostExternalIp]() {
        while (m_relayReceiverActive)
        {
            std::string url = "http://210.131.217.223:12345/server_relay.php?action=relay_recv&host_ip=" + hostExternalIp;
            std::string response;
            if (RoomManager::HttpGet(url, response))
            {
                try {
                    auto json = nlohmann::json::parse(response);
                    for (auto& item : json)
                    {

                        std::string from = item["user"];
                        std::string payloadType = item.value("payload_type", "");
                        std::string payload = item.value("payload", "");

                        if (payloadType == "chat") {
                            SetConsoleColor(LIGHT_YELLOW);
                            std::cout << "[Relay] " << from << " : " << payload << std::endl;
                            SetConsoleColor(WHITE);

                        }
                        else if (payloadType == "input") {
                            SetConsoleColor(LIGHT_YELLOW);
                            std::cout << "[Relay] 入力データ受信 from: " << from << " payload: " << payload << std::endl;
                            SetConsoleColor(WHITE);
                        }
                        else if (payloadType == "regular") {
                            SetConsoleColor(LIGHT_YELLOW);
                            std::cout << "[Relay] 定期更新データ受信 from: " << from << " payload size: " << payload.size() << std::endl;
                            SetConsoleColor(WHITE);
                        }
                        else if (payloadType == "voice")
                        {//ボイス受信
                            std::string decoded = FromBase64(payload);
                            // decoded にPCMデータが入っているので、
                            // ここでPlayPCM(decoded.data(), decoded.size());

                            SetConsoleColor(LIGHT_YELLOW);
                            std::cout << "[Relay]クライアントへの更新データ受信 " << from << std::endl;
                            SetConsoleColor(WHITE);
                        }
                        else if (payloadType == "heartbeat")
                        {//心拍受信
                            std::string from = item["user"];
                            // 最終受信時刻更新
                            if (m_isHost)
                            {
                                std::lock_guard<std::mutex> lock(m_clientsMutex);
                                auto it = std::find_if(m_clients.begin(), m_clients.end(),
                                    [&](const ClientInfo& c) { return c.userName == from; });
                                if (it != m_clients.end())
                                    it->lastHeartbeatTime = std::chrono::steady_clock::now();
                            }
                            else
                            {
                                // ホストから生存信号
                                m_lastHostHeartbeatTime = std::chrono::steady_clock::now();
                            }
                        }
                        else if (payloadType == "leave")
                        {//退出通知
                            // Relay経由の退席通知
                            SetConsoleColor(RED);
                            std::cout << "[Relay] " << from << " が退出しました。\n";
                            SetConsoleColor(WHITE);

                            if (m_isHost)
                            {
                                // ---------------------------------------
                                // ホスト側：他のクライアントに転送して通知を共有
                                // ---------------------------------------
                                std::lock_guard<std::mutex> lock(m_clientsMutex);

                                // 該当クライアントをリストから削除
                                auto it = std::find_if(m_clients.begin(), m_clients.end(),
                                    [&](const ClientInfo& c) { return c.userName == from; });

                                if (it != m_clients.end())
                                {
                                    std::cout << "[RelayHost] クライアント " << it->userName << " 情報を削除します。\n";
                                    m_clients.erase(it);
                                }

                                // 他の全クライアントに転送（リレー・P2P両方）
                                for (auto& c : m_clients)
                                {
                                    if (c.userName == from) continue;

                                    switch (c.connectionMode)
                                    {
                                    case ConnectionMode::Relay:
                                        RelaySendDataToServer(c.externalIp, "system", "leave", from);
                                        break;

                                    case ConnectionMode::P2P:
                                    case ConnectionMode::LocalP2P:
                                    {
                                        RakNet::BitStream bs;
                                        bs.Write((RakNet::MessageID)ID_LEAVE_NOTIFICATION);
                                        unsigned int len = static_cast<unsigned int>(from.size());
                                        bs.Write(len);
                                        bs.Write(from.c_str(), len);

                                        RakNet::SystemAddress targetAddr;
                                        if (c.connectionMode == ConnectionMode::LocalP2P)
                                            targetAddr.FromStringExplicitPort(c.localIp.c_str(), c.localPort);
                                        else
                                            targetAddr = c.address;

                                        m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED_WITH_ACK_RECEIPT, 2, targetAddr, false);
                                        break;
                                    }
                                    }
                                }
                            }
                            else
                            {
                                // ---------------------------------------
                                // クライアント側
                                // ---------------------------------------
                                if (from == m_hostIp || from == "host")
                                {
                                    // ホストが落ちた場合
                                    SetConsoleColor(RED);
                                    std::cout << "[Info] ホストが退出しました。Enterで最初に戻ります...\n";
                                    SetConsoleColor(WHITE);

                                    m_forceExit = true;
                                    Stop();
                                    m_running = false;
                                }
                                else
                                {
                                    // 他クライアントが退出した場合
                                    SetConsoleColor(RED);
                                    std::cout << "[Info] " << from << " が退出しました。\n";
                                    SetConsoleColor(WHITE);
                                }
                            }
                        }
                        else if (payloadType == "relay_ack")
                        {//初回リレーのお返しがきた
                            SetConsoleColor(LIGHT_YELLOW);
                            std::cout << "[Relay]初回リレーのお返し受信(relay_ack) " << from << " : " << payload << std::endl;
                            SetConsoleColor(WHITE);

                            // ★クライアント番号を抽出
                            int assignedId = -1;
                            if (payload.find("relay_ack_from_host:") == 0) {
                                assignedId = std::stoi(payload.substr(strlen("relay_ack_from_host:")));
                                std::cout << "[Client] 自分のクライアント番号は " << assignedId << " です\n";
                                m_clientId = assignedId;  // ← ChatNetwork に保持させる
                            }

                            StartHeartbeatLoop();//クライアント側
                            StartHostMonitorLoop();//ホストの監視開始
                            SetSendOk();
                        }





                    }
                }
                catch (...) {}
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        }).detach();
}

//初回リレーへのお返しリレー送信
bool ChatNetwork::RelaySendReplyToServer(
    const std::string& targetExternalIp,
    const std::string& fromName,
    const std::string& message)
{
    std::string url =
        "http://210.131.217.223:12345/server_relay.php?action=relay_send"
        + std::string("&host_ip=") + targetExternalIp
        + "&from=" + RoomManager::UrlEncode(RoomManager::CP932ToUTF8(fromName))
        + "&payload_type=relay_ack"
        + "&payload=" + RoomManager::UrlEncode(message);

    std::string response;
    if (!RoomManager::HttpGet(url, response)) {
        std::cerr << "[Relay返信失敗] target=" << targetExternalIp << std::endl;
        return false;
    }
    else {
        std::cout << "[Relay返信送信] target=" << targetExternalIp << "  message=" << message << std::endl;
    }
    return true;
}
// ------------------------------------
// 入力系送信（随時）(クライアント→ホストのみ)
// ------------------------------------
void ChatNetwork::SendGameInput(const AnyTime& inputData)
{
    if (!m_peer) return;

    //リレーならリレーで送信
    if (m_pendingConnectionMode == ConnectionMode::Relay) {
        std::ostringstream ss;
        ss << inputData.playerId << "," << inputData.inputFlags << "," << inputData.timeStamp;
        RelaySendDataToServer(m_hostIp, m_userName, "input", ss.str());
        return;
    }


    // P2P または LocalP2P
    RakNet::BitStream bs;
    bs.Write((RakNet::MessageID)ID_GAME_INPUT);
    bs.Write(inputData.playerId);
    bs.Write(inputData.inputFlags);
    bs.Write(inputData.timeStamp);

    if (m_peer->NumberOfConnections() > 0)
    {
        RakNet::SystemAddress target = m_peer->GetSystemAddressFromIndex(0);
        m_peer->Send(&bs, HIGH_PRIORITY, UNRELIABLE, 0, target, false);
    }
}
// ------------------------------------
// 定期更新送信（ホスト→クライアントのみ）
// ------------------------------------
void ChatNetwork::SendRegularUpdate(const Regular& update)
{
    if (!m_peer || !m_isHost) return;

    std::lock_guard<std::mutex> lock(m_clientsMutex);

    for (auto& c : m_clients)
    {
        if (c.connectionMode == ConnectionMode::Relay)
        {
            std::ostringstream ss;
            ss << update.objectID << "," << update.position.x << "," << update.position.y;
            RelaySendDataToServer(c.externalIp, m_userName, "regular", ss.str());
        }
        else
        {
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

            RakNet::SystemAddress targetAddr;
            if (c.connectionMode == ConnectionMode::LocalP2P)
            {
                targetAddr.FromStringExplicitPort(c.localIp.c_str(), c.localPort);
            }
            else
            {
                targetAddr = c.address;
            }
            m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED_WITH_ACK_RECEIPT, 1, targetAddr, false);
        }
    }
}
// ------------------------------------
// ボイスデータ送信（ホスト or クライアント共通）
// ------------------------------------
void ChatNetwork::SendVoicePacket(const char* audioData, int dataSize)
{
    if (!m_peer || dataSize <= 0) return;

    // クライアント送信は自分のモードで
    if (!m_isHost && m_pendingConnectionMode == ConnectionMode::Relay)
    {
        std::string encoded = ToBase64(std::string(audioData, dataSize));
        RelaySendDataToServer(m_hostIp, m_userName, "voice", encoded);
        return;
    }

    RakNet::BitStream bs;
    bs.Write((RakNet::MessageID)ID_VOICE_PACKET);
    bs.Write(audioData, dataSize);

    if (m_isHost)
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& c : m_clients)
        {
            switch (c.connectionMode)
            {
            case ConnectionMode::Relay:
            {
                std::string encoded = ToBase64(std::string(audioData, dataSize));
                RelaySendDataToServer(c.externalIp, m_userName, "voice", encoded);
                break;
            }
            case ConnectionMode::P2P:
            case ConnectionMode::LocalP2P:
            {
                RakNet::SystemAddress targetAddr;
                if (c.connectionMode == ConnectionMode::LocalP2P)
                {
                    targetAddr.FromStringExplicitPort(c.localIp.c_str(), c.localPort);
                }
                else
                {
                    targetAddr = c.address;
                }
                m_peer->Send(&bs, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 3, targetAddr, false);
                break;
            }
            }
        }
    }
    else
    {
        // クライアント P2P 送信
        if (m_peer->NumberOfConnections() > 0)
        {
            RakNet::SystemAddress target = m_peer->GetSystemAddressFromIndex(0);
            m_peer->Send(&bs, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 3, target, false);
        }
    }
}
// ------------------------------------
// チャット送信（ホスト or クライアント共通）
// ------------------------------------
void ChatNetwork::SendMessage(const std::string& message)
{
    // 送信用ビットストリームを作成
    RakNet::BitStream bs;
    bs.Write((RakNet::MessageID)ID_GAME_MESSAGE);
    std::string senderName = m_userName.empty() ? "匿名" : m_userName;
    std::string payload = senderName + "::" + message;
    unsigned int len = static_cast<unsigned int>(payload.size());
    bs.Write(len);
    bs.Write(payload.c_str(), len);

    if (m_isHost)
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);

        for (auto& c : m_clients)
        {
            std::cout << "[Host->Client送信] 宛先GUID: " << c.guid.ToString() << std::endl;

            switch (c.connectionMode)
            {
            case ConnectionMode::Relay:
                RelaySendDataToServer(c.externalIp, m_userName, "chat", message);
                break;

            case ConnectionMode::P2P:
            case ConnectionMode::LocalP2P:
            {
                // LocalP2P は GUID を無視して explicit に SystemAddress を作る
                RakNet::SystemAddress targetAddr;
                if (c.connectionMode == ConnectionMode::LocalP2P)
                {
                    targetAddr.FromStringExplicitPort(c.localIp.c_str(), c.localPort);
                }
                else
                {
                    targetAddr = c.address;
                }
                m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED_WITH_ACK_RECEIPT, 2, targetAddr, false);
                break;
            }
            }
        }
    }
    else // クライアント側
    {
        if (m_pendingConnectionMode == ConnectionMode::Relay)
        {
            RelaySendDataToServer(m_hostIp, m_userName, "chat", message);
        }
        else // LocalP2P または P2P
        {
            if (m_peer->NumberOfConnections() > 0)
            {
                RakNet::SystemAddress target = m_peer->GetSystemAddressFromIndex(0);
                m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED_WITH_ACK_RECEIPT, 2, target, false);
            }
        }
    }
}

//退出通知送信
void ChatNetwork::SendExit()
{
    // ------------------------------
    // 共通ビットストリーム作成
    // ------------------------------
    RakNet::BitStream bs;
    bs.Write((RakNet::MessageID)ID_LEAVE_NOTIFICATION);

    std::string senderName = m_userName.empty() ? "匿名" : m_userName;
    unsigned int len = static_cast<unsigned int>(senderName.size());
    bs.Write(len);
    bs.Write(senderName.c_str(), len);

    if (m_isHost)
    {
        // ==========================
        // ホスト → 全員に退出通知
        // ==========================
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& c : m_clients)
        {
            std::cout << "[Host->Client退出通知] 宛先GUID: " << c.guid.ToString() << std::endl;

            switch (c.connectionMode)
            {
            case ConnectionMode::Relay:
                RelaySendDataToServer(c.externalIp, m_userName, "leave", "host_exit");
                break;

            case ConnectionMode::P2P:
            case ConnectionMode::LocalP2P:
            {
                RakNet::SystemAddress targetAddr;
                if (c.connectionMode == ConnectionMode::LocalP2P)
                    targetAddr.FromStringExplicitPort(c.localIp.c_str(), c.localPort);
                else
                    targetAddr = c.address;

                m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED_WITH_ACK_RECEIPT, 2, targetAddr, false);
                break;
            }
            }
        }

        // 自身の終了処理
        std::cout << "[Host] 自身のセッションを終了します。\n";

        m_forceExit = true;
        Stop();  // クライアントは最初に戻る
        m_running = false;
    }
    else
    {
        // ==========================
        // クライアント → ホストのみ
        // ==========================
        std::cout << "[Client] ホストに退出通知を送信\n";

        if (m_pendingConnectionMode == ConnectionMode::Relay)
        {
            RelaySendDataToServer(m_hostIp, m_userName, "leave", "client_exit");
        }
        else
        {
            if (m_peer->NumberOfConnections() > 0)
            {
                RakNet::SystemAddress target = m_peer->GetSystemAddressFromIndex(0);
                m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED_WITH_ACK_RECEIPT, 2, target, false);
            }
        }

        // 自身の終了処理
        m_forceExit = true;
        Stop();  // クライアントは最初に戻る
        m_running = false;
    }
}



//-----------------------------[相手の生存確認まわり]-----------------------------------------
//心拍送信ループ
void ChatNetwork::StartHeartbeatLoop()
{
    m_heartbeatActive = true;
    std::thread([this]() {
        while (m_heartbeatActive && m_running)
        {
            SendHeartbeat();
            std::this_thread::sleep_for(std::chrono::seconds(3)); // 3秒ごとに送信
        }
        }).detach();
}

//心拍送信
void ChatNetwork::SendHeartbeat()
{
    if (!m_running) return;

    if (m_isHost)
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& c : m_clients)
        {
            switch (c.connectionMode)
            {
            case ConnectionMode::Relay:
                RelaySendDataToServer(c.externalIp, "system", "heartbeat", "alive");
                break;

            case ConnectionMode::P2P:
            case ConnectionMode::LocalP2P:
            {
                RakNet::BitStream bs;
                bs.Write((RakNet::MessageID)ID_HEARTBEAT);
                std::string msg = "alive";
                unsigned int len = static_cast<unsigned int>(msg.size());
                bs.Write(len);
                bs.Write(msg.c_str(), len);

                RakNet::SystemAddress targetAddr;
                if (c.connectionMode == ConnectionMode::LocalP2P)
                    targetAddr.FromStringExplicitPort(c.localIp.c_str(), c.localPort);
                else
                    targetAddr = c.address;

                m_peer->Send(&bs, LOW_PRIORITY, RELIABLE_ORDERED_WITH_ACK_RECEIPT, 0, targetAddr, false);
                break;
            }
            }
        }
    }
    else
    {
        // クライアント -> ホスト
        if (m_pendingConnectionMode == ConnectionMode::Relay)
            RelaySendDataToServer(m_hostIp, "system", "heartbeat", "alive");
        else
        {
            RakNet::BitStream bs;
            bs.Write((RakNet::MessageID)ID_HEARTBEAT);
            std::string msg = "alive";
            unsigned int len = static_cast<unsigned int>(msg.size());
            bs.Write(len);
            bs.Write(msg.c_str(), len);

            RakNet::SystemAddress target = m_peer->GetSystemAddressFromIndex(0);
            m_peer->Send(&bs, LOW_PRIORITY, RELIABLE_ORDERED_WITH_ACK_RECEIPT, 0, target, false);
        }
    }
}

//クライアント監視スレッド
void ChatNetwork::StartClientMonitorLoop()
{
    if (!m_isHost || m_clientMonitorActive) return;
    m_clientMonitorActive = true;

    m_clientMonitorThread = std::thread([this]()     
    {
        while (m_clientMonitorActive && m_running)
        {
            {
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                auto now = std::chrono::steady_clock::now();

                for (auto it = m_clients.begin(); it != m_clients.end();)
                {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->lastHeartbeatTime);
                    if (elapsed > m_heartbeatTimeout)
                    {
                        SetConsoleColor(RED);
                        std::cout << "[Monitor] クライアント " << it->userName << " の心拍が停止 (" << elapsed.count() << "s)。退出扱いにします。\n";
                        SetConsoleColor(WHITE);

                        // 他のクライアントに退席通知を送る
                        for (auto& c : m_clients)
                        {
                            if (c.guid == it->guid) continue;

                            switch (c.connectionMode)
                            {
                            case ConnectionMode::Relay:
                                RelaySendDataToServer(c.externalIp, "system", "leave", it->userName);
                                break;
                            case ConnectionMode::P2P:
                            case ConnectionMode::LocalP2P:
                            {
                                RakNet::BitStream bs;
                                bs.Write((RakNet::MessageID)ID_LEAVE_NOTIFICATION);
                                unsigned int len = static_cast<unsigned int>(it->userName.size());
                                bs.Write(len);
                                bs.Write(it->userName.c_str(), len);

                                RakNet::SystemAddress targetAddr;
                                if (c.connectionMode == ConnectionMode::LocalP2P)
                                    targetAddr.FromStringExplicitPort(c.localIp.c_str(), c.localPort);
                                else
                                    targetAddr = c.address;

                                m_peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED_WITH_ACK_RECEIPT, 0, targetAddr, false);
                                break;
                            }
                            }
                        }

                        it = m_clients.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    });
}

//ホスト監視スレッド
void ChatNetwork::StartHostMonitorLoop()
{
    if (m_isHost || m_hostMonitorActive) return;
    m_hostMonitorActive = true;

    m_hostMonitorThread = std::thread([this]() 
    {
        while (m_hostMonitorActive && m_running)
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastHostHeartbeatTime);

            if (elapsed > m_heartbeatTimeout)
            {
                SetConsoleColor(RED);
                std::cout << "[Monitor] ホストから " << elapsed.count() << " 秒間応答がありません。切断扱いにします。\n";
                SetConsoleColor(WHITE);

                m_forceExit = true;
                Stop();
                m_running = false;
                break;
            }

            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    });
}
//-----------------------------[相手の生存確認まわりの終端]-----------------------------------------


//通信可能判定
void ChatNetwork::SetSendOk()
{
    std::lock_guard<std::mutex> lk(m_canSendMutex);
    m_canSend = true;
    
    StopPunchLoop();
    SetConsoleColor(LIGHT_BLUE);
    std::cout << "\n++++++++++++++チャット可能+++++++++++++++\n";
    SetConsoleColor(WHITE);
}

//アドレス取得
const RakNet::SystemAddress& ChatNetwork::GetMyAddress() const
{
    return m_peer->GetMyBoundAddress();
}

//ユーザーネーム設定
void ChatNetwork::SetUserName(const std::string& name)
{
    m_userName = name;
}

//ホストのアドレス取得
RakNet::SystemAddress ChatNetwork::GetMyHostAddress() const
{
    if (!m_isHost && m_peer->NumberOfConnections() > 0)
    {
        return m_peer->GetSystemAddressFromIndex(0);
    }
    return RakNet::UNASSIGNED_SYSTEM_ADDRESS;
}


// -----------------------------
// Base64変換周りその１
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
// -----------------------------
// Base64変換周りその２
// -----------------------------
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



