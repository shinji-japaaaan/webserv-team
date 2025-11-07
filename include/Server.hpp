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
#include <vector>

#include "ClientInfo.hpp"
#include "RequestParser.hpp"
#include "ConfigParser.hpp"
#include "CgiProcess.hpp"

#define MAX_CLIENTS 100

// サーバー全体を管理するクラス
class Server
{
private:
	// -----------------------------
	// メンバ変数
	// -----------------------------
	ServerConfig cfg;					   // サーバー設定
	int serverFd;						   // listen用ソケット
	pollfd fds[MAX_CLIENTS];			   // クライアントFD監視配列
	int nfds;							   // fdsの有効数
	int port;							   // 待ち受けポート番号
	std::string host;					   // 追加: 待ち受けホストアドレス
	std::string root;					   // 追加: ドキュメントルート
	std::map<int, std::string> errorPages; // 追加: エラーページ設定

	std::map<int, ClientInfo> clients; // fd -> ClientInfo 対応表

	// Locationマッチ結果構造体
	struct LocationMatch
	{
		const ServerConfig::Location *loc;
		std::string path; // cfg.location のキー（例: "/delete/"）
		LocationMatch() : loc(NULL), path() {}
	};

	// -----------------------------
	// ここから追加：CGI対応用
	// -----------------------------
	std::map<int, CgiProcess> cgiMap; // key: outFd, value: 管理情報

	// -----------------------------
	// 初期化系
	// -----------------------------
	bool createSocket();
	bool bindAndListen();
	bool setNonBlocking(int fd);

	// -----------------------------
	// 接続処理
	// -----------------------------
	void handleNewConnection();
	int acceptClient(); // accept + nonblocking設定
	void handleDisconnect(int fd, int index, int bytes);
	void handleConnectionClose(int fd);
	void handleServerError(int fd);

	// -----------------------------
	// クライアント受信処理
	// -----------------------------
	void handleClient(int index);
	std::string extractNextRequest(int clientFd, std::string &recvBuffer,
									   Request &currentRequest);
	bool isContentLengthExceeded(const Request &req, const std::string &recvBuffer);
	void sendHttpError(int clientFd, int status, const std::string &msg,
					   size_t parsedLength, std::string &recvBuffer);		
	bool isMethodAllowed(const std::string &method,
						 const ServerConfig::Location *loc);
	bool checkMaxBodySize(int fd, int bytes, const ServerConfig::Location *loc);
	bool handleMethodCheck(int fd, Request &req, const ServerConfig::Location *loc, size_t reqSize);
	void processRequest(int fd, Request &req, const ServerConfig::Location *loc,
						const std::string &locPath, size_t reqSize);
	bool handleRedirect(int fd, const ServerConfig::Location *loc);

	// -----------------------------
	// クライアント送信処理
	// -----------------------------
	void handleClientSend(int index);
	void queueSend(int fd, const std::string &data);

	int findIndexByFd(int fd);

	// -----------------------------
	// ここから追加：CGI対応用
	// -----------------------------
	bool isCgiRequest(const Request &req);													   // CGI判定関数
	void startCgiProcess(int clientFd, const Request &req, const ServerConfig::Location &loc); // CGI実行関数
	void handleCgiOutput(int outFd);
	void handleCgiClose(int outFd);
	void handleCgiError(int outFd);	
	void handleCgiInput(int fd);										   
	std::string buildHttpResponseFromCgi(const std::string &cgiOutput);
	std::string buildHttpErrorPage(int code, const std::string &message);
	void registerCgiProcess(int clientFd, pid_t pid,
								int inFd, int outFd, const std::string &body,
								std::map<int, CgiProcess> &cgiMap);

	Server::LocationMatch getLocationForUri(const std::string &uri) const;
	void sendGatewayTimeout(int clientFd);

	// -----------------------------
	// ここから追加： POST処理用
	// -----------------------------
	void handlePost(int fd, Request &req, const ServerConfig::Location *loc);
	void handleMultipartForm(int fd, Request &req, const ServerConfig::Location *loc);
	void handleUrlEncodedForm(int fd, Request &req, const ServerConfig::Location *loc);
	void handleChunkedBody(int fd, Request &req, const ServerConfig::Location *loc);

	int findFdByRecvBuffer(const std::string &buffer) const;

public:
	// -----------------------------
	// コンストラクタ / デストラクタ
	// -----------------------------
	Server(const ServerConfig &cfg);
	~Server();

	// -----------------------------
	// 初期化 / メインループ
	// -----------------------------
	bool init();

	int getServerFd() const;
	std::vector<int> getClientFds() const;
	CgiProcess* getCgiProcess(int fd);

	// ServerManager から呼ばれる安全な公開インターフェース
	void onPollEvent(int fd, short revents);

	std::vector<int> getCgiFds() const; // 現在監視中のCGI出力FDリスト
	void checkCgiTimeouts(int maxLoops);
	bool hasPendingSend(int fd) const;
};

#endif
