//------------------------------------------------------------
// @file        main.cpp
// @brief       チャット周り（LAN優先接続対応）
//------------------------------------------------------------
#include "main.h"
#include "stun_client.h"
#include "ip_checker.h"
#include "udp_puncher.h"
#include "nat_checker.h"





//----------------------------------------------
// メイン
//----------------------------------------------
int main()
{
    SetConsoleOutputCP(932);
    SetConsoleCP(932);

    // ★全オブジェクトを main.cpp 側で保持
    std::vector<std::shared_ptr<UDPPuncher>> activePunchers;
    std::vector<std::shared_ptr<ChatSession>> activeChats;

    while (true)
    {
        // 前回の残骸を破棄
        ResetAll(activePunchers, activeChats);
        system("cls");

        std::string ip;
        unsigned short port;

        NATChecker checker;
        std::string type = checker.detectNATType();

        if (GetExternalAddress(ip, port)) {
            SetConsoleColor(GREEN);
            std::cout << "あなたの外部IP: " << ip << std::endl;
            std::cout << "NATマッピングポート: " << port << std::endl;
        }
        else {
            SetConsoleColor(RED);
            std::cout << "STUNサーバーへの接続に失敗しました。" << std::endl;
        }

        std::string localIp = GetLocalIPAddress();
        SetConsoleColor(GREEN);
        std::cout << "あなたのローカルIP: " << localIp << std::endl;
        SetConsoleColor(WHITE);

        if (!CheckServerIP()) return 1;

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

            std::string trimmed = userName;
            trimmed.erase(remove_if(trimmed.begin(), trimmed.end(), isspace), trimmed.end());
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
        if (isHost) {
            connected = HostFlow(roomManager, hostIp, port, userName, ip,
                activePunchers, activeChats);
        }
        else {
            connected = ClientFlow(roomManager, hostIp, userName, ip,
                activePunchers, activeChats);
        }

        SetConsoleColor(WHITE);
    }

    return 0;
}

