#include "Server.hpp"
#include "log.hpp"
#include "RequestParser.hpp"
#include "resp/ResponseBuilder.hpp" 
#include <sstream> 

// ----------------------------
// コンストラクタ・デストラクタ
// ----------------------------

// サーバー初期化（ポート指定）
Server::Server(int port, const std::string &host, const std::string &root,
               const std::map<int, std::string> &errorPages)
    : serverFd(-1), nfds(1), port(port),
      host(host), root(root), errorPages(errorPages) {}

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
        logMessage(ERROR, std::string("socket() failed: ") + strerror(errno));
        perror("socket");
        return false;
    }

    int opt = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        logMessage(ERROR, std::string("setsockopt() failed: ") + strerror(errno));
        perror("setsockopt");
        return false;
    }
    int flags = fcntl(serverFd, F_GETFL, 0);
    if (flags == -1) {
        logMessage(ERROR, std::string("fcntl(F_GETFL) failed: ") + strerror(errno));
        perror("fcntl get");
        return false;
    }

    if (fcntl(serverFd, F_SETFL, flags | O_NONBLOCK) == -1) {
        logMessage(ERROR, std::string("fcntl(O_NONBLOCK) failed: ") + strerror(errno));
        perror("fcntl set O_NONBLOCK");
        return false;
    }

    return true;
}

// bind & listen 設定
bool Server::bindAndListen() {
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host.c_str());

    if (addr.sin_addr.s_addr == INADDR_NONE) {
        // "0.0.0.0" の場合などは明示的に ANY に
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        logMessage(ERROR, std::string("bind() failed: ") + strerror(errno));
        perror("bind");
        return false;
    }

    if (listen(serverFd, 5) < 0) {
        logMessage(ERROR, std::string("listen() failed: ") + strerror(errno));
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
            logMessage(ERROR, std::string("poll() failed: ") + strerror(errno));
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
        std::ostringstream oss;
        oss << "Max clients reached, rejecting fd=" << clientFd;
        logMessage(WARNING, oss.str());
        close(clientFd);
        return;
    }

    fds[nfds].fd = clientFd;
    fds[nfds].events = POLLIN;
    nfds++;

    clients[clientFd] = ClientInfo();

    printf("New client connected: fd=%d\n", clientFd);
}

// accept + ノンブロッキング設定をまとめた関数
int Server::acceptClient() {
    int clientFd = accept(serverFd, NULL, NULL);
    if (clientFd < 0) {
        logMessage(ERROR, std::string("accept() failed: ") + strerror(errno));
        perror("accept");
        return -1;
    }

    int flags = fcntl(clientFd, F_GETFL, 0);
    if (flags == -1) {
        logMessage(ERROR, std::string("fcntl(F_GETFL client) failed: ") + strerror(errno));
        perror("fcntl get client");
        close(clientFd);
        return -1;
    }
    if (fcntl(clientFd, F_SETFL, flags | O_NONBLOCK) == -1) {
        logMessage(ERROR, std::string("fcntl(O_NONBLOCK client) failed: ") + strerror(errno));
        perror("fcntl set O_NONBLOCK client");
        close(clientFd);
        return -1;
    }

    return clientFd;
}

// ----------------------------
// クライアント受信処理
// ----------------------------

void Server::handleClient(int index) {
    char buffer[1024];
    int fd = fds[index].fd;
    int bytes = recv(fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes <= 0) {
        handleDisconnect(fd, index, bytes);
        return;
    } else {
      buffer[bytes] = '\0';
      clients[fd].recvBuffer.append(buffer);
        while (true) {
            std::string request =
                extractNextRequest(clients[fd].recvBuffer, clients[fd].currentRequest);
            if (request.empty()) break;

            printRequest(clients[fd].currentRequest);
            printf("Request complete from fd=%d\n", fd);

            std::string response;

            // --- CGI判定 ---
            bool isCgi = isCgiRequest(clients[fd].currentRequest);

            if (isCgi) {
                response = executeCgi(clients[fd].currentRequest);
            } else {
                ResponseBuilder rb;
                response = rb.generateResponse(clients[fd].currentRequest);
            }

            // このリクエスト分を削る（※二重eraseしない）
            clients[fd].recvBuffer.erase(0, request.size());
        }
    }
}

