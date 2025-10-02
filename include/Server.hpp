#ifndef SERVER_HPP
#define SERVER_HPP

#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#define MAX_CLIENTS 100

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
};

#endif