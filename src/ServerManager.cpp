#include "ServerManager.hpp"
#include "Server.hpp"
#include "CgiProcess.hpp"
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>

ServerManager::ServerManager() {}

ServerManager::~ServerManager() {
    for (size_t i = 0; i < servers.size(); i++) {
        delete servers[i];
    }
}

bool ServerManager::loadConfig(const std::string& path) {
    ConfigParser parser;
    configs = parser.getServerConfigs(path);
    return true;
}

bool ServerManager::initAllServers() {
    for (size_t i = 0; i < configs.size(); ++i) {
        const ServerConfig& cfg = configs[i];
        Server* srv = new Server(cfg);
        if (!srv->init()) {
            delete srv;
            return false;
        }
        servers.push_back(srv);
        std::cout << "Initialized server on " << cfg.host << ":" << cfg.port
                  << " (root=" << cfg.root << ")" << std::endl;
    }
    return true;
}

// ----------------------------
// 全ServerのFDを1つのpoll配列で管理する
// ----------------------------
void ServerManager::runAllServers() {
    const int pollTimeoutMs = 100;    // poll のタイムアウト
    const int cgiTimeoutSeconds = 5;  // CGI タイムアウトは 5 秒

    while (true) {
        std::vector<PollEntry> entries = buildPollEntries();
        struct pollfd* fds = new struct pollfd[entries.size()];
        for (size_t i = 0; i < entries.size(); i++) {
            fds[i].fd = entries[i].fd;
            fds[i].events = entries[i].events;
            fds[i].revents = 0;
        }

        int ret = poll(fds, entries.size(), pollTimeoutMs);
        if (ret < 0) {
            perror("poll");
            delete[] fds;
            continue;
        }

        for (size_t i = 0; i < entries.size(); ++i)
            entries[i].revents = fds[i].revents;

        handlePollEvents(entries);

        // --- CGI タイムアウト処理 ---
        for (size_t i = 0; i < servers.size(); ++i) {
            servers[i]->checkCgiTimeouts(cgiTimeoutSeconds);
        }

        delete[] fds;
    }
}

// 送信待ちデータがあるか確認
bool Server::hasPendingSend(int fd) const {
    std::map<int, ClientInfo>::const_iterator it = clients.find(fd);
    if (it == clients.end())
        return false;                       // fd が存在しない場合は false
    return !it->second.sendBuffer.empty();  // sendBuffer が空でなければ true
}

void Server::checkCgiTimeouts(int timeoutSeconds) {
    time_t now = time(NULL);
    std::map<int, CgiProcess>::iterator it = cgiMap.begin();

    while (it != cgiMap.end()) {
        CgiProcess& proc = it->second;

        if (difftime(now, proc.startTime) > timeoutSeconds) {
            // --- CGI 強制終了 ---
            kill(proc.pid, SIGKILL);

            // --- 504 Gateway Timeout レスポンス作成 ---
            sendGatewayTimeout(proc.clientFd);

            // --- CGI 出力 fd を閉じる ---
            close(proc.outFd);

            // --- 子プロセス回収 ---
            waitpid(proc.pid, NULL, 0);

            // --- map から削除 ---
            std::map<int, CgiProcess>::iterator tmp = it;
            ++it;
            cgiMap.erase(tmp);
        } else {
            ++it;
        }
    }
}

void Server::sendGatewayTimeout(int clientFd) {
    std::string response =
        "HTTP/1.1 504 Gateway Timeout\r\n"
        "Content-Length: 60\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<html><body><h1>504 Gateway Timeout</h1>"
        "<p>The CGI script did not respond in time.</p></body></html>";

    // クライアント情報が存在しない場合は作成
    std::map<int, ClientInfo>::iterator it = clients.find(clientFd);
    if (it == clients.end()) {
        ClientInfo ci;
        clients[clientFd] = ci;
        it = clients.find(clientFd);
    }

    ClientInfo& client = it->second;
    client.sendBuffer += response;

    // POLLOUT を有効化して poll で送信可能にする
    int idx = findIndexByFd(clientFd);
    if (idx >= 0) {
        fds[idx].events |= POLLOUT;
    }
}

// ----------------------------
// poll対象FDの作成
// ----------------------------
std::vector<PollEntry> ServerManager::buildPollEntries() {
    std::vector<PollEntry> entries;

    for (size_t i = 0; i < servers.size(); ++i) {
        Server* srv = servers[i];

        // --- サーバソケット（listen）登録 ---
        PollEntry serverEntry;
        serverEntry.fd = srv->getServerFd();
        serverEntry.events = POLLIN;  // 新規接続待ち
        serverEntry.server = srv;
        entries.push_back(serverEntry);

        // --- クライアントFD登録 ---
        const std::map<int, ClientInfo>& clients = srv->getClients();
        for (std::map<int, ClientInfo>::const_iterator it = clients.begin();
             it != clients.end(); ++it) {
            PollEntry entry;
            entry.fd = it->first;
            entry.server = srv;
            entry.events = POLLIN;

            // 🔹送信バッファが残っているクライアントには POLLOUT を追加
            if (srv->hasPendingSend(it->first)) {
                entry.events |= POLLOUT;
            }

            entries.push_back(entry);
        }

        // --- CGI FD登録（出力待ち）---
        const std::map<int, CgiProcess>& cgiMap = srv->getCgiMap();
        for (std::map<int, CgiProcess>::const_iterator it = cgiMap.begin();
            it != cgiMap.end(); ++it) {
            const CgiProcess& cgi = it->second;

            // CGIへの書き込み側（サーバ→CGI）
            PollEntry writeEntry;
            writeEntry.fd = cgi.inFd;
            writeEntry.server = srv;
            writeEntry.events = POLLOUT;  // サーバがCGIにデータを送る
            entries.push_back(writeEntry);

            // CGIの出力側（CGI→サーバ）
            PollEntry readEntry;
            readEntry.fd = cgi.outFd;
            readEntry.server = srv;
            readEntry.events = POLLIN;  // CGIの出力を受け取る
            entries.push_back(readEntry);
        }
    }
    return entries;
}

// ----------------------------
// pollイベント処理
// ----------------------------
void ServerManager::handlePollEvents(std::vector<PollEntry>& entries) {
    for (size_t i = 0; i < entries.size(); ++i) {
        int fd = entries[i].fd;
        short revents = entries[i].revents;
        Server* srv = entries[i].server;

        // if (revents & POLLERR) {
        //     srv->handlePollError(fd);
        //     continue;
        // }
        if (revents & POLLIN) {
            srv->handlePollIn(fd);
        }
        if (revents & POLLOUT) {
            srv->handlePollOut(fd);
        }
    }
}

