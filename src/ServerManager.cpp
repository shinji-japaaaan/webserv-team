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
    const int POLL_SLICE_MS = 100;     // pollごとのスライス
    const int READ_TIMEOUT_MS = 5000;  // タイムアウト5秒

    while (true) {
        std::vector<PollEntry> entries = buildPollEntries();

        std::vector<pollfd> fds(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            fds[i].fd = entries[i].fd;
            fds[i].events = entries[i].events;
            fds[i].revents = 0;
        }
        
        int ret = poll(&fds[0], fds.size(), POLL_SLICE_MS);
        if (ret < 0) {
            perror("poll");
            continue;
        }

        handlePollEvents(&fds[0], fds.size(), entries);

        // --- CGI タイムアウト処理 ---
        for (size_t i = 0; i < servers.size(); ++i) {
            servers[i]->checkCgiTimeouts(POLL_SLICE_MS);
        }

        // --- クライアント read タイムアウトチェック ---
        for (size_t i = 0; i < servers.size(); ++i) {
            servers[i]->checkClientTimeouts(POLL_SLICE_MS, READ_TIMEOUT_MS); // pollTimeoutMs単位に換算
        }

    }
}


// 送信待ちデータがあるか確認
bool Server::hasPendingSend(int fd) const {
    std::map<int, ClientInfo>::const_iterator it = clients.find(fd);
    if (it == clients.end()) return false;  // fd が存在しない場合は false
    return !it->second.sendBuffer.empty();  // sendBuffer が空でなければ true
}

void Server::checkCgiTimeouts(int elapsedMs) {
    for (std::map<int, CgiProcess>::iterator it = cgiMap.begin();
         it != cgiMap.end();) {
        CgiProcess &proc = it->second;

        // 残り時間を減算
        proc.remainingMs -= elapsedMs;

        // タイムアウト発生
        if (proc.remainingMs <= 0) {
            std::cerr << "[CGI Timeout] pid=" << proc.pid
                      << " fd=" << it->first << std::endl;

            kill(proc.pid, SIGKILL);
            sendGatewayTimeout(proc.clientFd);

            if (proc.inFd > 0) close(proc.inFd);
            if (proc.outFd > 0) close(proc.outFd);

            waitpid(proc.pid, NULL, 0);

            std::map<int, CgiProcess>::iterator tmp = it++;
            cgiMap.erase(tmp);
        } else {
            ++it;
        }
    }
}

void Server::sendGatewayTimeout(int clientFd) {
    // 504 レスポンスの本文を作成
    std::string body = "<html><body><h1>504 Gateway Timeout</h1>"
                       "<p>The CGI script did not respond in time.</p></body></html>";

    // Content-Length を本文のサイズに合わせる
    std::stringstream ss;
    ss << "HTTP/1.1 504 Gateway Timeout\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Content-Type: text/html\r\n"
       << "Connection: close\r\n\r\n"
       << body;

    std::string response = ss.str();

    // クライアント情報が存在しない場合は作成
    std::map<int, ClientInfo>::iterator it = clients.find(clientFd);
    if (it == clients.end()) {
        ClientInfo ci;
        clients[clientFd] = ci;
        it = clients.find(clientFd);
    }

    ClientInfo &client = it->second;
    client.sendBuffer += response;
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
            int outFd = cgiFds[j];
            CgiProcess *proc = srv->getCgiProcess(outFd); // CGI状態を取得
            if (!proc)
                continue;

            // --- 出力側（子→親） ---
            PollEntry outEntry;
            outEntry.fd = proc->outFd;
            outEntry.events = POLLIN; // 常に子出力監視
            outEntry.server = srv;
            outEntry.clientFd = proc->clientFd;
            outEntry.isCgiFd = true;
            pollEntries.push_back(outEntry);

            // --- 入力側（親→子） ---
            if (proc->inFd >= 0) {
                PollEntry inEntry;
                inEntry.fd = proc->inFd;
                inEntry.events = 0;
                if (!proc->inputBuffer.empty())
                    inEntry.events |= POLLOUT; // 書き込み残があればPOLLOUT
                inEntry.server = srv;
                inEntry.clientFd = proc->clientFd;
                inEntry.isCgiFd = true;
                pollEntries.push_back(inEntry);
            }
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

