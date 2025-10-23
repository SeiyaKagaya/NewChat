//------------------------------------------------------------
// @file        ip_checker.h
// @brief       Networkプロトコルチェック
//------------------------------------------------------------
#ifndef _IP_TEST_H_
#define _IP_TEST_H_

#include <string>

class IpChecker
{
public:

    IpChecker();
    ~IpChecker();

    //----------------------------------------------
    // サーバーに接続して IPv4/IPv6 の到達可否を取得
    // host: サーバーのホスト名/IP
    // path: HTTP GET のパス
    // port: ポート番号
    // 戻り値: "OK4", "OK6", "BOTH", "NONE"
    //----------------------------------------------
    std::string CheckServerIP(const std::string& host, const std::string& path, unsigned short port);

private:
    //----------------------------------------------
    // HTTP GET 実行
    // response: 取得したレスポンスを格納
    // タイムアウト5秒
    //----------------------------------------------
    bool HttpGet(const std::string& host, const std::string& path, unsigned short port, std::string& response);
};

#endif
