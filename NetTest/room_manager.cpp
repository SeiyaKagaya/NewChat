//------------------------------------------------------------
// @file        room_manager.cpp
// @brief       チャット周り（LAN優先接続対応）
//------------------------------------------------------------
#include "room_manager.h"


using json = nlohmann::json;

//----------------------------------------------
// コンストラクタ
//----------------------------------------------
RoomManager::RoomManager(const std::string& url) : serverUrl(url)
{
}

//----------------------------------------------
// UTF-8 → UTF-16
//----------------------------------------------
std::wstring RoomManager::UTF8ToWString(const std::string& utf8)
{
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len == 0) return L"";
    std::wstring wstr(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wstr.data(), len);
    return wstr;
}

bool RoomManager::RelayClientInfo(const std::string& hostExIP,
    const std::string& userName,
    const std::string& externalIp,
    unsigned short externalPort,
    const std::string& localIp,
    unsigned short localPort,
    bool sameLan)
{
    std::string url = serverUrl + "/room_manager.php?action=relay_send"
        + "&host_ip=" + hostExIP      // ★ここを変更
        + "&from=" + UrlEncode(CP932ToUTF8(userName))
        + "&external_ip=" + externalIp
        + "&external_port=" + std::to_string(externalPort)
        + "&local_ip=" + localIp
        + "&local_port=" + std::to_string(localPort)
        + "&same_lan=" + (sameLan ? "true" : "false");

    std::string response;
    if (!HttpGet(url, response)) {
        SetConsoleColor(RED);
        std::cerr << "[Relay送信失敗]" << std::endl;
        SetConsoleColor(WHITE);
        return false;
    }

    SetConsoleColor(GREEN);
    std::cout << "[Relay送信完了] " << response << std::endl;

    std::cout << "[Relay送信URL] " << url << std::endl;

    SetConsoleColor(WHITE);
    return true;
}



std::optional<PendingClientInfo> RoomManager::GetPendingClientInfo(const std::string& hostExternalIp)
{
    std::string oss;
    oss = serverUrl + "/room_manager.php?action=relay_recv"
        + "&host_ip=" + hostExternalIp; // ★ room → host_ip に変更

    //std::cout << "[Relay受信URL] " << oss << std::endl;



    std::string response;
    if (!HttpGet(oss, response) || response.empty() || response == "[]") {
        return std::nullopt;
    }

    try {
        auto clients = nlohmann::json::parse(response);

        if (!clients.is_array() || clients.empty())
            return std::nullopt;

        // 一つ目のクライアントデータを返す
        auto c = clients.front();

        PendingClientInfo info;
        info.external_ip = c["external_ip"].get<std::string>();
        info.external_port = c["external_port"].get<int>();
        info.local_ip = c["local_ip"].get<std::string>();
        info.local_port = c["local_port"].get<int>();
        info.client_name = c["user"].get<std::string>();

        SetConsoleColor(GREEN);
        std::cout << "[Relay] クライアント接続: " << info.client_name
            << " (" << info.external_ip << ":" << info.external_port << ")\n";
        SetConsoleColor(WHITE);
        return info;
    }
    catch (const std::exception& e) {
        std::cerr << "[Relay] JSON parse error: " << e.what() << std::endl;
    }

    return std::nullopt;
}

//std::optional<nlohmann::json> RoomManager::GetPendingRelayClients(const std::string& roomName)
//{
//    std::string relayUrl = serverUrl + "/room_manager.php?action=relay_recv"
//        + "&room=" + UrlEncode(CP932ToUTF8(roomName));
//
//    std::string response;
//    if (!HttpGet(relayUrl, response) || response.empty() || response == "[]") {
//        return std::nullopt; // データなし
//    }
//
//    try {
//        auto clients = nlohmann::json::parse(response);
//        if (!clients.is_array() || clients.empty())
//            return std::nullopt;
//
//        // ログ出力（Bタイプ準拠）
//        std::cout << "\n[Relay受信][クライアント参加通知]\n";
//        for (auto& c : clients) {
//            std::cout << "ユーザー: " << c["user"] << std::endl;
//            std::cout << "外部IP: " << c["external_ip"] << ":" << c["external_port"] << std::endl;
//            std::cout << "ローカルIP: " << c["local_ip"] << ":" << c["local_port"] << std::endl;
//            std::cout << "同一LAN: " << c["same_lan"] << std::endl;
//        }
//
//        return clients; // 呼び出し側が処理を行う
//    }
//    catch (const std::exception& e) {
//        SetConsoleColor(RED);
//        std::cerr << "[Relay受信JSONパース失敗] " << e.what() << std::endl;
//        SetConsoleColor(WHITE);
//    }
//
//    return std::nullopt;
//}



