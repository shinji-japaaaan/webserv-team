#include "Server.hpp"
#include "log.hpp"
#include "RequestParser.hpp"
#include "ConfigParser.hpp"
#include "resp/ResponseBuilder.hpp" 
#include <sstream> 
#include <sys/wait.h>

// ----------------------------
// コンストラクタ・デストラクタ
// ----------------------------

// サーバー初期化（ポート指定）
Server::Server(const ServerConfig &config)
: cfg(config),
  port(config.port),
  host(config.host),
  root(config.root),
  errorPages(config.errorPages)
{
    // serverブロックで max_body_size が指定されていれば採用
    if (config.max_body_size > 0) {
        clientMaxBodySize = config.max_body_size;
    } else {
        // 無制限
        clientMaxBodySize = SIZE_MAX;
    }
}


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
    }

    buffer[bytes] = '\0';
    clients[fd].recvBuffer.append(buffer);

    while (true) {
        // 1リクエスト分を抽出
        std::string requestStr =
            extractNextRequest(clients[fd].recvBuffer, clients[fd].currentRequest);
        if (requestStr.empty()) break;

        Request &req = clients[fd].currentRequest;

        // Locationごとの max_body_size を取得（デフォルトは server の設定）
        size_t maxSize = clientMaxBodySize; // serverデフォルト
        const ServerConfig::Location* loc = getLocationForUri(req.uri);
        if (loc && loc->max_body_size > 0) {
            maxSize = loc->max_body_size; // Location の値で上書き
        }

        // ボディサイズのみをチェック
        if (maxSize != SIZE_MAX &&
            req.body.size() > maxSize) {
            sendPayloadTooLarge(fd);
            clients[fd].shouldClose = true;
            // このリクエストを処理せず終了
            clients[fd].recvBuffer.erase(0, requestStr.size());
            break;
        }

        printRequest(req);
        printf("Request complete from fd=%d\n", fd);

        if(loc && !loc->cgi_path.empty() && isCgiRequest(req)) {
            // CGIはLocationの中だけで実行
            startCgiProcess(fd, req);
        } else if (req.method == "POST") {
            handlePost(fd, req, loc);  // 通常のPOST処理
        } else {
            ResponseBuilder rb;
            std::string response = rb.generateResponse(req);
            queueSend(fd, response);
        }

        // このリクエスト分を recvBuffer から削除
        clients[fd].recvBuffer.erase(0, requestStr.size());
    }
}


const ServerConfig::Location* Server::getLocationForUri(const std::string &uri) const {
    const ServerConfig::Location* bestMatch = NULL;
    size_t longest = 0;

    for (std::map<std::string, ServerConfig::Location>::const_iterator it =
             cfg.location.begin(); it != cfg.location.end(); ++it) {
        const std::string &path = it->first;
        if (uri.compare(0, path.size(), path) == 0) { // prefix match
            if (path.size() > longest) {
                longest = path.size();
                bestMatch = &(it->second);
            }
        }
    }
    return bestMatch;
}

void Server::sendPayloadTooLarge(int fd) {
    std::string body = "<html><body><h1>413 Payload Too Large</h1></body></html>";
    std::ostringstream oss;
    oss << "HTTP/1.1 413 Payload Too Large\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    queueSend(fd, oss.str());
}

void Server::handlePost(int clientFd, const Request &req, const ServerConfig::Location* loc) {
    std::cout << "[INFO] Handling POST for URI: " << req.uri << std::endl;
    // method許可確認 saitoさんのコードをマージしたら、buildMethodNotAllowed(allow, cfg)が使えるようになるはず。その後にコメントアウトを外す
    // if (loc && !loc->method.empty()) {
    //     if (std::find(loc->method.begin(), loc->method.end(), "POST") == loc->method.end()) {
    //         // ResponseBuilderを使って405を送信
    //         ResponseBuilder rb;
    //         std::string allow = joinMethods(loc->method); // "GET, HEAD, DELETE" など
    //         std::string res = rb.buildMethodNotAllowed(allow, cfg);
    //         queueSend(clientFd, res);
    //         return;
    //     }
    // }

    // -----------------------------
    // Content-Type 分岐
    // -----------------------------
    std::string contentType;
    if (req.headers.find("Content-Type") != req.headers.end())
        contentType = req.headers.at("Content-Type");
    else
        contentType = "";
    if (contentType.find("application/x-www-form-urlencoded") != std::string::npos) {
        handleUrlEncodedForm(clientFd, req);
    }
    else if (contentType.find("multipart/form-data") != std::string::npos) {
        handleMultipartForm(clientFd, req, loc);
    }
    else {
        // 未対応Content-Type
        std::ostringstream body;
        body << "<html><body><h1>415 Unsupported Media Type</h1></body></html>";
        std::ostringstream res;
        res << "HTTP/1.1 415 Unsupported Media Type\r\n"
            << "Content-Type: text/html\r\n"
            << "Content-Length: " << body.str().size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << body.str();
        queueSend(clientFd, res.str());
    }
}

