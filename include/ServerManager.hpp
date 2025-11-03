#ifndef SERVERMANAGER_HPP
#define SERVERMANAGER_HPP

#include <vector>
#include <string>
#include <map>
#include <poll.h>
#include "Server.hpp"
#include "ConfigParser.hpp"

// pollで管理するFD情報
struct PollEntry {
    int fd;
    short events;
    Server* server;
    int clientFd;        // どのクライアントに紐づくCGIか
    bool isCgiFd;        // CGI用パイプかどうか
};

class ServerManager {
private:
    std::vector<Server*> servers;
    std::vector<ServerConfig> configs;
    std::vector<PollEntry> buildPollEntries();
    void handlePollEvents(struct pollfd* fds, size_t nfds, const std::vector<PollEntry>& entries);

    public:
      ServerManager();
      ~ServerManager();

      bool loadConfig(const std::string &path);
      bool initAllServers();
      void runAllServers();
    };

#endif