//----------------------------------------------
// 部屋作成
//----------------------------------------------
bool RoomManager::CreateRoom(const std::string& roomName,
    std::string& outHostIp,
    const std::string& ipMode,
    int maxPlayers,
    unsigned short natPort,
    const std::string& localIp,
    const std::string& hostName)
{
    std::string utf8Room = CP932ToUTF8(roomName);
    std::string encodedRoom = UrlEncode(utf8Room);

    std::string utf8HostName = CP932ToUTF8(hostName);   // ←ここを追加
    std::string encodedHostName = UrlEncode(utf8HostName);

    std::string url = serverUrl + "/room_manager.php?action=create"
        + "&room=" + encodedRoom
        + "&max=" + std::to_string(maxPlayers)
        + "&protocol=" + ipMode
        + "&nat_port=" + std::to_string(natPort)
        + "&local_ip=" + localIp
        + "&host_name=" + encodedHostName;

    //std::cout << "[DEBUG] CreateRoom URL: " << url << std::endl;

    std::string response;
    if (!HttpGet(url, response)) {
        SetConsoleColor(RED);
        std::cerr << "[ERROR] HTTP GET 失敗\n";
        SetConsoleColor(WHITE);
        return false;
    }

    //std::cout << "[DEBUG] CreateRoom response: " << response << std::endl;

    try {
        auto jsonRes = nlohmann::json::parse(response);
        if (jsonRes.value("success", false)) {
            outHostIp = jsonRes.value("host_ip", "");
            return true;
        }
    }
    catch (const std::exception& e) {
        SetConsoleColor(RED);
        std::cerr << "[ERROR] JSON parse 失敗: " << e.what() << std::endl;
        SetConsoleColor(WHITE);
        return false;
    }

    return false;
}

//----------------------------------------------
// 部屋一覧取得
//----------------------------------------------
bool RoomManager::GetRoomList(std::map<std::string, json>& outRooms)
{
    std::string cmd = "curl -s \"" + serverUrl + "/room_manager.php?action=list\"";

    std::string result;
    std::array<char, 256> buffer;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
        result += buffer.data();
    pclose(pipe);

    try {
        auto j = json::parse(result);
        outRooms.clear();

        for (auto& [roomName, info] : j.items()) {
            outRooms[roomName] = info;
        }

        return true;
    }
    catch (const std::exception& e) {
        SetConsoleColor(RED);
        std::cerr << "部屋一覧取得エラー: " << e.what() << std::endl;
        std::cerr << "レスポンス: " << result << std::endl;
        SetConsoleColor(WHITE);
    }

    return false;
}

//----------------------------------------------
// WinHTTP GET
//----------------------------------------------
bool RoomManager::HttpGet(const std::string& url, std::string& outResponse)
{
    outResponse.clear();

    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);

    wchar_t hostName[256] = {};
    wchar_t urlPath[1024] = {};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = _countof(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = _countof(urlPath);

    std::wstring wUrl = UTF8ToWString(url);

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
        std::cerr << "[ERROR] WinHttpCrackUrl failed\n";
        return false;
    }

    HINTERNET hSession = WinHttpOpen(L"RoomManager/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession,
        hostName,
        urlComp.nPort,
        0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
        L"GET",
        urlPath,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    bool result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr);

    if (result)
    {
        DWORD dwSize = 0;
        do {
            DWORD downloaded = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;

            char* buffer = new char[dwSize + 1];
            if (WinHttpReadData(hRequest, buffer, dwSize, &downloaded)) {
                buffer[downloaded] = 0;
                outResponse.append(buffer, downloaded);
            }
            delete[] buffer;
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}

//----------------------------------------------
// Shift-JIS → UTF-8
//----------------------------------------------
std::string RoomManager::CP932ToUTF8(const std::string& sjis)
{
    int wlen = MultiByteToWideChar(932, 0, sjis.c_str(), -1, nullptr, 0);
    if (wlen == 0) return "";
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(932, 0, sjis.c_str(), -1, wstr.data(), wlen);

    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len == 0) return "";
    std::string utf8(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, utf8.data(), len, nullptr, nullptr);
    if (!utf8.empty() && utf8.back() == '\0') utf8.pop_back();
    return utf8;
}

//----------------------------------------------
// URLエンコード
//----------------------------------------------
std::string RoomManager::UrlEncode(const std::string& value)
{
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value)
    {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~')
            escaped << c;
        else
            escaped << '%' << std::uppercase << std::setw(2)
            << int((unsigned char)c);
    }
    return escaped.str();
}

//
//bool RoomManager::JoinRoom(const std::string& roomName,
//    const std::string& userName,
//    const std::string& extIp,
//    unsigned short extPort,
//    const std::string& localIp,
//    unsigned short localPort)
//{
//    std::string encodedRoom = UrlEncode(CP932ToUTF8(roomName));
//    std::string encodedUser = UrlEncode(CP932ToUTF8(userName));
//
//    std::string url = serverUrl + "/join_room.php"
//        + "?room=" + encodedRoom
//        + "&user=" + encodedUser
//        + "&ext_ip=" + UrlEncode(extIp)
//        + "&ext_port=" + std::to_string(extPort)
//        + "&local_ip=" + UrlEncode(localIp)
//        + "&local_port=" + std::to_string(localPort);
//
//    std::string response;
//    if (!HttpGet(url, response)) {
//        std::cerr << "[ERROR] JoinRoom HTTP GET failed\n";
//        return false;
//    }
//
//    try {
//        auto jsonRes = nlohmann::json::parse(response);
//        return jsonRes.value("success", false);
//    }
//    catch (...) {
//        std::cerr << "[ERROR] JoinRoom JSON parse failed\n";
//        return false;
//    }
//}