// std::string Server::joinMethods(const std::vector<std::string>& methods) {
//     std::string s;
//     for (size_t i = 0; i < methods.size(); ++i) {
//         if (i > 0) s += ", ";
//         s += methods[i];
//     }
//     return s;
// }

// -----------------------------
// 共通チャンク送信補助
// -----------------------------
void Server::queueSendChunk(int fd, const std::string &data) {
    std::ostringstream chunk;
    chunk << std::hex << data.size() << "\r\n" << data << "\r\n";
    queueSend(fd, chunk.str());
}

// -----------------------------
// URLエンコードフォーム対応
// -----------------------------
void Server::handleUrlEncodedForm(int clientFd, const Request &req) {
    std::map<std::string, std::string> formData = parseUrlEncoded(req.body);

    // ヘッダ送信
    std::ostringstream hdr;
    hdr << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: text/html\r\n"
        << "Transfer-Encoding: chunked\r\n\r\n";
    queueSend(clientFd, hdr.str());

    // ボディ送信
    std::ostringstream body;
    body << "<html><body><h1>Form Received</h1><ul>";
    queueSendChunk(clientFd, body.str());

    for (std::map<std::string,std::string>::iterator it = formData.begin(); 
         it != formData.end(); ++it) {
        std::ostringstream li;
        li << "<li>" << it->first << " = " << it->second << "</li>";
        queueSendChunk(clientFd, li.str());
    }

    body.str(""); body.clear();
    body << "</ul></body></html>";
    queueSendChunk(clientFd, body.str());

    // 最終チャンク
    queueSend(clientFd, "0\r\n\r\n");
}

// -----------------------------
// multipartフォーム対応
// -----------------------------
static std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b; // a が空なら b を返す
    if (a[a.size()-1] == '/' && !b.empty() && b[0] == '/') {
        return a + b.substr(1); // 両方スラッシュなら b の先頭を削除
    }
    if (a[a.size()-1] != '/' && !b.empty() && b[0] != '/') {
        return a + "/" + b; // 両方スラッシュなしなら間に追加
    }
    return a + b; // それ以外はそのまま結合
}

