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

// ここでは仮にconfigsを読み込むだけ
bool ServerManager::loadConfig(const std::string &path) {
    // BさんのConfigParserを想定
    // 今は仮に1つのポートだけ設定
    (void)path; // 未使用回避
    ServerConfig cfg;
    cfg.port = 8080;
    cfg.host = "0.0.0.0";
    configs.push_back(cfg);
    return true;
}

// bool ServerManager::loadConfig(const std::string &path) {
//     // Bさん：ConfigParserをここに統合してください
//     ConfigParser parser;

//     if (!parser.parse(path)) {
//         std::cerr << "Config parsing failed" << std::endl;
//         return false;
//     }

//     configs = parser.getServerConfigs();  // vector<ServerConfig>を受け取る想定
//     return true;
// }

bool ServerManager::initAllServers() {
    for (size_t i = 0; i < configs.size(); i++) {
        Server* srv = new Server(configs[i].port);
        if (!srv->init()) {
            delete srv;
            return false;
        }
        servers.push_back(srv);
    }
    return true;
}

// ----------------------------
// 全ServerのFDを1つのpoll配列で管理する
// ----------------------------
void ServerManager::runAllServers() {
    while (true) {
        std::vector<PollEntry> entries = buildPollEntries();

        if (entries.empty()) continue;

        struct pollfd* fds = new struct pollfd[entries.size()];
        for (size_t i = 0; i < entries.size(); i++) {
            fds[i].fd = entries[i].fd;
            fds[i].events = entries[i].events;
            fds[i].revents = 0;
        }

        int ret = poll(fds, entries.size(), 100);
        if (ret < 0) {
            perror("poll");
            delete[] fds;
            continue;
        }

        handlePollEvents(fds, entries.size(), entries);

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

