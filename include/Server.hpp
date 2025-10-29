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
#include <fstream>
#include <sstream>

#include "ClientInfo.hpp"
#include "RequestParser.hpp"
#include "ConfigParser.hpp"

#define MAX_CLIENTS 100

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

// multipart/form-data用の簡易構造体
struct FilePart {
    std::string name;        // フィールド名
    std::string filename;    // 元ファイル名
    std::string contentType; // Content-Type
    std::string content;     // 実データ
};

// ==============================
// Serverクラス
// ==============================
class Server {
private:
    // --------------------------------
    // メンバ変数
    // --------------------------------
    int serverFd;                 // listenソケットFD
    pollfd fds[MAX_CLIENTS];      // poll監視配列
    int nfds;                     // 有効なfds数
    ServerConfig cfg;             // サーバ設定全体
    std::string host;             // ホストアドレス
    std::string root;             // ドキュメントルート
    size_t clientMaxBodySize;     // 最大リクエストボディサイズ
    std::map<int, ClientInfo> clients; // クライアント管理マップ

    // Locationマッチ結果構造体
    struct LocationMatch {
        const ServerConfig::Location *loc;
        std::string path; // cfg.location のキー（例: "/delete/"）
        LocationMatch() : loc(NULL), path() {}
    };

    // CGIプロセス管理構造体
    struct CgiProcess {
        pid_t pid;
        int inFd;
        int outFd;
        int clientFd;
        Request req;
        std::string buffer;
        int elapsedLoops;
        bool headerSent;
        CgiProcess()
            : pid(-1), inFd(-1), outFd(-1), clientFd(-1),
              buffer(""), elapsedLoops(0), headerSent(false) {}
    };

    std::map<int, CgiProcess> cgiMap; // key: outFd, value: CGIプロセス情報

    // --------------------------------
    // 初期化・ソケット関連
    // --------------------------------
    bool createSocket();
    bool bindAndListen();

    // --------------------------------
    // 接続処理
    // --------------------------------
    void handleNewConnection();
    int acceptClient();
    LocationMatch getLocationForUri(const std::string &uri) const;

    // --------------------------------
    // リクエスト受信処理
    // --------------------------------
    void handleClient(int index);
    std::string extractNextRequest(std::string &recvBuffer, Request &currentRequest);

    // --------------------------------
    // レスポンス送信処理
    // --------------------------------
    void handleClientSend(int index);
    void queueSend(int fd, const std::string &data);
    void queueSendChunk(int fd, const std::string &data);
    void handleConnectionClose(int fd);
    void handleDisconnect(int fd, int index, int bytes);
    int findIndexByFd(int fd);

    // --------------------------------
    // Method制御
    // --------------------------------
    bool isMethodAllowed(const std::string &method, const ServerConfig::Location *loc) const;
    std::string buildAllowHeader(const ServerConfig::Location *loc) const;

    // --------------------------------
    // POST関連
    // --------------------------------
    void handlePost(int clientFd, const Request &req, const ServerConfig::Location *loc);
    void handleUrlEncodedForm(int clientFd, const Request &req, const ServerConfig::Location *loc);
    void handleMultipartForm(int clientFd, const Request &req, const ServerConfig::Location *loc);
    std::map<std::string, std::string> parseUrlEncoded(const std::string &body);
    std::vector<FilePart> parseMultipart(const std::string &contentType, const std::string &body);
    std::string sanitizeFileName(const std::string &filename);
    void sendPayloadTooLarge(int fd);
    std::string urlDecode(const std::string &s);
    std::string joinMethods(const std::vector<std::string> &methods) const;

    // --------------------------------
    // CGI関連
    // --------------------------------
    bool isCgiRequest(const Request &req);
    void startCgiProcess(int clientFd, const Request &req);
    void handleCgiOutput(int outFd);
    void sendInternalServerError(int clientFd);
    void sendGatewayTimeout(int clientFd);
    void checkCgiTimeouts(int maxLoops);

public:
    // --------------------------------
    // コンストラクタ / デストラクタ
    // --------------------------------
    Server(const ServerConfig &config);
    ~Server();

    // --------------------------------
    // 初期化・メインループ関連
    // --------------------------------
    bool init();
    int getServerFd() const;
    std::vector<int> getClientFds() const;
    void onPollEvent(int fd, short revents);
};

#endif // SERVER_HPP