//----------------------------------------------
// ホスト側フロー（初期化対応版）
//----------------------------------------------
bool HostFlow(RoomManager& roomManager, std::string& hostIp, unsigned short natPort,
    const std::string& userName, const std::string& youExternalIp,
    std::vector<std::shared_ptr<UDPPuncher>>& activePunchers,
    std::vector<std::shared_ptr<ChatSession>>& activeChats)
{
    // --- 以下、以前の HostFlow とほぼ同じ ---
    // 部屋名入力
    std::string roomName;
    SetConsoleColor(YELLOW);
    std::cout << "部屋の名前を入力してください (キャンセルは x): ";
    SetConsoleColor(WHITE);
    std::getline(std::cin, roomName);
    if (roomName == "x" || roomName == "X") return false;

    // 最大人数入力
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

    if (!roomManager.CreateRoom(roomName, hostIp, ipMode, maxPlayers, natPort, localIp, userName)) {
        SetConsoleColor(RED);
        std::cout << "部屋作成に失敗しました。\n";
        SetConsoleColor(WHITE);
        return false;
    }

    SetConsoleColor(BLUE);
    std::cout << "部屋を作成しました！他のプレイヤーの参加を待っています...\n";
    SetConsoleColor(WHITE);

    // リレー受信
    roomManager.StartRelayListener(roomName, [&](const nlohmann::json& client)
        {
            SetConsoleColor(LIGHT_BLUE);
            std::cout << "\n[新規クライアント検出] " << client["user"] << std::endl;
            SetConsoleColor(WHITE);

            std::string ip = client["same_lan"].get<bool>() ? client["local_ip"] : client["external_ip"];
            unsigned short port = client["same_lan"].get<bool>() ? client["local_port"] : client["external_port"];

            auto puncher = std::make_shared<UDPPuncher>();
            puncher->Start(ip, port, true);
            activePunchers.push_back(puncher);
        });

    // 接続待機
    SetConsoleColor(WHITE);
    std::cout << "\n[待機中] クライアントの接続を監視しています...ｘで中断\n";

    while (true) {
        bool anyConnected = false;
        for (auto& p : activePunchers) if (p->IsConnected()) { anyConnected = true; break; }
        if (anyConnected) break;

        if (_kbhit()) {
            char ch = _getch();
            if (ch == 'x' || ch == 'X') {
                SetConsoleColor(RED);
                std::cout << "\n[中断] メインメニューに戻ります。\n";
                SetConsoleColor(WHITE);
                ResetAll(activePunchers, activeChats);
                return false;
            }
        }
    }

    // チャットセッション生成
    std::cout << "\n[INFO] クライアントとの接続が確立しました！チャット開始...\n";

    for (auto& p : activePunchers) {
        if (p->IsConnected()) {
            auto chat = std::make_shared<ChatSession>(p->GetSocket(), p->GetPeerAddr());
            chat->Start();
            activeChats.push_back(chat);
        }
    }

    // チャット監視
    bool bEnd = false;
    while (!bEnd) {
        for (auto& chat : activeChats) if (chat->GetChatState() == true) { bEnd = true; break; }

        if (_kbhit()) {
            char ch = _getch();
            if (ch == 'x' || ch == 'X') { bEnd = true; break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ResetAll(activePunchers, activeChats);
    return true;
}

//----------------------------------------------
// クライアント側フロー（初期化対応版）
//----------------------------------------------
bool ClientFlow(RoomManager& roomManager, std::string& hostIp,
    const std::string& userName, const std::string& youExternalIp,
    std::vector<std::shared_ptr<UDPPuncher>>& activePunchers,
    std::vector<std::shared_ptr<ChatSession>>& activeChats)
{
    IpChecker ipChecker;
    std::string ipMode = ipChecker.CheckServerIP("210.131.217.223", "/check_ip.php", 12345);

    std::map<std::string, nlohmann::json> rooms;
    std::vector<std::string> roomNames;
    std::string myLocalIp = GetLocalIPAddress();

    while (true) {
        system("cls");

        if (roomManager.GetRoomList(rooms) && !rooms.empty()) {
            SetConsoleColor(GREEN);
            std::cout << "現在作成されている部屋一覧:\n";
            SetConsoleColor(WHITE);

            roomNames.clear();
            int idx = 1;
            for (auto& [name, info] : rooms) {
                std::cout << " [" << idx << "] " << UTF8ToCP932(name)
                    << " (ホスト: " << UTF8ToCP932(info.value("host_name", "名無し")) << ")"
                    << " / 外部IP:" << info.value("host_ip", "不明")
                    << " / ローカルIP:" << info.value("local_ip", "不明")
                    << " / port:" << info.value("nat_port", 0) << "\n";
                roomNames.push_back(name);
                ++idx;
            }
        }
        else {
            SetConsoleColor(RED);
            std::cout << "現在作成されている部屋はありません。\n";
            SetConsoleColor(WHITE);
        }

        // 部屋選択
        SetConsoleColor(YELLOW);
        std::cout << "\n接続したい部屋の番号を入力してください / 更新 r / キャンセル x: ";
        SetConsoleColor(WHITE);

        std::string input;
        std::getline(std::cin, input);

        if (input == "x" || input == "X") return false;
        if (input == "r" || input == "R") continue;

        int selectedRoom = -1;
        try { selectedRoom = std::stoi(input); }
        catch (...) { selectedRoom = -1; }
        if (selectedRoom < 1 || selectedRoom >(int)roomNames.size()) {
            SetConsoleColor(RED);
            std::cout << "無効な番号です。\n";
            SetConsoleColor(WHITE);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        auto& info = rooms[roomNames[selectedRoom - 1]];
        std::string hostExtIp = info.value("host_ip", "");
        std::string hostLocalIp = info.value("local_ip", "");
        unsigned short hostNatPort = (unsigned short)info.value("nat_port", 12345);

        bool sameLAN = IsSameLAN(myLocalIp, hostLocalIp);
        hostIp = sameLAN ? hostLocalIp : hostExtIp;
        if (sameLAN) hostNatPort = 12345;

        std::string myExtIp;
        unsigned short myExtPort = 0;
        if (!GetExternalAddress(myExtIp, myExtPort)) { myExtIp = youExternalIp; myExtPort = 12345; }

        roomManager.SendRelayInfo(roomNames[selectedRoom - 1], userName,
            myExtIp, myExtPort, myLocalIp, 12345, sameLAN);

        // UDPパンチング
        auto puncher = std::make_shared<UDPPuncher>();
        activePunchers.push_back(puncher);
        std::thread punchThread([&]() { puncher->Start(hostIp, hostNatPort, false); });

        // 接続確立待機
        while (!puncher->IsConnected()) {
            if (_kbhit()) {
                char ch = _getch();
                if (ch == 'x' || ch == 'X') {
                    SetConsoleColor(RED);
                    std::cout << "\n[中断] メインメニューに戻ります。\n";
                    SetConsoleColor(WHITE);

                    puncher->Stop();       // running = false にしてループ終了
                    punchThread.join();    // スレッド終了待機
                    ResetAll(activePunchers, activeChats);
                    return false;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        punchThread.join();

        // チャット生成
        auto chat = std::make_shared<ChatSession>(puncher->GetSocket(), puncher->GetPeerAddr());
        chat->Start();
        activeChats.push_back(chat);

        // チャット監視
        bool bEnd = false;
        while (!bEnd) {
            for (auto& c : activeChats) if (c->GetChatState()) { bEnd = true; break; }
            if (_kbhit()) { char ch = _getch(); if (ch == 'x' || ch == 'X') { bEnd = true; break; } }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        ResetAll(activePunchers, activeChats);
        return true;
    }
}


//----------------------------------------------
// サーバー接続チェック
//----------------------------------------------
bool CheckServerIP()
{
    IpChecker ipChecker;
    SetConsoleColor(GRAY);
    std::cout << "サーバーにアクセスして IPv4/IPv6 判定中..." << std::endl;
    SetConsoleColor(WHITE);
    std::string result = ipChecker.CheckServerIP("210.131.217.223", "/check_ip.php", 12345);

    if (result == "NONE") {

        //表示物色変更
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleColor(RED);
        std::cout << "? サーバーにアクセスできませんでした。\n";
        SetConsoleColor(WHITE);
        return false;
    }

    SetConsoleColor(BLUE);
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


//----------------------------------------------
// 全オブジェクト初期化関数
//----------------------------------------------
void ResetAll(std::vector<std::shared_ptr<UDPPuncher>>& punchers,
    std::vector<std::shared_ptr<ChatSession>>& chats)
{
    for (auto& chat : chats) chat->Stop();
    chats.clear();

    for (auto& punch : punchers) punch->Stop();
    punchers.clear();
}