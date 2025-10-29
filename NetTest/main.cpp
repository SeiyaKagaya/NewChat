//------------------------------------------------------------
// @file        main.cpp
// @brief       チャット周り（LAN優先接続対応）
//------------------------------------------------------------
#include "main.h"
#include "stun_client.h"
#include "ip_checker.h"
#include "nat_checker.h"




//----------------------------------------------
// メイン
//----------------------------------------------
int main()
{
    SetConsoleOutputCP(932);
    SetConsoleCP(932);

    while (true)
    {
        system("cls");

        std::string ip;
        unsigned short port;
        
        NATChecker checker;
        std::string natType = checker.detectNATType();
        ConnectionMode connectionMode = checker.decideConnectionMode(natType);

        // 表示
        SetConsoleColor(LIGHT_CYAN);
        std::cout << "\n[通信方式 仮決定]: ";
        if (connectionMode == ConnectionMode::P2P)
        {
            std::cout << "P2P通信モード\n";
        }
        else
        {
            std::cout << "Relay通信モード\n";
        }
        SetConsoleColor(WHITE);


        if (GetExternalAddress(ip, port)) {
            std::cout << "あなたの外部IP: " << ip << std::endl;
            std::cout << "NATマッピングポート: " << port << std::endl;
        }
        else {
            SetConsoleColor(RED);
            std::cout << "STUNサーバーへの接続に失敗しました。" << std::endl;
            SetConsoleColor(WHITE);
        }

        std::string localIp = GetLocalIPAddress();
        std::cout << "あなたのローカルIP: " << localIp << std::endl;

        if (!CheckServerIP()) return 1;

        ChatNetwork chatNetwork;
        RoomManager roomManager("http://210.131.217.223:12345");
        std::string hostIp;
        bool isHost = false;

        //---------------------------------------
        // ユーザー名入力（空白・空行禁止）
        //---------------------------------------
        std::string userName;
        while (true) {
            SetConsoleColor(YELLOW);
            std::cout << "あなたのユーザー名を入力してください: ";
            SetConsoleColor(WHITE);

            std::getline(std::cin, userName);

            // 半角スペースと制御文字を削除（マルチバイト安全）
            std::string trimmed = userName;
            trimmed.erase(std::remove_if(trimmed.begin(), trimmed.end(),
                [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }),
                trimmed.end());

            if (trimmed.empty()) {
                SetConsoleColor(RED);
                std::cout << "ユーザー名を入力してください。\n";
                SetConsoleColor(WHITE);
                continue;
            }
            break;
        }

        //---------------------------------------
        // ホスト/クライアント選択
        //---------------------------------------

        SetConsoleColor(YELLOW);
        std::cout << "\nあなたはホストですか？ (y/n) / 終了は x: ";
        SetConsoleColor(WHITE);
        char choice;
        std::cin >> choice;
        std::cin.ignore();

        if (choice == 'x' || choice == 'X') {
            SetConsoleColor(RED);
            std::cout << "終了します。\n";
            SetConsoleColor(WHITE);
            break;
        }

        isHost = (choice == 'y' || choice == 'Y');

        bool connected = false;
        chatNetwork.SetUserName(userName);

        if (isHost) {
            connected = HostFlow(chatNetwork, roomManager, hostIp, port, userName, ip, connectionMode);
        }
        else {
            connected = ClientFlow(chatNetwork, roomManager, hostIp, userName, ip, connectionMode);
        }

        if (!connected) {
            // 念のためネットワークを安全に止めてからループ継続
            chatNetwork.Stop();
            continue;
        }



        ChatLoop(chatNetwork);
       }

    return 0;
}