void Server::handleMultipartForm(int clientFd, const Request &req, const ServerConfig::Location* loc) {
    std::string contentType = req.headers.at("Content-Type");
    std::vector<FilePart> files = parseMultipart(contentType, req.body);

    // ヘッダ送信
    std::ostringstream hdr;
    hdr << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: text/html\r\n"
        << "Transfer-Encoding: chunked\r\n\r\n";
    queueSend(clientFd, hdr.str());

    // ボディ開始
    std::ostringstream body;
    body << "<html><body><h1>Files Uploaded</h1><ul>";
    queueSendChunk(clientFd, body.str());

    std::string baseUploadPath;

    // 1. location の upload_path
    if (loc && !loc->upload_path.empty()) {
        baseUploadPath = loc->upload_path;
    }
    // 2. location の root
    else if (loc && !loc->root.empty()) {
        baseUploadPath = joinPath(loc->root, req.uri);
    }
    // 3. サーバーデフォルト root
    else if (!cfg.root.empty()) {
        baseUploadPath = joinPath(cfg.root, req.uri);
    }
    // 4. どれも無ければ一時ディレクトリ
    else {
        baseUploadPath = "/tmp/webserv_upload/";
    }


    for (size_t i = 0; i < files.size(); ++i) {
        std::string safeName = sanitizeFileName(files[i].filename);
        std::string savePath = baseUploadPath + "/" + safeName;

        std::ofstream ofs(savePath.c_str(), std::ios::binary);
        if (!ofs.is_open()) {
            sendInternalServerError(clientFd);
            return;
        }
        //saitoさんのコードをマージしたら、上記の簡単なエラー処理をではなく、下記のコメントアウト部分のようにlocation/サーバーのカスタム500ページ対応を実装する
        // std::ofstream ofs(savePath.c_str(), std::ios::binary);
        // if (!ofs.is_open()) {
        //     // location/サーバーのカスタム500ページ対応
        //     if (loc && loc->ret.count(500))
        //         queueSend(clientFd, buildOkResponseFromFile(loc->ret.at(500), false, true));
        //     else if (cfg.errorPages.count(500))
        //         queueSend(clientFd, buildOkResponseFromFile(cfg.errorPages.at(500), false, true));
        //     else
        //         sendInternalServerError(clientFd);
        //     return;
        // }
        ofs.write(files[i].content.c_str(), files[i].content.size());
        ofs.close();

        std::ostringstream li;
        li << "<li>" << safeName << " (" << files[i].content.size() << " bytes)</li>";
        queueSendChunk(clientFd, li.str());
    }

    body.str(""); body.clear();
    body << "</ul></body></html>";
    queueSendChunk(clientFd, body.str());

    // 最終チャンク
    queueSend(clientFd, "0\r\n\r\n");
}

// -----------------------------
// ファイル名安全化補助
// -----------------------------
std::string Server::sanitizeFileName(const std::string &filename) {
    std::string safe = filename;
    for (size_t j = 0; j < safe.size(); ++j)
        if (safe[j] == '/' || safe[j] == '\\') safe[j] = '_';

    size_t pos;
    while ((pos = safe.find("..")) != std::string::npos)
        safe.replace(pos, 2, "__");

    std::ostringstream uniqueName;
    uniqueName << time(NULL) << "_" << safe;
    return uniqueName.str();
}

// -----------------------------
// multipart解析（現状のまま、ファイル名は後で安全化）
// -----------------------------
std::vector<FilePart> Server::parseMultipart(const std::string &contentType,
                                             const std::string &body) {
    std::vector<FilePart> parts;
    size_t bpos = contentType.find("boundary=");
    if (bpos == std::string::npos) return parts;
    std::string boundary = contentType.substr(bpos + 9);

    std::string delimiter = "--" + boundary;
    size_t pos = 0, end;
    while ((pos = body.find(delimiter, pos)) != std::string::npos) {
        pos += delimiter.length();
        if (body.compare(pos, 2, "--") == 0) break;
        if (body.compare(pos, 2, "\r\n") == 0) pos += 2;

        end = body.find(delimiter, pos);
        std::string part = body.substr(pos, end - pos);

        size_t headerEnd = part.find("\r\n\r\n");
        if (headerEnd == std::string::npos) continue;

        std::string headers = part.substr(0, headerEnd);
        std::string content = part.substr(headerEnd + 4);
        if (content.size() >= 2 && content.substr(content.size()-2) == "\r\n")
            content.erase(content.size()-2);

        size_t namePos = headers.find("name=\"");
        if (namePos == std::string::npos) continue;
        namePos += 6;
        size_t nameEnd = headers.find("\"", namePos);
        std::string name = headers.substr(namePos, nameEnd - namePos);

        FilePart fp;
        fp.name = name;

        size_t filePos = headers.find("filename=\"");
        if (filePos != std::string::npos) {
            filePos += 10;
            size_t fileEnd = headers.find("\"", filePos);
            fp.filename = headers.substr(filePos, fileEnd - filePos);
            fp.content = content;
        } else {
            fp.filename = "";
            fp.content = content;
        }
        parts.push_back(fp);
        pos = end;
    }
    return parts;
}

std::string Server::urlDecode(const std::string &str) {
    std::string result;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '+') {
            result += ' ';
        } else if (str[i] == '%' && i + 2 < str.size()) {
            char hex[3] = { str[i + 1], str[i + 2], '\0' };
            if (isxdigit(hex[0]) && isxdigit(hex[1])) {
                char decoded = static_cast<char>(strtol(hex, NULL, 16));
                result += decoded;
                i += 2;
            } else {
                result += '%'; // 不正な形式ならそのまま追加
            }
        } else {
            result += str[i];
        }
    }
    return result;
}

