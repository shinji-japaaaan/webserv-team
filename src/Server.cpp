#include "Server.hpp"

// ----------------------------
// コンストラクタ・デストラクタ
// ----------------------------

// サーバー初期化（ポート指定）
Server::Server(int port) : serverFd(-1), nfds(1), port(port) {}

// サーバー破棄（全クライアントFDクローズ）
Server::~Server() {
    for (int i = 0; i < nfds; i++) {
        close(fds[i].fd);
    }
    clients.clear();
}

// ----------------------------
// 初期化系関数
// ----------------------------

// サーバー全体の初期化（ソケット作成＋バインド＋リッスン）
bool Server::init() {
    if (!createSocket())
        return false;

    if (!bindAndListen())
        return false;

    fds[0].fd = serverFd;
    fds[0].events = POLLIN;

    std::cout << "Server listening on port " << port << std::endl;
    return true;
}

// ソケット作成とオプション設定
bool Server::createSocket() {
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

    int flags = fcntl(serverFd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl get");
        return false;
    }

    if (fcntl(serverFd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl set O_NONBLOCK");
        return false;
    }

    return true;
}

// bind & listen 設定
bool Server::bindAndListen() {
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

    return true;
}

// ----------------------------
// メインループ
// ----------------------------

// サーバー実行（poll で I/O待機し、新規接続・クライアント処理）
void Server::run() {
    while (true) {
        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            perror("poll");
            break;
        }

        // 新規接続の処理
        if (fds[0].revents & POLLIN) {
            handleNewConnection();
        }

        // 各クライアントの処理
        for (int i = 1; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                handleClient(i);
            }
            if (fds[i].revents & POLLOUT) {
                handleClientSend(i);
            }
        }
    }
}

// ----------------------------
// クライアント接続処理
// ----------------------------

// 新規接続ハンドラ
void Server::handleNewConnection() {
    int clientFd = acceptClient();
    if (clientFd < 0) return; // accept 失敗時は何もしない

    if (nfds >= MAX_CLIENTS) {
        printf("Max clients reached, rejecting: fd=%d\n", clientFd);
        close(clientFd);
        return;
    }

    // fds 配列と clients を初期化
    fds[nfds].fd = clientFd;
    fds[nfds].events = POLLIN;
    nfds++;

    clients[clientFd] = ClientInfo(); // 送受信バッファ初期化

    printf("New client connected: fd=%d\n", clientFd);
}

// accept + ノンブロッキング設定をまとめた関数
int Server::acceptClient() {
    int clientFd = accept(serverFd, NULL, NULL);
    if (clientFd < 0) {
        perror("accept");
        return -1;
    }

    // ノンブロッキング設定
    int flags = fcntl(clientFd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl get client");
        close(clientFd);
        return -1;
    }
    if (fcntl(clientFd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl set O_NONBLOCK client");
        close(clientFd);
        return -1;
    }

    return clientFd;
}

// ----------------------------
// クライアント受信処理
// ----------------------------

// クライアントからの受信データ処理
void Server::handleClient(int index) {
    char buffer[1024];
    int fd = fds[index].fd;
    int bytes = recv(fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes <= 0) {
        handleDisconnect(fd, index, bytes);
        return;
    } else {
        // --- ここから受信データの処理 ---
        buffer[bytes] = '\0';
        clients[fd].recvBuffer.append(buffer);

        // ★ 複数リクエスト対応ループ
        while (true) {
            std::string request = extractNextRequest(clients[fd].recvBuffer);
            if (request.empty()) break;  // 次のリクエストが未到達なら抜ける

            // 仮置き：Bさんのパーサーに渡す
            // parseRequest(request);

            printf("Request complete from fd=%d:\n%s\n",
                fd, request.c_str());

            // ★ テスト用HTTPレスポンス
            std::string response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 12\r\n"
                "Connection: keep-alive\r\n"
                "\r\n"
                "Hello World\n";

            queueSend(fd, response); // 送信キューに入れる

            // 処理済みリクエスト部分をバッファから削除
            clients[fd].recvBuffer.erase(0, request.size());
        }
    }
}

// ----------------------------
// クライアント送信処理
// ----------------------------

// クライアント送信バッファのデータ送信
void Server::handleClientSend(int index) {
    int fd = fds[index].fd;
    std::map<int, ClientInfo>::iterator it = clients.find(fd);
    if (it == clients.end()) return;

    ClientInfo &client = it->second;
    if (!client.sendBuffer.empty()) {
        ssize_t n = write(fd, client.sendBuffer.data(), client.sendBuffer.size());
        if (n > 0) {
            client.sendBuffer.erase(0, n);
            if (client.sendBuffer.empty()) {
                fds[index].events &= ~POLLOUT; // 送信完了 → POLLOUT 無効化
                handleConnectionClose(fd);
            }
        } else if (n == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("write");
            close(fd);
            fds[index] = fds[nfds-1];
            nfds--;
            clients.erase(fd);
        }
    } else {
        fds[index].events &= ~POLLOUT; // 送信バッファ空なら POLLOUT 無効化
    }
}

// 送信キューにデータを追加する関数
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

// ----------------------------
// クライアント接続終了処理
// ----------------------------

// クライアント接続クローズ処理
void Server::handleConnectionClose(int fd)
{
    // 将来の keep-alive 対応予定
    // if (client.keepAlive && !client.recvBuffer.empty()) {
    //     client.state = READY_FOR_NEXT_REQUEST;
    //     return;
    // }

    std::cout << "[INFO] Closing connection: fd=" << fd << std::endl;

    // ソケットを閉じる
    close(fd);

    // fds 配列から該当 fd を削除（最後の要素と入れ替えて nfds--）
    int index = -1;
    for (int i = 0; i < nfds; ++i) {
        if (fds[i].fd == fd) {
            index = i;
            break;
        }
    }

    if (index != -1) {
        fds[index] = fds[nfds - 1];
        nfds--;
    }

    // clients から削除
    std::map<int, ClientInfo>::iterator it = clients.find(fd);
    if (it != clients.end()) {
        clients.erase(it);
    }
}

// 接続切断処理（recv エラーや切断時の処理）
void Server::handleDisconnect(int fd, int index, int bytes) {
     if (bytes == 0) {
        printf("Client disconnected: fd=%d\n", fd);
    } else if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        perror("recv");
    } else {
        // bytes > 0 の場合はここに来ない（保険）
        return;
    }
    close(fd);
    fds[index] = fds[nfds - 1]; // 配列の詰め替え
    nfds--;
    clients.erase(fd);
}

// ----------------------------
// ヘッダ解析・リクエスト処理
// ----------------------------

// 次のリクエストを受信バッファから抽出
std::string Server::extractNextRequest(std::string &recvBuffer) {
    size_t pos = recvBuffer.find("\r\n\r\n"); // ヘッダ終端判定
    if (pos == std::string::npos) {
        return ""; // ヘッダがまだ揃っていない → 何も返さない
    }
    // 仮でヘッダまでを1リクエストとして返す
    return recvBuffer.substr(0, pos + 4);
}