//----------------------------------------------
// ホスト側フロー
//----------------------------------------------
bool HostFlow(ChatNetwork& chatNetwork, RoomManager& roomManager, std::string& hostIp, unsigned short natPort, const std::string& userName, const std::string& youExternalIp,ConnectionMode mode)
{


    std::string roomName;
    SetConsoleColor(YELLOW);
    std::cout << "部屋の名前を入力してください (キャンセルは x): ";
    SetConsoleColor(WHITE);
    std::getline(std::cin, roomName);

    if (roomName == "x" || roomName == "X") return false;

    int maxPlayers = 0;
    while (true) {
        SetConsoleColor(YELLOW);
        std::cout << "最大人数を入力してください (2から30): ";
        SetConsoleColor(WHITE);
        std::string input;

        std::getline(std::cin, input);

        try { maxPlayers = std::stoi(input); }
        catch (...) { maxPlayers = 0; }
        if (maxPlayers >= 2 && maxPlayers <= 30) break;
        SetConsoleColor(RED);
        std::cout << "無効な人数です。\n";
        SetConsoleColor(WHITE);
    }

    IpChecker ipChecker;
    std::string ipMode = ipChecker.CheckServerIP("210.131.217.223", "/check_ip.php", 12345);
    std::string localIp = GetLocalIPAddress();

    // 部屋情報にユーザー名を追加して送信
    if (!roomManager.CreateRoom(roomName, hostIp, ipMode, maxPlayers, natPort, localIp, userName, mode))
    {
        SetConsoleColor(RED);
        std::cout << "部屋作成に失敗しました。\n";
        SetConsoleColor(WHITE);
        return false;
    }

    std::cout << "部屋を作成しました！\n";

    if (!chatNetwork.Init(true, 12345, "", ipMode, roomManager, youExternalIp, mode)) {
    
        SetConsoleColor(RED);
        std::cout << "チャット初期化失敗\n";
        SetConsoleColor(WHITE);
        return false;
    }

    return true;
}

