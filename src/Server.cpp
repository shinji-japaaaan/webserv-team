#include "Server.hpp"

Server::Server(int port) : serverFd(-1), nfds(1), port(port) {}

Server::~Server() {
    for (int i = 0; i < nfds; i++) {
        close(fds[i].fd);
    }
    clients.clear();
}

bool Server::init() {
    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        perror("socket");
        return false;
    }

    int opt = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        return false;
    }

    // ノンブロッキング設定
    int flags = fcntl(serverFd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl get");
        return false;
    }
    if (fcntl(serverFd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl set O_NONBLOCK");
        return false;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return false;
    }

    if (listen(serverFd, 5) < 0) {
        perror("listen");
        return false;
    }

    fds[0].fd = serverFd;
    fds[0].events = POLLIN;

    std::cout << "Server listening on port " << port << std::endl;
    return true;
}

void Server::run() {
    while (true) {
        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            perror("poll");
            break;
        }

        if (fds[0].revents & POLLIN) {
            handleNewConnection();
        }

        for (int i = 1; i < nfds; i++) {
            int fd = fds[i].fd;
            if (fds[i].revents & POLLIN) {
                handleClient(i);
            }
            // ★ Day19-20追加: POLLOUT が立っていれば送信
            if (fds[i].revents & POLLOUT) {
                ClientInfo &client = clients[fd];
                if (!client.sendBuffer.empty()) {
                    ssize_t n = write(fd, client.sendBuffer.data(),
                                      client.sendBuffer.size());
                    if (n > 0) {
                        client.sendBuffer.erase(0, n);
                        if (client.sendBuffer.empty()) {
                            fds[i].events &= ~POLLOUT; // 送信完了 → POLLOUT 無効化
                        }
                    } else if (n == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("write");
                        close(fd);
                        fds[i] = fds[nfds-1];
                        nfds--;
                        clients.erase(fd);
                    }
                } else {
                    fds[i].events &= ~POLLOUT; // 送信バッファ空ならPOLLOUT無効化
                }
            }
        }
    }
}

void Server::handleNewConnection() {
    int clientFd = accept(serverFd, NULL, NULL);
    if (clientFd < 0) {
        perror("accept");
    } else if (nfds >= MAX_CLIENTS) {
        printf("Max clients reached, rejecting: fd=%d\n", clientFd);
        close(clientFd);
    } else {
        int flags = fcntl(clientFd, F_GETFL, 0);
        if (flags == -1) {
            perror("fcntl get client");
            close(clientFd);
            return;
        } else if (fcntl(clientFd, F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("fcntl set O_NONBLOCK client");
            close(clientFd);
            return;
        }
        fds[nfds].fd = clientFd;
        fds[nfds].events = POLLIN;
        nfds++;

        // ★ Day19-20追加: sendBuffer も含めて初期化
        clients[clientFd] = ClientInfo();

        printf("New client connected: fd=%d\n", clientFd);
    }
}

void Server::handleClient(int index) {
    char buffer[1024];
    int fd = fds[index].fd;
    int bytes = recv(fd, buffer, sizeof(buffer)-1, 0);

    if (bytes == 0) {
        close(fd);
        printf("Client disconnected: fd=%d\n", fd);
        fds[index] = fds[nfds-1];
        nfds--;
        clients.erase(fd);
    } else if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        perror("recv");
        close(fd);
        fds[index] = fds[nfds-1];
        nfds--;
        clients.erase(fd);
    } else {
        buffer[bytes] = '\0';
        clients[fd].recvBuffer.append(buffer);

        if (clients[fd].recvBuffer.find("\r\n\r\n") != std::string::npos) {
            clients[fd].requestComplete = true;

            printf("Request complete from fd=%d:\n%s\n",
                   fd, clients[fd].recvBuffer.c_str());

            

            // parseRequest(clients[fd].recvBuffer); // Bさん用

            clients[fd].recvBuffer.clear();
            clients[fd].requestComplete = false;
        }
    }
}

// ★ Day19-20修正版: C++98対応（auto禁止）
void Server::queueSend(int fd, const std::string &data) {
    std::map<int, ClientInfo>::iterator it = clients.find(fd);
    if (it != clients.end()) {
        // 送信バッファにデータを追加
        it->second.sendBuffer += data;

        // POLLOUT を有効化して poll に送信させる
        for (int i = 1; i < nfds; i++) {
            if (fds[i].fd == fd) {
                fds[i].events |= POLLOUT | POLLIN;
                break;
            }
        }
    }
}

