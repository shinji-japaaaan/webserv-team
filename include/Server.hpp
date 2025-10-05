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

#define MAX_CLIENTS 100

// クライアント情報構造体
struct ClientInfo {
    std::string recvBuffer;    // 受信バッファ
    std::string sendBuffer;    // ★ Day19-20追加: 送信バッファ
    bool requestComplete;      // リクエスト受信完了フラグ
};

// サーバー全体を管理するクラス
class Server {
private:
    int serverFd;             // listen用ソケット
    pollfd fds[MAX_CLIENTS];  // クライアントFD監視配列
    int nfds;                 // fdsの有効数
    int port;                 // 待ち受けポート番号

    std::map<int, ClientInfo> clients; // fd -> ClientInfo 対応表
    void handleNewConnection();
    void handleClient(int index);
    // ★ Day19-20追加: クライアントに送信するデータをバッファに積む
    void queueSend(int fd, const std::string &data);
    std::string extractNextRequest(std::string &recvBuffer);
    void handleDisconnect(int fd, int index, int bytes);

public:
    Server(int port);
    ~Server();

    bool init();
    void run();

};
    

#endif