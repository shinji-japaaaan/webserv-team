#ifndef SERVER_HPP
#define SERVER_HPP

#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#define MAX_CLIENTS 100

class Server {
private:
    int serverFd;
    pollfd fds[MAX_CLIENTS];
    int nfds;
    int port;

public:
    Server(int port);
    ~Server();

    bool init();
    void run();

private:
    void handleNewConnection();
    void handleClient(int index);
};

#endif