bool Server::isCgiRequest(const Request &req) {
    if (req.uri.size() < 4) return false;
    std::string ext = req.uri.substr(req.uri.find_last_of("."));
    return (ext == ".php" || ext == ".py");
}

// ----------------------------
// CGI実行用関数
// ----------------------------

std::string Server::executeCgi(const Request &req) {
    int inPipe[2], outPipe[2];
    if (pipe(inPipe) < 0 || pipe(outPipe) < 0) {
        return "Status: 500 Internal Server Error\r\n\r\nPipe creation failed";
    }

    pid_t pid = fork();
    if (pid < 0) {
        return "Status: 500 Internal Server Error\r\n\r\nFork failed";
    }

    if (pid == 0) { // 子プロセス
        // stdin/outをpipeに差し替え
        dup2(inPipe[0], STDIN_FILENO);
        dup2(outPipe[1], STDOUT_FILENO);
        close(inPipe[1]); close(inPipe[0]);
        close(outPipe[0]); close(outPipe[1]);

        // 環境変数設定
        setenv("REQUEST_METHOD", req.method.c_str(), 1);
        {
            std::ostringstream oss;
            oss << req.body.size();
            setenv("CONTENT_LENGTH", oss.str().c_str(), 1);
        }
        setenv("SCRIPT_FILENAME", req.uri.c_str(), 1);

        char* argv[] = {const_cast<char*>(req.uri.c_str()), NULL};
        char* envp[] = {NULL};
        execve(req.uri.c_str(), argv, envp);
        exit(1); // exec失敗時
    } else { // 親プロセス
        close(inPipe[0]);
        close(outPipe[1]);

        // リクエストボディを子プロセスに送る
        if (!req.body.empty()) {
            write(inPipe[1], req.body.c_str(), req.body.size());
        }
        close(inPipe[1]);

        // 子プロセスの出力を取得
        std::string result;
        char buf[4096];
        ssize_t n;
        while ((n = read(outPipe[0], buf, sizeof(buf))) > 0) {
            result.append(buf, n);
        }
        close(outPipe[0]);

        // 必要ならHTTPヘッダ追加
        if (result.find("HTTP/") != 0) {
            std::ostringstream oss;
            oss << "HTTP/1.1 200 OK\r\nContent-Length: " << result.size() << "\r\n\r\n" << result;
            result = oss.str();
        }

        return result;
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
        } 
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
    // bytes が 0 または負の場合は接続終了とみなす
    if (bytes <= 0) {
        std::ostringstream oss;
        if (bytes == 0) {
            oss << "Client disconnected: fd=" << fd;
        } else {
            oss << "Client read error or disconnected: fd=" << fd;
        }
        logMessage(INFO, oss.str());
        close(fd);// ソケットを閉じる
        fds[index] = fds[nfds - 1];// fds 配列の詰め替え
        nfds--;
        clients.erase(fd);// clients から削除
    }
}


// ----------------------------
// ヘッダ解析・リクエスト処理
// ----------------------------

std::string Server::extractNextRequest(std::string &recvBuffer,
                                       Request &currentRequest) {
  RequestParser parser;
  if (!parser.isRequestComplete(recvBuffer))
    return "";
  currentRequest = parser.parse(recvBuffer);
  return recvBuffer.substr(0, parser.getParsedLength());
}

int Server::getServerFd() const {
    return serverFd;
}

std::vector<int> Server::getClientFds() const {
    std::vector<int> fds;
    for (std::map<int, ClientInfo>::const_iterator it = clients.begin();
         it != clients.end(); ++it) {
        fds.push_back(it->first);
    }
    return fds;
}

void Server::onPollEvent(int fd, short revents) {
    if (fd == serverFd && (revents & POLLIN)) {
        handleNewConnection();
    } else {
        if (revents & POLLIN)
            handleClient(findIndexByFd(fd));
        if (revents & POLLOUT)
            handleClientSend(findIndexByFd(fd));
    }
}

// fdからindexを見つける補助関数
int Server::findIndexByFd(int fd) {
    for (int i = 0; i < nfds; ++i) {
        if (fds[i].fd == fd)
            return i;
    }
    return -1;
}