// URLエンコード解析
std::map<std::string,std::string> Server::parseUrlEncoded(const std::string &body) {
    std::map<std::string,std::string> data;
    std::istringstream ss(body);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string key = urlDecode(pair.substr(0, eq));
            std::string value = urlDecode(pair.substr(eq+1));
            data[key] = value;
        }
    }
    return data;
}


// --- ヘルパー：パスから拡張子を取得（ドットを含めて返す） ---
static std::string getExtension(const std::string &path) {
    // 最後のスラッシュ位置（ファイル名の先頭）
    size_t lastSlash = path.find_last_of('/');
    // 最後のドット位置
    size_t lastDot = path.find_last_of('.');
    if (lastDot == std::string::npos) {
        // ドットが無い
        return "";
    }
    // ドットがスラッシュより前なら拡張子ではない（例: /dir.name/file）
    if (lastSlash != std::string::npos && lastDot < lastSlash) {
        return "";
    }
    // 安全に substr を使う（dot を含めて返す）
    return path.substr(lastDot);
}

bool Server::isCgiRequest(const Request &req) {
    if (req.uri.size() < 4) return false;
    std::string ext = getExtension(req.uri);
    return (ext == ".php");
}

// ----------------------------
// CGI実行用関数
// ----------------------------

void Server::startCgiProcess(int clientFd, const Request &req) {
    int inPipe[2], outPipe[2];
    if (pipe(inPipe) < 0 || pipe(outPipe) < 0) return;

    pid_t pid = fork();
    if (pid == 0) { // --- 子プロセス ---
        dup2(inPipe[0], STDIN_FILENO);
        dup2(outPipe[1], STDOUT_FILENO);
        close(inPipe[1]); close(outPipe[0]);
        setenv("REQUEST_METHOD", req.method.c_str(), 1);
        std::ostringstream len;
        len << req.body.size();
        setenv("CONTENT_LENGTH", len.str().c_str(), 1);
        std::string scriptPath = root + req.uri;  // 例: /var/www/html/test.php
        setenv("SCRIPT_FILENAME", scriptPath.c_str(), 1);
        setenv("REDIRECT_STATUS", "200", 1);
        char *argv[] = { (char*)"php-cgi", NULL };
        execve("/usr/bin/php-cgi", argv, environ);
        // exit/_exit が使えないので失敗時は何もしない
        // 子プロセスはそのまま残るが課題上は無視して良い
    }

    // --- 親プロセス ---
    close(inPipe[0]);
    close(outPipe[1]);

    // 非ブロッキング設定
    fcntl(outPipe[0], F_SETFL, O_NONBLOCK);

    // クライアント→CGI 入力送信
    if (!req.body.empty()) write(inPipe[1], req.body.c_str(), req.body.size());
    close(inPipe[1]);

    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) { // 子プロセスはすでに終了
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            // 子が異常終了していた → CGIエラー扱い
            sendInternalServerError(clientFd);
            return;
        }
    }

    // poll 監視に追加
    struct pollfd pfd;
    pfd.fd = outPipe[0];
    pfd.events = POLLIN;
    fds[nfds++] = pfd;  // nfds は現在の要素数

    // 管理マップに登録
    CgiProcess proc;
    proc.clientFd = clientFd;
    proc.pid = pid;
    proc.outFd = outPipe[0];
    cgiMap[outPipe[0]] = proc;
}

void Server::sendInternalServerError(int clientFd) {
    std::ostringstream body;
    body << "<html><body>"
         << "<h1>500 Internal Server Error</h1>"
         << "<p>The server encountered an unexpected condition.</p>"
         << "</body></html>";

    std::ostringstream res;
    res << "HTTP/1.1 500 Internal Server Error\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.str().size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body.str();

    queueSend(clientFd, res.str());
}

