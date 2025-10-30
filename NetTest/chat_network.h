//------------------------------------------------------------
// @file        chat_network.h
// @brief       チャット周り（更新済：パンチ処理＋TCP完遂報告＋LAN判定対応＋生存監視スレッド追加）
//------------------------------------------------------------
#ifndef _CHAT_NETWORK_H_
#define _CHAT_NETWORK_H_

#pragma once

#include "includemanager.h"
#include "room_manager.h"
#include "nat_checker.h"



// 随時
struct AnyTime
{
    int playerId;
    unsigned int inputFlags;
    DWORD timeStamp;
};

// 定期
struct Regular
{
    unsigned int objectID;
    XMFLOAT3 position;
    XMFLOAT4 rotation;
    XMFLOAT3 linerVelocity;
    XMFLOAT3 angularVelocity;
};


// 新しいパケットIDを追加
enum GAME_MESSAGES
{
    ID_GAME_MESSAGE = ID_USER_PACKET_ENUM + 1,
    ID_CLIENT_PROTOCOL,   // クライアントプロトコル通知用
    ID_PUNCH_INFO,        // クライアント -> ホスト : JSON による外部IP/port通知
    ID_PUNCH_PACKET,      // パンチ用パケット（通知／パンチ）
    ID_LEAVE_NOTIFICATION,// クライアント/ホスト退出通知
    ID_HEARTBEAT,          // 生存確認用
    ID_GAME_INPUT,         // 入力系構造体[ゲーム]
    ID_GAME_REGULAR_UPDATE,// 定期更新[ゲーム]
    ID_VOICE_PACKET        // ボイチャ[ゲーム]
};

struct ClientInfo {
    RakNet::SystemAddress address;
    RakNet::RakNetGUID guid;   // ←これを追加！
    std::string protocol;       // "OK4" / "OK6"
    std::string externalIp;
    unsigned short externalPort = 0;
    std::string localIp;        // LAN内アドレス
    unsigned short localPort = 0;
    bool isSameLAN = false;     // LAN内判定
    std::string userName;
    std::chrono::steady_clock::time_point connectedTime;

    ConnectionMode connectionMode = ConnectionMode::Relay; // ★追加
};







// 種別ごとの識別子
enum class RelayType : uint8_t {
    Chat,         // 送信元以外へ転送
    Voice,        // 送信元以外へ転送
    RegularUpdate // 全員（送信元含む）に配信
};

class ChatNetwork
{
public:
    ChatNetwork();
    ~ChatNetwork();

    // 初期化
    bool Init(bool host, unsigned short port, const std::string& bindIp, const std::string& protocol, RoomManager& roomManager, const std::string& youExternalIp, ConnectionMode MyConnectMode);

    // ホストへ接続
    bool ConnectToHost(const std::string& hostIp, const std::string& hostProtocol, unsigned short hostPort, ConnectionMode MyConnectMode);

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
        bool sameLAN, const std::string& userName, ConnectionMode  connectionMode);

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

    //void SendLeaveNotification();        // クライアント用
    //void BroadcastLeaveNotification();   // ホスト用
    bool IsHost() const { return m_isHost; }

    //void StartHeartbeat();
    //void StopHeartbeat();

    RakNet::SystemAddress GetMyHostAddress() const;
    //std::chrono::steady_clock::time_point GetLastHeartbeat(RakNet::SystemAddress addr);
    //void CheckClientTimeouts();

    //std::optional<std::chrono::steady_clock::time_point> GetLastHeartbeatOpt(RakNet::SystemAddress addr);

    void StartRelayPollThread(RoomManager& roomManager, const std::string& hostExternalIp, ConnectionMode MyConnectMode);

    //void StartClientMonitor();  // ホストがクライアント生存確認
    //void StartHostMonitor();    // クライアントがホスト生存確認

    bool GetForceExit() { return m_forceExit; }


    // 入力系送信（随時）
    void SendGameInput(const AnyTime& inputData);

    // 定期更新送信
    void SendRegularUpdate(const Regular& update);
   
    // ボイスデータ送信
    void SendVoicePacket(const char* audioData, int dataSize);

    //スター型P2Pのリレー(サーバーリレーでない。上記３つや上記の受信時など、補助ツールに近い)
    void RelayPacket(RelayType type, const RakNet::SystemAddress& sender, const RakNet::BitStream& data);
   
    //void CheckClientTimeouts();
    //void CheckHostTimeout();


    // ===============================