//----------------------------------------------
// クライアント側フロー
//----------------------------------------------
bool ClientFlow(ChatNetwork& chatNetwork, RoomManager& roomManager, std::string& hostIp, const std::string& userName, const std::string& youExternalIp, ConnectionMode mode)
{

    IpChecker ipChecker;
    std::string ipMode = ipChecker.CheckServerIP("210.131.217.223", "/check_ip.php", 12345);


    if (!chatNetwork.Init(false, 12345, "", ipMode, roomManager, youExternalIp, mode)) {
        SetConsoleColor(RED);
        std::cout << "初期化失敗\n";
        SetConsoleColor(WHITE);
        return false;
    }

    std::map<std::string, nlohmann::json> rooms;
    std::vector<std::string> roomNames;
    int selectedRoom = -1;
    std::string myLocalIp = GetLocalIPAddress();

    while (true)
    {
        system("cls");

        if (roomManager.GetRoomList(rooms) && !rooms.empty())
        {
            std::cout << "現在作成されている部屋一覧:\n";

            roomNames.clear();
            int idx = 1;
            for (auto& [name, info] : rooms)
            {
                std::string ip = info.value("host_ip", "不明");
                std::string localIp = info.value("local_ip", "不明");
                int natPort = info.value("nat_port", 0);
                std::string hostName = info.value("host_name", "名無し"); // ←追加
                std::string connMode = info.value("connection_mode", "P2P"); // ★追加

                std::cout << " [" << idx << "] " << UTF8ToCP932(name)
                    << " (ホスト: " << UTF8ToCP932(hostName) << ")"
                    << " / 外部IP:" << ip
                    << " / 通信方式:" << connMode // ★追加
                    << " / port:" << natPort << "\n";

                roomNames.push_back(name);
                ++idx;
            }
        }
        else
        {
            SetConsoleColor(RED);
            std::cout << "現在作成されている部屋はありません。\n";
            SetConsoleColor(WHITE);
        }

        SetConsoleColor(YELLOW);
        std::cout << "\n接続したい部屋の番号を入力してください / 更新 r / キャンセル x: ";
        SetConsoleColor(WHITE);

        std::string input;
        std::getline(std::cin, input);

        // 💡 入力即チェック
        if (input == "x" || input == "X") {
            SetConsoleColor(RED);
            std::cout << "メインメニューに戻ります。\n";
            SetConsoleColor(WHITE);
            // 入力ストリームのエラー状態をクリアして安全に抜ける
            if (std::cin.fail()) std::cin.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // chatNetworkを安全に停止してから抜ける
            chatNetwork.Stop();

            return false;
        }

        if (input == "r" || input == "R") continue;

        int selectedRoom = -1;
        try { selectedRoom = std::stoi(input); }
        catch (...) { selectedRoom = -1; }

        if (selectedRoom >= 1 && selectedRoom <= (int)roomNames.size())
        {
            auto& info = rooms[roomNames[selectedRoom - 1]];
            std::string hostExtIp = info.value("host_ip", "");
            std::string hostLocalIp = info.value("local_ip", "");
            int hostNatPortInt = info.value("nat_port", 12345);
            std::string connMode = info.value("connection_mode", "P2P"); // ★追加

            unsigned short hostNatPort = static_cast<unsigned short>(hostNatPortInt);

            bool sameLAN = false; // ホストとの同一LAN判定はここで決める


            ConnectionMode conectModeUse = mode;//最終的に決まった通信方式(とりあえず初期値を自身の仮通信方式)



            // 同一LAN判定
            if (IsSameLAN(myLocalIp, hostLocalIp)) {
            
                SetConsoleColor(RED);
                std::cout << "\n同一LANが検出されました。ローカル接続モードを使用します。\n";
                SetConsoleColor(WHITE);
                hostIp = hostLocalIp;//ローカル化

                // STUN済みNATポートではなくホストの待受ポートを使用
                hostNatPort = 12345;

                sameLAN = true;

                conectModeUse = ConnectionMode::LocalP2P;
            }
            else {
                hostIp = hostExtIp;//外部のまま
                // 外部接続の場合は NATマッピングポートを使用
                hostNatPort = hostNatPort;

                ConnectionMode hostMode = StringToConnectionMode(connMode);

                if (hostMode == ConnectionMode::Relay)
                {//ホストがリレー方式
                    conectModeUse = ConnectionMode::Relay;//ホストに合わせる
                }
                else
                {//ホストがP2P方式
                    conectModeUse = mode;//自身が最初に識別した可能な通信方式(つまりホストがクライアントの通信方式に合わせる)
                }


            }

            std::cout << "接続先IP: " << hostIp << " / port: " << hostNatPort << std::endl;


            std::string myExtIp;
            unsigned short myExtPort = 0;
            if (GetExternalAddress(myExtIp, myExtPort)) {
                
                chatNetwork.SetPendingPunch(
                    myExtIp,        // 外部IP
                    myExtPort,      // 外部ポート
                    myLocalIp,      // ローカルIP
                    12345,          // ローカルポート（自分がBindしているポート）
                    sameLAN,        // 同一LANかどうか
                    userName,        //ユーザーネーム
                    conectModeUse
                );
            }

            //初回リレー送信
            roomManager.RelayClientInfo(hostExtIp, userName,
                myExtIp, myExtPort, myLocalIp, 12345, sameLAN);


            //パンチ開始(Client→ホスト)
            if (chatNetwork.ConnectToHost(hostIp, ipMode, hostNatPort, conectModeUse))
            {
                std::cout << "ホストに接続試行中...\n";
                return true;
            }
            else {
                SetConsoleColor(RED);
                std::cout << "接続に失敗しました。\n";
                SetConsoleColor(WHITE);
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
        else
        {
            SetConsoleColor(RED);
            std::cout << "無効な番号です。\n";
            SetConsoleColor(WHITE);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

//----------------------------------------------
// チャットループ
//----------------------------------------------
void ChatLoop(ChatNetwork& chatNetwork)
{

    std::thread recvThread(&ChatNetwork::ReceiveLoop, &chatNetwork);

    std::cout << "チャット開設作業開始。終了するには x を入力してください。\n";
    while (true)
    {
        std::string inputMessage;

        if (chatNetwork.GetForceExit()) { // ★追加
            //std::cout << "\n自動的に最初の画面に戻ります...\n";
            break;
        }


        if (std::getline(std::cin, inputMessage) && !inputMessage.empty())
        {
            if (inputMessage == "x" || inputMessage == "X")
            {
                SetConsoleColor(RED);
                std::cout << "チャットを終了します。\n";
                SetConsoleColor(WHITE);

                // 退出通知を送信
                if (chatNetwork.IsHost()) {
                    chatNetwork.BroadcastLeaveNotification(); // 新規関数
                }
                else {
                    chatNetwork.SendLeaveNotification(); // 新規関数
                }

                chatNetwork.Stop();
                break;
            }
            chatNetwork.SendMessage(inputMessage);
        }


        auto now = std::chrono::steady_clock::now();

        if (!chatNetwork.IsHost())
        {
            auto lastOpt = chatNetwork.GetLastHeartbeatOpt(chatNetwork.GetMyHostAddress());
            if (lastOpt && std::chrono::duration_cast<std::chrono::seconds>(now - *lastOpt) > std::chrono::seconds(10))
            {
                chatNetwork.Stop();
                break;
            }
        }
        else
        {
            chatNetwork.CheckClientTimeouts();
        }



    }

    if (recvThread.joinable())
        recvThread.join();
}
//----------------------------------------------
// サーバー接続チェック
//----------------------------------------------
bool CheckServerIP()
{
    IpChecker ipChecker;
    std::cout << "サーバーにアクセスして IPv4/IPv6 判定中..." << std::endl;
    std::string result = ipChecker.CheckServerIP("210.131.217.223", "/check_ip.php", 12345);

    if (result == "NONE") {
        
        SetConsoleColor(RED);
        std::cout << "? サーバーにアクセスできませんでした。\n";
        SetConsoleColor(WHITE);
        return false;
    }
    SetConsoleColor(GREEN);
    std::cout << "判定結果: " << result << std::endl;
    SetConsoleColor(WHITE);
    return true;
}
//----------------------------------------------
// UTF-8 → CP932 変換
//----------------------------------------------
std::string UTF8ToCP932(const std::string& utf8)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len == 0) return "";
    std::wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wstr.data(), len);

    len = WideCharToMultiByte(932, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len == 0) return "";
    std::string s(len, 0);
    WideCharToMultiByte(932, 0, wstr.c_str(), -1, s.data(), len, nullptr, nullptr);

    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

//----------------------------------------------
// ローカルIP取得
//----------------------------------------------
std::string GetLocalIPAddress()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return "unknown";

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
        WSACleanup();
        return "unknown";
    }

    addrinfo hints = {}, * res = nullptr;
    hints.ai_family = AF_INET; // IPv4 のみ
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostname, nullptr, &hints, &res) != 0) {
        WSACleanup();
        return "unknown";
    }

    std::string localIp = "unknown";
    for (addrinfo* ptr = res; ptr != nullptr; ptr = ptr->ai_next)
    {
        sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(ptr->ai_addr);
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &(addr->sin_addr), ip, sizeof(ip));
        if (strcmp(ip, "127.0.0.1") != 0) { // ループバックでないものを優先
            localIp = ip;
            break;
        }
    }

    freeaddrinfo(res);
    WSACleanup();
    return localIp;
}


//----------------------------------------------
// 同一LAN判定（サブネット先頭3オクテット比較）
//----------------------------------------------
bool IsSameLAN(const std::string& ip1, const std::string& ip2)
{
    if (ip1.empty() || ip2.empty()) return false;
    int dotCount = 0;
    size_t i = 0;
    for (; i < ip1.size() && dotCount < 3; ++i)
        if (ip1[i] == '.') ++dotCount;
    std::string prefix1 = ip1.substr(0, i);

    dotCount = 0;
    size_t j = 0;
    for (; j < ip2.size() && dotCount < 3; ++j)
        if (ip2[j] == '.') ++dotCount;
    std::string prefix2 = ip2.substr(0, j);

    return prefix1 == prefix2;
}
