#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#define MAX_CLIENTS 100

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    // SO_REUSEADDR を設定（再起動時に bind エラーを防ぐ）
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    std::cout << "Waiting for connections..." << std::endl;

    pollfd fds[MAX_CLIENTS];
    int nfds = 1;

    // listenソケットをpoll配列に登録
    fds[0].fd = server_fd;
    fds[0].events = POLLIN;

    while (true) {
        int ret = poll(fds, nfds, -1); // -1 = 無限待ち
        if (ret < 0) {
            perror("poll");
            break;
        }

        // 新しいクライアント接続を受け入れる
        if (fds[0].revents & POLLIN) {
            int client_fd = accept(server_fd, NULL, NULL);
            if (client_fd < 0) {
                perror("accept");
            } else {
                if (nfds >= MAX_CLIENTS) {
                    printf("Max clients reached, rejecting: fd=%d\n", client_fd);
                    close(client_fd);
                } else {
                    fds[nfds].fd = client_fd;
                    fds[nfds].events = POLLIN;
                    nfds++;
                    printf("New client connected: fd=%d\n", client_fd);
                }
            }
        }

        // 既存クライアントからの受信処理
        for (int i = 1; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                char buffer[1024];
                int bytes = recv(fds[i].fd, buffer, sizeof(buffer)-1, 0);
                if (bytes <= 0) {
                    // クライアント切断
                    close(fds[i].fd);
                    printf("Client disconnected: fd=%d\n", fds[i].fd);
                    fds[i] = fds[nfds-1]; // 配列を詰める
                    nfds--;
                    i--; // 詰めたので同じ i を再チェック
                } else {
                    buffer[bytes] = '\0';
                    printf("Received from fd=%d: %s\n", fds[i].fd, buffer);
                    // エコーで返す（エラー処理付き）
                    int sent = send(fds[i].fd, buffer, bytes, 0);
                    if (sent < 0) {
                        perror("send");
                        close(fds[i].fd);
                        fds[i] = fds[nfds-1];
                        nfds--;
                        i--;
                    }
                }
            }
        }
    }

    // サーバー終了処理
    for (int i = 0; i < nfds; i++) {
        close(fds[i].fd);
    }
}
