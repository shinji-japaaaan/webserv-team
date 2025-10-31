#include "ServerManager.hpp"
#include <iostream>
#include <poll.h>
#include <unistd.h>

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
    const int pollTimeoutMs = 100;   // poll のタイムアウト
    const int cgiTimeoutLoops = 50;  // 100ms * 50 ≒ 5秒

    while (true) {
        std::vector<PollEntry> entries = buildPollEntries();
        if (entries.empty()) continue;

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

        handlePollEvents(fds, entries.size(), entries);

        // --- CGI タイムアウト処理は各サーバーに委譲 ---
        for (size_t i = 0; i < servers.size(); ++i) {
            servers[i]->checkCgiTimeouts(cgiTimeoutLoops);
        }

        delete[] fds;
    }
}

// ----------------------------
// poll対象FDの作成
// ----------------------------
std::vector<PollEntry> ServerManager::buildPollEntries() {
    std::vector<PollEntry> pollEntries;

    for (size_t i = 0; i < servers.size(); i++) {
        Server* srv = servers[i];

        // listen socket
        PollEntry listenEntry;
        listenEntry.fd = srv->getServerFd();
        listenEntry.events = POLLIN;
        listenEntry.server = srv;
        pollEntries.push_back(listenEntry);

        // client sockets
        std::vector<int> clientFds = srv->getClientFds();
        for (size_t j = 0; j < clientFds.size(); j++) {
            PollEntry entry;
            entry.fd = clientFds[j];
            entry.events = POLLIN | POLLOUT;
            entry.server = srv;
            pollEntries.push_back(entry);
        }

        // --- CGI 出力パイプ ---
        std::vector<int> cgiFds = srv->getCgiFds();
        for (size_t j = 0; j < cgiFds.size(); j++) {
            PollEntry entry;
            entry.fd = cgiFds[j];
            entry.events = POLLIN; // CGIは読むだけ
            entry.server = srv;
            pollEntries.push_back(entry);
        }
    }

    return pollEntries;
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

