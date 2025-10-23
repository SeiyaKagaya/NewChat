//------------------------------------------------------------
// @file        ip_checker.cpp
// @brief       Networkプロトコルチェッカー
//------------------------------------------------------------
#include "ip_checker.h"

//----------------------------------------------
// コンストラクタ
//----------------------------------------------
IpChecker::IpChecker()
{
}

//----------------------------------------------
// デストラクタ
//----------------------------------------------
IpChecker::~IpChecker()
{
}

//----------------------------------------------
// HTTP GET を実行（タイムアウト 5秒 / エラー表示付き）
// host: サーバー名
// path: パス
// port: ポート番号
// response: 取得したレスポンス文字列
//----------------------------------------------
bool IpChecker::HttpGet(const std::string& host, const std::string& path, unsigned short port, std::string& response)
{
    response.clear();

    HINTERNET hSession = WinHttpOpen(L"CityCleaners HTTP Client/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        std::cerr << "[WinHTTP] WinHttpOpen failed: " << GetLastError() << std::endl;
        return false;
    }

    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

    std::wstring wHost(host.begin(), host.end());
    std::wstring wPath(path.begin(), path.end());

    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), port, 0);
    if (!hConnect) {
        std::cerr << "[WinHTTP] WinHttpConnect failed: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(),
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        std::cerr << "[WinHTTP] WinHttpOpenRequest failed: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, NULL)) {
        std::cerr << "[WinHTTP] WinHttpSendRequest/ReceiveResponse failed: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        char* buffer = new char[bytesAvailable + 1];
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer, bytesAvailable, &bytesRead)) {
            buffer[bytesRead] = 0;
            response += buffer;
        }
        delete[] buffer;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return true;
}

//----------------------------------------------
// サーバーにアクセスして IPv4/IPv6 判定
// host: サーバー名
// path: 判定用スクリプトパス
// port: ポート番号
// 戻り値: "OK4", "OK6", "BOTH", "NONE"
//----------------------------------------------
std::string IpChecker::CheckServerIP(const std::string& host, const std::string& path, unsigned short port)
{
    std::string result;

    if (HttpGet(host, path, port, result)) {
        // 前後の空白・改行除去
        result.erase(result.find_last_not_of(" \r\n") + 1);
        result.erase(0, result.find_first_not_of(" \r\n"));
        return result;
    }

    return "NONE";
}
