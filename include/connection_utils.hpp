#ifndef CONNECTION_UTILS_HPP
#define CONNECTION_UTILS_HPP

#include <unistd.h>
#include <map>
#include <string>
#include <poll.h>
#include "Server.hpp"

// fds は pollfd[MAX_CLIENTS] で固定長配列なので std::vector は不要
void handleConnectionClose(int fd,
                           std::map<int, ClientInfo> &clients,
                           pollfd fds[], int &nfds);

#endif