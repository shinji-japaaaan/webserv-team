#include "Server.hpp"

Server::Server(int port) : serverFd(-1), nfds(1), port(port) {}

Server::~Server() {
    for (int i = 0; i < nfds; i++) {
        close(fds[i].fd);
    }
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
            if (fds[i].revents & POLLIN) {
                handleClient(i);
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
        fds[nfds].fd = clientFd;
        fds[nfds].events = POLLIN;
        nfds++;
        printf("New client connected: fd=%d\n", clientFd);
    }
}

void Server::handleClient(int index) {
    char buffer[1024];
    int bytes = recv(fds[index].fd, buffer, sizeof(buffer)-1, 0);

    if (bytes <= 0) {
        close(fds[index].fd);
        printf("Client disconnected: fd=%d\n", fds[index].fd);
        fds[index] = fds[nfds-1];
        nfds--;
    } else {
        buffer[bytes] = '\0';
        printf("Received from fd=%d: %s\n", fds[index].fd, buffer);

        int sent = send(fds[index].fd, buffer, bytes, 0);
        if (sent < 0) {
            perror("send");
            close(fds[index].fd);
            fds[index] = fds[nfds-1];
            nfds--;
        }
    }
}