void Server::handleCgiOutput(int fd) {
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf));

    if (n > 0) {
        CgiProcess &proc = cgiMap[fd];
        proc.buffer.append(buf, n);  // 受信データをバッファに追加

        // --- 最初のヘッダ送信 ---
        if (!proc.headerSent) {
            size_t headerEnd = proc.buffer.find("\r\n\r\n");
            std::ostringstream hdr;

            if (headerEnd != std::string::npos) {
                // CGIがHTTPヘッダを送ってきた場合
                std::string cgiHdr = proc.buffer.substr(0, headerEnd + 4);
                hdr << cgiHdr
                    << "Transfer-Encoding: chunked\r\n";
                queueSend(proc.clientFd, hdr.str());
                proc.buffer.erase(0, headerEnd + 4);
            } else {
                // ヘッダなし → 明示的に chunked で返す
                hdr << "HTTP/1.1 200 OK\r\n"
                    << "Content-Type: text/html\r\n"
                    << "Transfer-Encoding: chunked\r\n\r\n";
                queueSend(proc.clientFd, hdr.str());
            }
            proc.headerSent = true;
        }

        // --- データを chunked 形式で送信 ---
        if (!proc.buffer.empty()) {
            std::ostringstream chunk;
            chunk << std::hex << proc.buffer.size() << "\r\n"
                  << proc.buffer << "\r\n";
            queueSend(proc.clientFd, chunk.str());
            proc.buffer.clear();
        }

    } else if (n == 0) {
        // --- EOF: 最終チャンク送信 ---
        CgiProcess &proc = cgiMap[fd];
        queueSend(proc.clientFd, "0\r\n\r\n");

        close(fd);
        waitpid(proc.pid, NULL, 0);
        cgiMap.erase(fd);

    } else {
        // --- read エラー時は 500 を返して CGI 処理を中止 ---
        CgiProcess &proc = cgiMap[fd];
        std::ostringstream body;
        body << "<html><body><h1>500 Internal Server Error</h1>"
             << "<p>CGI read failed.</p></body></html>";
        std::ostringstream res;
        res << "HTTP/1.1 500 Internal Server Error\r\n"
            << "Content-Type: text/html\r\n"
            << "Content-Length: " << body.str().size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << body.str();
        queueSend(proc.clientFd, res.str());

        close(fd);
        waitpid(proc.pid, NULL, 0);
        cgiMap.erase(fd);
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
    if (clients[fd].sendBuffer.empty() && clients[fd].shouldClose) {
        close(fd);
        clients.erase(fd);
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
        return;
    }

    // 🔹 CGI出力ファイルディスクリプタなら
    if (cgiMap.count(fd)) {
        handleCgiOutput(fd);
        return;
    }

    // 🔹 通常クライアント
    int idx = findIndexByFd(fd);
    if (revents & POLLIN) handleClient(idx);
    if (revents & POLLOUT) handleClientSend(idx);
}

// fdからindexを見つける補助関数
int Server::findIndexByFd(int fd) {
    for (int i = 0; i < nfds; ++i) {
        if (fds[i].fd == fd)
            return i;
    }
    return -1;
}

//-------------------------------------------
void Server::checkCgiTimeouts(int maxLoops) {
    std::map<int, CgiProcess>::iterator it = cgiMap.begin();
    while (it != cgiMap.end()) {
        it->second.elapsedLoops++;  // poll 1回ごとにカウント
        if (it->second.elapsedLoops > maxLoops) {
            // タイムアウトした CGI を強制終了
            kill(it->second.pid, SIGKILL);

            // 504 Gateway Timeout を返す
            sendGatewayTimeout(it->second.clientFd);

            // CGI 出力用の fd を閉じる
            close(it->second.outFd);

            // 子プロセスを回収
            waitpid(it->second.pid, NULL, 0);

            // map から削除
            std::map<int, CgiProcess>::iterator tmp = it;
            ++it; // 次の要素へ
            cgiMap.erase(tmp); // erase は void なので注意
        } else {
            ++it;
        }
    }
}

void Server::sendGatewayTimeout(int clientFd) {
    std::ostringstream body;
    body << "<html><body>"
         << "<h1>504 Gateway Timeout</h1>"
         << "<p>The CGI script did not respond in time.</p>"
         << "</body></html>";

    std::ostringstream res;
    res << "HTTP/1.1 504 Gateway Timeout\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.str().size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body.str();

    queueSend(clientFd, res.str());
}