// Relay通信関連
// ===============================
    bool RelaySendDataToServer(
        const std::string& hostIp,
        const std::string& fromName,
        const std::string& payloadType,
        const std::string& payload);
   

    void StartRelayReceiver(const std::string& hostExternalIp);
    
    //生存確認送信関数
    void SendHeartbeatToClientOrHost(const ClientInfo& info);


    //退出通知送信関数
    void SendLeaveNotificationToClientOrHost(const ClientInfo& info);

    std::mutex& GetClientsMutex() { return m_clientsMutex; }
    std::vector<ClientInfo>& GetClients() { return m_clients; }
    ConnectionMode GetPendingConnectionMode() const { return m_pendingConnectionMode; }

    

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

  std::atomic<bool> m_clientMonitorActive{ false };  // ホスト側監視有効
    std::thread m_clientMonitorThread;                 // ホスト→クライアント監視スレッド

    std::atomic<bool> m_hostMonitorActive{ false };    // クライアント側監視有効
    std::thread m_hostMonitorThread;                   // クライアント→ホスト監視スレッド

    std::thread m_receiveThread;

    std::atomic<bool> m_forceExit{ false }; // クライアント強制終了フラグ


    // ChatNetwork クラスのメンバとして追加
    std::thread m_clientRelayPollThread;

    bool running = false;  // スレッド動作用フラグ

    ConnectionMode m_pendingConnectionMode = ConnectionMode::P2P; // ←追加

        // ==============================
        // Relay関連ステート
        // ==============================
        std::atomic<bool> m_relayReceiverActive = false;   // Relay受信スレッド稼働フラグ
       // ConnectionMode m_myConnectionMode = ConnectionMode::P2P; // 自分の接続モード(初期はP2P)

        // ユーザー名をキーに、最後の受信時刻を管理
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_lastHeartbeatRelay;
};








//| | 特性                                    | 軽さ | 確実性 | 順序保証    | 用途例                       |
//| --------------------------------------- - | ---- | ---- - | --------- - | -----------------------------|
//| **UNRELIABLE * *                          | ◎   | ✕     | ✕          | 座標、移動、カメラ           |
//| **UNRELIABLE_SEQUENCED * *　　　　　　　  | ◎   | ✕     | △(最新のみ)| 移動・向き同期               |
//| **RELIABLE * *                            | ○   | ◎     | ✕          | アイテム取得                 |
//| **RELIABLE_ORDERED * *                    | △   | ◎     | ◎          | 攻撃-イベント-チャット       |
//| **RELIABLE_SEQUENCED * *                  | ○   | ◎     | △（最新）  | HPバーなど上書き型情報       |
//| **RELIABLE_ORDERED_WITH_ACK_RECEIPT * *   | ✕   | ◎◎   | ◎◎        |クエスト進行-システムイベント |



//使用例はmemo!!!!!!!!!!.txtにあり

//送信---UNRELIABLE（軽量・順序保証なし・ロストOK）

//RakNet::BitStream bsOut;
//bsOut.Write((RakNet::MessageID)ID_PLAYER_POSITION);
//bsOut.Write(playerId);
//bsOut.Write(playerPos.x);
//bsOut.Write(playerPos.y);
//bsOut.Write(playerPos.z);
//
//peer->Send(
//    &bsOut,
//    LOW_PRIORITY,
//    UNRELIABLE,
//    0,
//    address,
//    false
//);


//受信
//case ID_PLAYER_POSITION: {
//    RakNet::BitStream bsIn(packet->data, packet->length, false);
//    bsIn.IgnoreBytes(sizeof(RakNet::MessageID));
//    int id; float x, y, z;
//    bsIn.Read(id); bsIn.Read(x); bsIn.Read(y); bsIn.Read(z);
//    UpdatePlayerPosition(id, x, y, z);
//    break;
//}



#endif
