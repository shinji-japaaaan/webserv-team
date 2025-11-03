#ifndef SERVERMANAGER_HPP
#define SERVERMANAGER_HPP

#include <poll.h>

#include <map>
#include <string>
#include <vector>

#include "ConfigParser.hpp"
#include "Server.hpp"

// pollで管理するFD情報
struct PollEntry {
    int fd;
    short events;
    short revents;
    Server* server;
    int clientFd;  // どのクライアントに紐づくCGIか
    bool isCgiFd;  // CGI用パイプかどうか
};

class ServerManager {
   private:
    std::vector<Server*> servers;
    std::vector<ServerConfig> configs;
    std::vector<PollEntry> buildPollEntries();
    void handlePollEvents(std::vector<PollEntry>& entries);

   public:
    ServerManager();
    ~ServerManager();

    bool loadConfig(const std::string& path);
    bool initAllServers();
    void runAllServers();
};

#endif
