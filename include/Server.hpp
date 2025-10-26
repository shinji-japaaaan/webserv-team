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

// 簡易ファイルパート構造体（multipart の戻り値に使用）
struct FilePart {
    std::string name;     // フォームフィールド名 (例: "file")
    std::string filename; // 元のファイル名 (例: "hello.txt")
    std::string contentType;
    std::string content;  // バイナリ / テキストデータ
};

// サーバー全体を管理するクラス
class Server {
private:
    // -----------------------------
    // メンバ変数
    // -----------------------------
    int serverFd;                 // listen用ソケット
    pollfd fds[MAX_CLIENTS];      // クライアントFD監視配列
    int nfds;                     // fdsの有効数

    ServerConfig cfg;             // サーバー設定
    int port;                     // 待ち受けポート番号
    std::string host;             // 追加: 待ち受けホストアドレス
    std::string root;             // 追加: ドキュメントルート
    std::map<int, std::string> errorPages; // 追加: エラーページ設定
    size_t clientMaxBodySize;     // 追加: クライアント最大ボディサイズ

    std::map<int, ClientInfo> clients; // fd -> ClientInfo 対応表
    
    // -----------------------------
    // ここから追加：CGI対応用
    // -----------------------------
    struct CgiProcess {
        pid_t pid;
        int inFd;   // CGIへの書き込み用
        int outFd;  // CGIからの読み取り用
        int clientFd; // ←追加: このCGIリクエストのクライアントFD
        Request req;
        std::string buffer;          // ←追加: CGI出力を一時的に蓄積
        int elapsedLoops; // poll ループ数タイムアウト用
        bool headerSent;
        CgiProcess()
        : pid(-1), inFd(-1), outFd(-1), clientFd(-1),
          buffer(""), elapsedLoops(0), headerSent(false) {}

    };
    std::map<int, CgiProcess> cgiMap; // key: outFd, value: 管理情報

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
    std::string extractNextRequest(std::string &recvBuffer, Request &currentRequest);

    // -----------------------------
    // クライアント送信処理
    // -----------------------------
    void handleClientSend(int index);
    void queueSend(int fd, const std::string &data);

    int findIndexByFd(int fd);

    // -----------------------------
    // ここから追加：CGI対応用
    // -----------------------------
    bool isCgiRequest(const Request &req);               // CGI判定関数
    void startCgiProcess(int clientFd, const Request &req);          // CGI実行関数
    void handleCgiOutput(int outFd);                     // pollで読み取り可能になったCGI出力を処理
    void sendInternalServerError(int clientFd);

    // -----------------------------
    // POST 関連
    // -----------------------------
    void handlePost(int clientFd, const Request &req);

    // ボディサイズを超えた時のレスポンス送信
    void sendPayloadTooLarge(int fd);

    // Content-Type ごとの処理（宣言）
    void handleUrlEncodedForm(int clientFd, const Request &req);
    void handleMultipartForm(int clientFd, const Request &req);

    // 補助パーサー（宣言）
    std::map<std::string, std::string> parseUrlEncoded(const std::string &body);
    std::vector<FilePart> parseMultipart(const std::string &contentType, const std::string &body);
    void queueSendChunk(int fd, const std::string &data);
    std::string sanitizeFileName(const std::string &filename);

    // URLデコード補助
    std::string urlDecode(const std::string &s);

    void sendGatewayTimeout(int clientFd);
    const ServerConfig::Location* getLocationForUri(const std::string &uri) const;

public:
    // -----------------------------
    // コンストラクタ / デストラクタ
    // -----------------------------
    Server(const ServerConfig &config);
    ~Server();

    // -----------------------------
    // 初期化 / メインループ
    // -----------------------------
    bool init();

    int getServerFd() const;
    std::vector<int> getClientFds() const;

    // ServerManager から呼ばれる安全な公開インターフェース
    void onPollEvent(int fd, short revents);

    std::vector<int> getCgiFds() const;                 // 現在監視中のCGI出力FDリスト
    void checkCgiTimeouts(int maxLoops); // pollループごとに呼ぶ
};

#endif
