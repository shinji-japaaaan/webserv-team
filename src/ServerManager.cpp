#include "ServerManager.hpp"
#include <iostream>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "CgiProcess.hpp"

ServerManager::ServerManager() {}

ServerManager::~ServerManager() {
    for (size_t i = 0; i < servers.size(); i++) {
        delete servers[i];
    }
}

bool ServerManager::loadConfig(const std::string &path) {
    ConfigParser parser;
    configs = parser.getServerConfigs(path);
    return true;
}

bool ServerManager::initAllServers() {
    for (size_t i = 0; i < configs.size(); ++i) {
        const ServerConfig &cfg = configs[i];
        Server* srv = new Server(cfg);
        if (!srv->init()) {
            delete srv;
            return false;
        }
        servers.push_back(srv);
        std::cout << "Initialized server on " 
                  << cfg.host << ":" << cfg.port 
                  << " (root=" << cfg.root << ")" << std::endl;
    }
    return true;
}

// ----------------------------
// 全ServerのFDを1つのpoll配列で管理する
// ----------------------------
void ServerManager::runAllServers() {
    const int pollTimeoutMs = 100;
    const int cgiTimeoutSeconds = 5;

    while (true) {
        std::vector<PollEntry> entries = buildPollEntries();

        // pollfd を vector で確保
        std::vector<pollfd> fds(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            fds[i].fd = entries[i].fd;
            fds[i].events = entries[i].events;
            fds[i].revents = 0;
        }

        int ret = poll(&fds[0], fds.size(), pollTimeoutMs);
        if (ret < 0) {
            perror("poll");
            continue;
        }

        handlePollEvents(&fds[0], fds.size(), entries);

        // --- CGI タイムアウト処理 ---
        for (size_t i = 0; i < servers.size(); ++i) {
            servers[i]->checkCgiTimeouts(cgiTimeoutSeconds);
        }
    }
}


// 送信待ちデータがあるか確認
bool Server::hasPendingSend(int fd) const {
    std::map<int, ClientInfo>::const_iterator it = clients.find(fd);
    if (it == clients.end()) return false;  // fd が存在しない場合は false
    return !it->second.sendBuffer.empty();  // sendBuffer が空でなければ true
}

void Server::checkCgiTimeouts(int timeoutSeconds) {
    time_t now = time(NULL);
    std::map<int, CgiProcess>::iterator it = cgiMap.begin();

    while (it != cgiMap.end()) {
        CgiProcess &proc = it->second;

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

    ClientInfo &client = it->second;
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
    std::vector<PollEntry> pollEntries;

    for (size_t i = 0; i < servers.size(); ++i) {
        Server* srv = servers[i];

        // --- listen socket ---
        PollEntry listenEntry;
        listenEntry.fd = srv->getServerFd();
        listenEntry.events = POLLIN;
        listenEntry.server = srv;
        listenEntry.clientFd = 0;
        listenEntry.isCgiFd = false;
        pollEntries.push_back(listenEntry);

        // --- client sockets ---
        std::vector<int> clientFds = srv->getClientFds();
        for (size_t j = 0; j < clientFds.size(); ++j) {
            PollEntry entry;
            entry.fd = clientFds[j];
            entry.events = POLLIN;
            if (srv->hasPendingSend(clientFds[j]))
                entry.events |= POLLOUT;
            entry.server = srv;
            entry.clientFd = clientFds[j];
            entry.isCgiFd = false;
            pollEntries.push_back(entry);
        }

        // --- CGI FDs ---
        std::vector<int> cgiFds = srv->getCgiFds();
        for (size_t j = 0; j < cgiFds.size(); ++j) {
            int fd = cgiFds[j];
            CgiProcess *proc = srv->getCgiProcess(fd); // CGI状態を取得
            if (!proc) // 存在しない場合スキップ
                continue;

            PollEntry entry;
            entry.fd = fd;
            entry.events = proc->events;      // ポインタ参照に変更
            entry.server = srv;
            entry.clientFd = proc->clientFd;  // ポインタ参照に変更
            entry.isCgiFd = true;
            pollEntries.push_back(entry);
        }
    }

    return pollEntries;
}


std::vector<int> Server::getCgiFds() const {
    std::vector<int> fds;
    for (std::map<int, CgiProcess>::const_iterator it = cgiMap.begin();
        it != cgiMap.end(); ++it) {
        fds.push_back(it->first);
    }
    return fds;
}

// ----------------------------
// pollイベント処理
// ----------------------------
void ServerManager::handlePollEvents(struct pollfd* fds, size_t nfds, const std::vector<PollEntry>& entries) {
    for (size_t i = 0; i < nfds; i++) {
        if (fds[i].revents != 0) {
            entries[i].server->onPollEvent(fds[i].fd, fds[i].revents);
        }
    }
}

