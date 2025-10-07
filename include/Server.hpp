#ifndef SERVER_HPP
#define SERVER_HPP

#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <fcntl.h>
#include <cerrno>
#include <vector>

#include "ClientInfo.hpp"

#define MAX_CLIENTS 100

// サーバー全体を管理するクラス
class Server {
private:
    // -----------------------------
    // メンバ変数
    // -----------------------------
    int serverFd;                 // listen用ソケット
    pollfd fds[MAX_CLIENTS];      // クライアントFD監視配列
    int nfds;                     // fdsの有効数
    int port;                     // 待ち受けポート番号

    std::map<int, ClientInfo> clients; // fd -> ClientInfo 対応表

    // -----------------------------
    // 初期化系
    // -----------------------------
    bool createSocket();
    bool bindAndListen();

    // -----------------------------
    // 接続処理
    // -----------------------------
    void handleNewConnection();
    int acceptClient();                  // accept + nonblocking設定
    void handleDisconnect(int fd, int index, int bytes);
    void handleConnectionClose(int fd);

    // -----------------------------
    // クライアント受信処理
    // -----------------------------
    void handleClient(int index);
    std::string extractNextRequest(std::string &recvBuffer);

    // -----------------------------
    // クライアント送信処理
    // -----------------------------
    void handleClientSend(int index);
    void queueSend(int fd, const std::string &data);

    int findIndexByFd(int fd);

public:
    // -----------------------------
    // コンストラクタ / デストラクタ
    // -----------------------------
    Server(int port);
    ~Server();

    // -----------------------------
    // 初期化 / メインループ
    // -----------------------------
    bool init();
    void run();

    int getServerFd() const;
    std::vector<int> getClientFds() const;  

    // ServerManager から呼ばれる安全な公開インターフェース
    void onPollEvent(int fd, short revents);
};

#endif
