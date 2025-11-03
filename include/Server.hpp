#ifndef SERVER_HPP
#define SERVER_HPP

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "CgiProcess.hpp"
#include "ClientInfo.hpp"
#include "ConfigParser.hpp"
#include "RequestParser.hpp"

#define MAX_CLIENTS 100

// サーバー全体を管理するクラス
class Server {
   private:
    // -----------------------------
    // メンバ変数
    // -----------------------------
    ServerConfig cfg;                       // サーバー設定
    int serverFd;                           // listen用ソケット
    pollfd fds[MAX_CLIENTS];                // クライアントFD監視配列
    int nfds;                               // fdsの有効数
    int port;                               // 待ち受けポート番号
    std::string host;                       // 追加: 待ち受けホストアドレス
    std::string root;                       // 追加: ドキュメントルート
    std::map<int, std::string> errorPages;  // 追加: エラーページ設定

    std::map<int, ClientInfo> clients;  // fd -> ClientInfo 対応表

    // Locationマッチ結果構造体
    struct LocationMatch {
        const ServerConfig::Location* loc;
        std::string path;  // cfg.location のキー（例: "/delete/"）
        LocationMatch() : loc(NULL), path() {}
    };
    // CGIプロセス管理構造体
    std::map<int, CgiProcess> cgiMap;  // key: outFd, value: 管理情報

    // -----------------------------
    // 初期化系
    // -----------------------------
    bool createSocket();
    bool bindAndListen();

    // -----------------------------
    // pollイベント処理
    // -----------------------------
    bool isServerSocket(int fd) const;
    bool isCgiInFd(int fd) const;
    bool isCgiStdoutFd(int fd) const;
    bool isClientFd(int fd) const;

    // -----------------------------
    // 接続処理
    // -----------------------------
    void handleNewConnection();
    int acceptClient();  // accept + nonblocking設定
    void handleDisconnect(int fd, int index, int bytes);
    void handleConnectionClose(int fd);

    // -----------------------------
    // クライアント受信処理
    // -----------------------------
    void handleClient(int index);
    std::string extractNextRequest(std::string& recvBuffer,
                                   Request& currentRequest);
    bool isMethodAllowed(const std::string& method,
                         const ServerConfig::Location* loc);
    bool checkMaxBodySize(int fd, int bytes, const ServerConfig::Location* loc);
    bool handleMethodCheck(int fd, Request& req,
                           const ServerConfig::Location* loc, size_t reqSize);
    void processRequest(int fd, Request& req, const ServerConfig::Location* loc,
                        const std::string& locPath, size_t reqSize);
    bool handleRedirect(int fd, const ServerConfig::Location* loc);

    // -----------------------------
    // クライアント送信処理
    // -----------------------------
    void handleClientSend(int fd);
    void queueSend(int fd, const std::string& data);

    int findIndexByFd(int fd);

    // -----------------------------
    // ここから追加：CGI対応用
    // -----------------------------
    bool isCgiRequest(const Request& req);  // CGI判定関数
    void startCgiProcess(int clientFd, const Request& req,
                         const ServerConfig::Location& loc);  // CGI実行関数
    void handleCgiReadable(
        int outFd);  // pollで読み取り可能になったCGI出力を処理
    std::string buildHttpResponseFromCgi(const std::string& cgiOutput);
    void registerCgiProcess(int clientFd, pid_t pid, int outFd,
                            std::map<int, CgiProcess>& cgiMap, pollfd fds[],
                            int& nfds);

    Server::LocationMatch getLocationForUri(const std::string& uri) const;
    void sendGatewayTimeout(int clientFd);

    // -----------------------------
    // ここから追加： POST処理用
    // -----------------------------
    void handlePost(int fd, Request& req, const ServerConfig::Location* loc);
    void handleMultipartForm(int fd, Request& req,
                             const ServerConfig::Location* loc);
    void handleUrlEncodedForm(int fd, Request& req,
                              const ServerConfig::Location* loc);
    void handleChunkedBody(int fd, Request& req,
                           const ServerConfig::Location* loc);

    int findFdByRecvBuffer(const std::string& buffer) const;

   public:
    // -----------------------------
    // コンストラクタ / デストラクタ
    // -----------------------------
    Server(const ServerConfig& cfg);
    ~Server();

    // -----------------------------
    // 初期化 / メインループ
    // -----------------------------
    bool init();

    // -----------------------------
    // pollイベント処理
    // -----------------------------
    void handlePollIn(int fd);
    void handlePollOut(int fd);

    int getServerFd() const;
    const std::map<int, ClientInfo>& getClients() const;
    const std::map<int, CgiProcess>& getCgiMap() const;

    void checkCgiTimeouts(int maxLoops);
    bool hasPendingSend(int fd) const;
};

#endif
