#ifndef SERVER_HPP
#define SERVER_HPP

#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>     // ★ Day17-18追加: クライアントごとのバッファ管理に使用
#include <string>

#define MAX_CLIENTS 100

// ★ Day17-18追加: クライアント情報構造体
struct ClientInfo {
    std::string recvBuffer;   // 受信バッファ
    bool requestComplete;     // リクエスト受信完了フラグ
};

// サーバー全体を管理するクラス
class Server {
private:
    int serverFd;             // listen用ソケット
    pollfd fds[MAX_CLIENTS];  // クライアントFD監視配列
    int nfds;                 // fdsの有効数
    int port;                 // 待ち受けポート番号

public:
    Server(int port);   // コンストラクタ（ポート番号設定）
    ~Server();          // デストラクタ（全ソケットを閉じる）

    bool init();        // サーバー初期化（socket/bind/listen）
    void run();         // クライアント接続ループ

private:
    void handleNewConnection(); // 新規クライアント接続処理
    void handleClient(int index); // クライアント受信・送信処理

    // ★ Day17-18追加: fd -> ClientInfo の対応表
    std::map<int, ClientInfo> clients;
};

#endif