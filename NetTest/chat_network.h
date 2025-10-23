//------------------------------------------------------------
// @file        chat_network.h
// @brief       チャット周り（更新済：パンチ処理＋TCP完遂報告＋LAN判定対応＋生存監視スレッド追加）
//------------------------------------------------------------
#ifndef _CHAT_NETWORK_H_
#define _CHAT_NETWORK_H_

#pragma once

#include "RakNet/Source/RakPeerInterface.h"
#include "RakNet/Source/RakNetTypes.h"
#include "RakNet/Source/BitStream.h"
#include "RakNet/Source/MessageIdentifiers.h"
#include "RakNet/Source/RakSleep.h"
#include "RakNet/Source/RakString.h"
#include "RakNet/Source/NatTypeDetectionClient.h"

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <windows.h>
#include <map>
#include <chrono>
#include <optional>
#include "room_manager.h"


#pragma comment(lib, "RakNetDLL.lib")

// 新しいパケットIDを追加
enum GAME_MESSAGES
{
    ID_GAME_MESSAGE = ID_USER_PACKET_ENUM + 1,
    ID_CLIENT_PROTOCOL,   // クライアントプロトコル通知用
    ID_PUNCH_INFO,        // クライアント -> ホスト : JSON による外部IP/port通知
    ID_PUNCH_PACKET,      // パンチ用パケット（通知／パンチ）
    ID_LEAVE_NOTIFICATION,// クライアント/ホスト退出通知
    ID_HEARTBEAT          // 生存確認用
};

struct ClientInfo {
    RakNet::SystemAddress address;
    std::string protocol;       // "OK4" / "OK6"
    std::string externalIp;
    unsigned short externalPort = 0;
    std::string localIp;        // 新規: LAN内アドレス
    unsigned short localPort = 0; // 新規: LAN内ポート
    bool isSameLAN = false;     // 新規: 同一LAN判定
    std::string userName;       // ←追加 (バイナリ/CP932 でも扱える)
    std::chrono::steady_clock::time_point connectedTime; // ★ 追加
};



class ChatNetwork
{
public:
    ChatNetwork();
    ~ChatNetwork();

    // 初期化
    bool Init(bool host, unsigned short port, const std::string& bindIp, const std::string& protocol, RoomManager& roomManager, const std::string& youExternalIp);

    // ホストへ接続
    bool ConnectToHost(const std::string& hostIp, const std::string& hostProtocol, unsigned short hostPort);

    // メッセージ送信
    void SendMessage(const std::string& message);

    // 受信ループ
    void ReceiveLoop();

    // 自身のアドレス取得
    const RakNet::SystemAddress& GetMyAddress() const;

    // 終了処理
    void Stop();

    // クライアントが STUN で得た外部アドレスをチャットに渡す（キュー格納）
    void SetPendingPunch(const std::string& extIp, unsigned short extPort,
        const std::string& localIp, unsigned short localPort,
        bool sameLAN, const std::string& userName);

    // パンチループ制御
    void StartPunchLoop(const std::string& targetIp, unsigned short targetPort, bool isHostSide);
    void StopPunchLoop();

    // TCP 完遂送信
    void SendPunchDoneTCP(const std::string& targetIp, unsigned short port);

    // 名前格納
    void SetUserName(const std::string& name);

    void SetConsoleColor(WORD color);
    void ResetConsoleColor();

    std::string ToBase64(const std::string& input);
    std::string FromBase64(const std::string& input);

    void SendLeaveNotification();        // クライアント用
    void BroadcastLeaveNotification();   // ホスト用
    bool IsHost() const { return m_isHost; }

    void StartHeartbeat();
    void StopHeartbeat();

    RakNet::SystemAddress GetMyHostAddress() const;
    std::chrono::steady_clock::time_point GetLastHeartbeat(RakNet::SystemAddress addr);
    void CheckClientTimeouts();

    std::optional<std::chrono::steady_clock::time_point> GetLastHeartbeatOpt(RakNet::SystemAddress addr);



    void StartRelayPollThread(RoomManager& roomManager, const std::string& hostExternalIp);

    //void ChatNetwork::StartHostToClientPunch(const PendingClientInfo& info);



    //void StartClientRelayPoll(RoomManager& roomManager, const std::string& roomName,bool isSameLan, const std::string& hostExternalIp);


    // ============================================================
    // ★★★ 新規追加：生存監視スレッド機能（ホスト／クライアント）
    // ============================================================
    void StartClientMonitor();  // ホストがクライアント生存確認
    void StartHostMonitor();    // クライアントがホスト生存確認

    bool GetForceExit() { return m_forceExit; }

    // ChatNetwork.h に追加
    //void StartJoinRequestPoll(RoomManager& roomManager, const std::string& roomName);





private:
    RakNet::RakPeerInterface* m_peer;
    bool m_isHost;
    unsigned short m_port;
    bool m_canSend;

    std::string m_clientProtocol;
    std::string m_hostProtocol;

    std::mutex m_clientsMutex;
    std::vector<ClientInfo> m_clients;
    std::atomic<bool> m_running{ true };

    // パンチ管理
    std::string m_pendingPunchIp;
    unsigned short m_pendingPunchPort = 0;
    std::string m_pendingLocalIp;
    unsigned short m_pendingLocalPort = 0;
    bool m_pendingSameLAN = false;
    std::atomic<bool> m_hasPendingPunch{ false };
    std::mutex m_pendingMutex;
    std::string m_pendingUserName; // ←追加 (raw bytes as provided)

    std::string m_userName; // 自分のユーザー名（クライアント用）

    // 接続先ホスト情報（クライアント時に使用）
    std::string m_hostIp;
    unsigned short m_hostPort = 0;

    // パンチループ実行用
    std::atomic<bool> m_punchLoopActive{ false };
    std::thread m_punchThread;

    // TCP 完遂待受用
    std::atomic<bool> m_tcpWaiterActive{ false };
    std::thread m_tcpWaiterThread;

    // m_canSend の保護
    std::mutex m_canSendMutex;

    // ハートビート管理
    std::atomic<bool> m_heartbeatActive{ false };
    std::thread m_heartbeatThread;
    std::mutex m_heartbeatMutex;
    std::map<RakNet::SystemAddress, std::chrono::steady_clock::time_point> m_lastHeartbeat; // ホスト用
    std::chrono::seconds m_heartbeatTimeout{ 10 };


    // ============================================================
    // ★★★ 新規追加：生存監視スレッド関連メンバ
    // ============================================================
    std::atomic<bool> m_clientMonitorActive{ false };  // ホスト側監視有効
    std::thread m_clientMonitorThread;                 // ホスト→クライアント監視スレッド

    std::atomic<bool> m_hostMonitorActive{ false };    // クライアント側監視有効
    std::thread m_hostMonitorThread;                   // クライアント→ホスト監視スレッド

    std::thread m_receiveThread;

    std::atomic<bool> m_forceExit{ false }; // クライアント強制終了フラグ


    // ChatNetwork クラスのメンバとして追加
    std::thread m_clientRelayPollThread;

    bool running = false;  // スレッド動作用フラグ
};

#endif
