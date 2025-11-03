#include "Server.hpp"

#include <sys/wait.h>

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

#include "CgiProcess.hpp"
#include "RequestParser.hpp"
#include "log.hpp"
#include "resp/ResponseBuilder.hpp"

// ----------------------------
// コンストラクタ・デストラクタ
// ----------------------------

// サーバー初期化（ポート指定）
Server::Server(const ServerConfig& c)
    : cfg(c),
      serverFd(-1),
      nfds(1),
      port(c.port),
      host(c.host),
      root(c.root),
      errorPages(c.errorPages) {}

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
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        logMessage(ERROR,
                   std::string("setsockopt() failed: ") + strerror(errno));
        perror("setsockopt");
        return false;
    }
    int flags = fcntl(serverFd, F_GETFL, 0);
    if (flags == -1) {
        logMessage(ERROR,
                   std::string("fcntl(F_GETFL) failed: ") + strerror(errno));
        perror("fcntl get");
        return false;
    }

    if (fcntl(serverFd, F_SETFL, flags | O_NONBLOCK) == -1) {
        logMessage(ERROR,
                   std::string("fcntl(O_NONBLOCK) failed: ") + strerror(errno));
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
// pollイベント処理
// ----------------------------

// fdからindexを見つける補助関数
int Server::findIndexByFd(int fd) {
    for (int i = 0; i < nfds; ++i) {
        if (fds[i].fd == fd)
            return i;
    }
    return -1;
}

int Server::getServerFd() const { return serverFd; }
const std::map<int, CgiProcess>& Server::getCgiMap() const { return cgiMap; }
const std::map<int, ClientInfo>& Server::getClients() const { return clients; }
bool Server::isServerSocket(int fd) const { return fd == serverFd; }
bool Server::isClientFd(int fd) const {
    return clients.find(fd) != clients.end();
}
bool Server::isCgiStdoutFd(int fd) const {
    return cgiMap.find(fd) != cgiMap.end();
}

void Server::handlePollIn(int fd) {
    if (isServerSocket(fd)) {
        handleNewConnection();
        return;
    }

    if (isCgiStdoutFd(fd)) {
        handleCgiReadable(fd);
        return;
    }

    if (isClientFd(fd)) {
        int index = findIndexByFd(fd);
        if (index >= 0)
            handleClient(index);  // ← 既存関数をそのまま呼ぶ
    }
}

void Server::handlePollOut(int fd) {
    if (isClientFd(fd)) {
        handleClientSend(fd);
    }
    // else if (isCgiInFd(fd)) {
    //     handleCgiWritable(fd);
    // }
}

// ----------------------------
// クライアント接続処理
// ----------------------------

// 新規接続ハンドラ
void Server::handleNewConnection() {
    int clientFd = acceptClient();
    if (clientFd < 0)
        return;  // accept 失敗時は何もしない

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
        logMessage(ERROR, std::string("fcntl(F_GETFL client) failed: ") +
                              strerror(errno));
        perror("fcntl get client");
        close(clientFd);
        return -1;
    }
    if (fcntl(clientFd, F_SETFL, flags | O_NONBLOCK) == -1) {
        logMessage(ERROR, std::string("fcntl(O_NONBLOCK client) failed: ") +
                              strerror(errno));
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

    // もしヘッダ解析済みなら max_body_size チェック
    Request& req = clients[fd].currentRequest;
    LocationMatch m = getLocationForUri(req.uri);
    const ServerConfig::Location* loc = m.loc;

    if (loc && clients[fd].receivedBodySize + bytes >
                   static_cast<size_t>(loc->max_body_size)) {
        std::ostringstream res;
        res << "HTTP/1.1 413 Payload Too Large\r\nContent-Length: "
            << clients[fd].receivedBodySize + bytes << "\r\n\r\n";
        queueSend(fd, res.str());
        // handleDisconnect(fd, index, bytes);
        return;
    }

    // 累積ボディサイズを更新
    clients[fd].receivedBodySize += req.body.size();

    // 1リクエストずつ処理
    while (true) {
        std::string requestStr = extractNextRequest(clients[fd].recvBuffer,
                                                    clients[fd].currentRequest);
        if (requestStr.empty())
            break;

        Request& req = clients[fd].currentRequest;
        LocationMatch m = getLocationForUri(req.uri);
        const ServerConfig::Location* loc = m.loc;
        const std::string& locPath = m.path;

        // 1リクエスト分の body が max_body_size を超えていないかチェック
        if (!checkMaxBodySize(fd, req.body.size(), loc)) {
            // handleDisconnect(fd, index, 0);
            break;
        }

        printf("Request complete from fd=%d\n", fd);

        // メソッド許可チェック
        if (!handleMethodCheck(fd, req, loc, requestStr.size()))
            continue;

        // --- リダイレクト処理 ---
        if (handleRedirect(fd, loc)) {
            // redirect を queueSend したらこのリクエスト処理は完了
            // ループを抜けて次の recv まで待つ
            break;
        }

        // CGI / POST / GET 処理
        processRequest(fd, req, loc, locPath, requestStr.size());
    }
}

bool Server::handleRedirect(int fd, const ServerConfig::Location* loc) {
    if (!loc || loc->ret.empty())
        return false;  // 続行してOK

    std::map<int, std::string>::const_iterator it = loc->ret.begin();
    int code = it->first;
    const std::string& target = it->second;

    std::ostringstream res;
    res << "HTTP/1.1 " << code << " Moved Permanently\r\n"
        << "Location: " << target << "\r\n"
        << "Content-Length: 0\r\n"
        << "Connection: close\r\n\r\n";

    queueSend(fd, res.str());  // Server 内の関数を呼ぶ

    return true;
}

// Server.cpp に実装
bool Server::checkMaxBodySize(int fd, int bytes,
                              const ServerConfig::Location* loc) {
    if (!loc)
        return true;

    clients[fd].receivedBodySize += bytes;
    if ((static_cast<size_t>(loc->max_body_size) != 0) &&
        (clients[fd].receivedBodySize >
         static_cast<size_t>(loc->max_body_size))) {
        std::ostringstream res;
        res << "HTTP/1.1 413 Payload Too Large\r\nContent-Length: " << bytes
            << "\r\n\r\n";
        queueSend(fd, res.str());
        clients[fd].recvBuffer.clear();
        return false;  // 超過
    }
    return true;
}

bool Server::handleMethodCheck(int fd, Request& req,
                               const ServerConfig::Location* loc,
                               size_t reqSize) {
    // 実装済みのMethodかチェック。PUTは未実装なので501で返す。
    if (req.method != "GET" && req.method != "POST" && req.method != "DELETE" &&
        req.method != "HEAD") {
        queueSend(fd, "HTTP/1.1 501 Not Implemented\r\n");
        clients[fd].recvBuffer.erase(0, reqSize);
        return false;
    }
    if (!isMethodAllowed(req.method, loc)) {
        queueSend(
            fd, "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n");
        clients[fd].recvBuffer.erase(0, reqSize);
        return false;
    }
    return true;
}

void Server::processRequest(int fd, Request& req,
                            const ServerConfig::Location* loc,
                            const std::string& locPath, size_t reqSize) {
    if (isCgiRequest(req)) {
        startCgiProcess(fd, req, *loc);
    } else if (req.method == "POST") {
        handlePost(fd, req, loc);
    } else {
        ResponseBuilder rb;
        queueSend(fd, rb.generateResponse(req, cfg, loc, locPath));
    }
    clients[fd].recvBuffer.erase(0, reqSize);
}

std::string generateUniqueFilename() {
    // 現在時刻を取得
    std::time_t t = std::time(NULL);
    std::tm* now = std::localtime(&t);

    std::ostringstream oss;

    // 年月日_時分秒（ゼロ埋め）
    oss.fill('0');
    oss << (now->tm_year + 1900) << std::setw(2) << (now->tm_mon + 1)
        << std::setw(2) << now->tm_mday << "_" << std::setw(2) << now->tm_hour
        << std::setw(2) << now->tm_min << std::setw(2) << now->tm_sec;

    // 乱数付与（0〜9999）
    int randNum = std::rand() % 10000;
    oss << "_" << randNum;

    // ファイル拡張子
    oss << ".txt";

    return oss.str();
}

std::string buildHttpResponse(int statusCode, const std::string& body,
                              const std::string& contentType = "text/plain") {
    std::stringstream ss;
    ss << "HTTP/1.1 " << statusCode << " "
       << (statusCode == 201   ? "Created"
           : statusCode == 403 ? "Forbidden"
           : statusCode == 500 ? "Internal Server Error"
                               : "")
       << "\r\n";
    ss << "Content-Length:" << body.size() << "\r\n";
    ss << "Content-Type: " << contentType << "\r\n\r\n";
    ss << body;
    return ss.str();
}

void Server::handlePost(int fd, Request& req,
                        const ServerConfig::Location* loc) {
    std::string contentType;
    if (req.headers.find("content-type") != req.headers.end()) {
        contentType = req.headers.at("content-type");
    } else {
        contentType = "";
    }

    bool isChunked = false;
    std::map<std::string, std::string>::iterator it =
        req.headers.find("transfer-encoding");
    if (it != req.headers.end() &&
        it->second.find("chunked") != std::string::npos)
        isChunked = true;
    if (isChunked) {
        handleChunkedBody(fd, req, loc);
        return;
    }

    if (contentType.find("application/x-www-form-urlencoded") !=
        std::string::npos) {
        handleUrlEncodedForm(fd, req, loc);
        return;
    } else if (contentType.find("multipart/form-data") != std::string::npos) {
        handleMultipartForm(fd, req, loc);
    } else {
        std::string body = "Unsupported Content-Type: " + contentType + "\n";
        queueSend(fd, buildHttpResponse(415, body));
    }
}

void saveBodyToFile(const std::string& body, const std::string& uploadDir) {
    // 現在時刻
    std::time_t t = std::time(NULL);
    std::tm tm = *std::localtime(&t);

    // ファイル名生成
    std::ostringstream oss;
    oss << uploadDir;
    if (!uploadDir.empty() && uploadDir[uploadDir.size() - 1] != '/' &&
        uploadDir[uploadDir.size() - 1] != '\\')
        oss << '/';

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    oss << "POST_" << buf;

    static int counter = 0;
    oss << "_" << counter++ << ".txt";

    std::string filename = oss.str();

    std::ofstream ofs(filename.c_str(), std::ios::binary);
    if (!ofs.is_open()) {
        std::cerr << "Failed to open file for writing: " << filename
                  << std::endl;
        return;
    }

    ofs.write(body.c_str(), body.size());
    ofs.close();

    std::cout << "[INFO] Saved POST body to: " << filename << std::endl;
}

void Server::handleChunkedBody(int fd, Request& req,
                               const ServerConfig::Location* loc) {
    // すでに unchunk された req.body を使って処理
    // 例: ファイル保存や CGI に渡すなど
    if (loc->upload_path.empty()) {
        queueSend(fd, buildHttpResponse(200, "Chunked data received\n"));
    } else {
        saveBodyToFile(req.body, loc->upload_path);
        queueSend(fd, buildHttpResponse(201, "File saved\n"));
    }
}

// URLデコード用
std::string urlDecode(const std::string& str) {
    std::string ret;
    char hex[3] = {0};
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '+') {
            ret += ' ';
        } else if (str[i] == '%' && i + 2 < str.size()) {
            hex[0] = str[i + 1];
            hex[1] = str[i + 2];
            ret += static_cast<char>(strtol(hex, NULL, 16));
            i += 2;
        } else {
            ret += str[i];
        }
    }
    return ret;
}

// x-www-form-urlencoded を処理する関数
void Server::handleUrlEncodedForm(int fd, Request& req,
                                  const ServerConfig::Location* loc) {
    std::map<std::string, std::string> formData;
    std::stringstream ss(req.body);
    std::string pair;

    // key=value に分解
    while (std::getline(ss, pair, '&')) {
        size_t eqPos = pair.find('=');
        if (eqPos != std::string::npos) {
            std::string key = urlDecode(pair.substr(0, eqPos));
            std::string value = urlDecode(pair.substr(eqPos + 1));
            formData[key] = value;
        }
    }

    // ファイル保存パスが設定されていれば保存
    if (!loc->upload_path.empty()) {
        std::ostringstream filenameStream;
        filenameStream << loc->upload_path << "/form_" << time(NULL) << ".txt";
        std::string filename = filenameStream.str();

        std::ofstream ofs(filename.c_str());
        if (!ofs) {
            std::string res = buildHttpResponse(500, "Internal Server Error\n");
            queueSend(fd, res);
            return;
        }

        for (std::map<std::string, std::string>::const_iterator it =
                 formData.begin();
             it != formData.end(); ++it) {
            ofs << it->first << "=" << it->second << "\n";
        }
        ofs.close();
    }

    std::string responseBody = "Form received successfully\n";
    std::string res = buildHttpResponse(201, responseBody);
    queueSend(fd, res);
}

std::string extractBoundary(const std::string& contentType) {
    std::string key = "boundary=";
    size_t pos = contentType.find(key);
    if (pos == std::string::npos)
        return "";
    return "--" + contentType.substr(pos + key.size());
}

std::vector<std::string> splitParts(const std::string& body,
                                    const std::string& boundary) {
    std::vector<std::string> parts;
    size_t start = 0, end;

    while ((end = body.find(boundary, start)) != std::string::npos) {
        std::string part = body.substr(start, end - start);

        // 末尾の余分な改行を削除
        if (part.size() >= 2 && part.substr(part.size() - 2) == "\r\n")
            part.erase(part.size() - 2);

        // 空文字や Content-Disposition を持たないパートは無視
        if (!part.empty() &&
            part.find("Content-Disposition") != std::string::npos) {
            parts.push_back(part);
        }

        start = end + boundary.size();
        if (body.substr(start, 2) == "--")
            break;  // 終端ならループ終了
    }

    return parts;
}

void parsePart(const std::string& part, std::string& filename,
               std::string& content) {
    size_t headerEnd = part.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return;

    std::string header = part.substr(0, headerEnd);
    content = part.substr(headerEnd + 4);

    // filename 抽出
    size_t pos = header.find("filename=\"");
    if (pos != std::string::npos) {
        pos += 10;
        size_t end = header.find("\"", pos);
        filename = header.substr(pos, end - pos);
    } else {
        filename = "upload.bin";
    }
}

void Server::handleMultipartForm(int fd, Request& req,
                                 const ServerConfig::Location* loc) {
    std::cerr << "=== Multipart Raw Body ===\n"
              << req.body << "\n=========================\n";

    if (loc->upload_path.empty()) {
        queueSend(fd, buildHttpResponse(403, "Upload path not configured.\n"));
        return;
    }

    std::string boundary = extractBoundary(req.headers["content-type"]);
    if (boundary.empty()) {
        queueSend(
            fd, buildHttpResponse(400, "Missing boundary in Content-Type.\n"));
        return;
    }

    std::vector<std::string> parts = splitParts(req.body, boundary);
    if (parts.empty()) {
        queueSend(fd, buildHttpResponse(400, "No multipart data found.\n"));
        return;
    }

    for (size_t i = 0; i < parts.size(); ++i) {
        std::string filename, content;
        parsePart(parts[i], filename, content);
        std::string fullpath = loc->upload_path + "/" + filename;

        std::ofstream ofs(fullpath.c_str(), std::ios::binary);
        if (!ofs) {
            queueSend(fd, buildHttpResponse(500, "Failed to open file.\n"));
            return;
        }
        ofs.write(content.data(), content.size());
        ofs.close();
    }

    queueSend(fd, buildHttpResponse(201, "File uploaded successfully.\n"));
}

bool Server::isMethodAllowed(const std::string& method,
                             const ServerConfig::Location* loc) {
    if (!loc)
        return false;
    for (size_t i = 0; i < loc->method.size(); i++) {
        if (loc->method[i] == method)
            return true;
    }
    return false;
}

std::string normalizePath(const std::string& path) {
    if (path == "/")
        return "/";  // ルートはそのまま
    if (!path.empty() && path[path.size() - 1] == '/')
        return path.substr(0, path.size() - 1);
    return path;
}

Server::LocationMatch Server::getLocationForUri(const std::string& uri) const {
    LocationMatch bestMatch;
    size_t bestLen = 0;

    std::string normUri = normalizePath(uri);

    for (std::map<std::string, ServerConfig::Location>::const_iterator it =
             cfg.location.begin();
         it != cfg.location.end(); ++it) {
        std::string normLoc = normalizePath(it->first);
        if (normLoc.empty())
            normLoc = "/";
        if (normUri.compare(0, normLoc.size(), normLoc) == 0) {
            if (normLoc.size() > bestLen) {
                bestLen = normLoc.size();
                bestMatch.loc = &it->second;
                bestMatch.path = it->first;  // 元のパスはそのまま
            }
        }
    }
    return bestMatch;
}

bool Server::isCgiRequest(const Request& req) {
    // パーサー未実装 → loc に書き込まず、直接比較文字列を使用
    const std::string cgiExt = ".php";

    // 1. クエリストリングを落とす (/foo.php?x=1 -> /foo.php)
    std::string uri = req.uri;
    size_t q = uri.find('?');
    if (q != std::string::npos) {
        uri = uri.substr(0, q);
    }

    // 2. 最後の '.' を探す
    size_t dot = uri.find_last_of('.');
    if (dot == std::string::npos) {
        // 拡張子が無い → CGIじゃない
        return false;
    }

    std::string ext = uri.substr(dot);  // ".php" とか
    return (ext == cgiExt);             // いまはPHPだけCGI扱い
}

// ----------------------------
// CGI実行用関数
// ----------------------------

std::pair<std::string, std::string> splitUri(const std::string& uri) {
    size_t pos = uri.find('?');
    if (pos == std::string::npos) {
        return std::make_pair(uri, "");
    } else {
        return std::make_pair(uri.substr(0, pos), uri.substr(pos + 1));
    }
}

// 外部関数（Serverクラス外でも良い）
std::pair<std::string, std::string> buildCgiScriptPath(
    const std::string& uri, const ServerConfig::Location& loc,
    const std::map<std::string, ServerConfig::Location>& locations) {
    std::pair<std::string, std::string> parts = splitUri(uri);
    std::string path_only = parts.first;
    std::string query_str = parts.second;

    std::string scriptPath = loc.root;
    if (!scriptPath.empty() && scriptPath[scriptPath.size() - 1] == '/')
        scriptPath.erase(scriptPath.size() - 1);

    // location キーを探す
    std::string locKey;
    for (std::map<std::string, ServerConfig::Location>::const_iterator it =
             locations.begin();
         it != locations.end(); ++it) {
        if (&it->second == &loc)
            locKey = it->first;
    }

    if (path_only.find(locKey) == 0) {
        std::string rest = path_only.substr(locKey.length());
        if (!rest.empty() && rest[0] != '/')
            scriptPath += '/';
        scriptPath += rest;
    } else {
        scriptPath += path_only;
    }

    return std::make_pair(scriptPath, query_str);
}

// env 設定を作る関数
std::map<std::string, std::string> buildCgiEnv(
    const Request& req, const ServerConfig::Location& loc,
    const std::map<std::string, ServerConfig::Location>& locations) {
    std::map<std::string, std::string> env;

    env["REQUEST_METHOD"] = req.method;

    std::ostringstream len;
    len << req.body.size();
    env["CONTENT_LENGTH"] = len.str();

    std::pair<std::string, std::string> envPaths =
        buildCgiScriptPath(req.uri, loc, locations);
    env["SCRIPT_FILENAME"] = envPaths.first;
    env["QUERY_STRING"] = envPaths.second;
    env["REDIRECT_STATUS"] = "200";

    return env;
}

// 子プロセス側の設定・exec
void executeCgiChild(int inFd, int outFd, const std::string& cgiPath,
                     const std::map<std::string, std::string>& env) {
    dup2(inFd, STDIN_FILENO);
    dup2(outFd, STDOUT_FILENO);
    close(inFd);
    close(outFd);

    for (std::map<std::string, std::string>::const_iterator it = env.begin();
         it != env.end(); ++it)
        setenv(it->first.c_str(), it->second.c_str(), 1);

    char* argv[] = {(char*)"php-cgi", NULL};
    execve(cgiPath.c_str(), argv, environ);
    exit(1);
}

// CGIプロセス開始と監視登録
void Server::startCgiProcess(int clientFd, const Request& req,
                             const ServerConfig::Location& loc) {
    int inPipe[2], outPipe[2];
    if (pipe(inPipe) < 0 || pipe(outPipe) < 0)
        return;

    pid_t pid = fork();
    if (pid == 0) {
        // 子プロセス側
        std::map<std::string, std::string> env =
            buildCgiEnv(req, loc, cfg.location);
        executeCgiChild(inPipe[0], outPipe[1], loc.cgi_path, env);
    }

    // 親プロセス側
    close(inPipe[0]);
    close(outPipe[1]);

    // リクエストボディをCGIに書き込む
    if (!req.body.empty()) {
        write(inPipe[1], req.body.c_str(), req.body.size());
    }
    close(inPipe[1]);  // 入力完了を通知

    // CGI出力を監視対象に登録
    registerCgiProcess(clientFd, pid, outPipe[0], cgiMap, fds, nfds);
}

// 親プロセス側でのパイプ送信と poll 登録
void Server::registerCgiProcess(int clientFd, pid_t pid, int outFd,
                                std::map<int, CgiProcess>& cgiMap, pollfd fds[],
                                int& nfds) {
    fcntl(outFd, F_SETFL, O_NONBLOCK);

    pollfd pfd;
    pfd.fd = outFd;
    pfd.events = POLLIN;
    fds[nfds++] = pfd;

    CgiProcess proc;
    proc.clientFd = clientFd;
    proc.pid = pid;
    proc.outFd = outFd;
    proc.buffer.clear();
    cgiMap[outFd] = proc;
}

// ----------------------------
// CGI出力読み取り処理
// ----------------------------

void Server::handleCgiReadable(int fd) {
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf));

    if (n > 0) {
        // CGI出力を蓄積
        cgiMap[fd].buffer.append(buf, n);
        return;
    } else if (n == 0) {
        // EOF: CGI出力完了
        int clientFd = cgiMap[fd].clientFd;

        // HTTPレスポンス生成
        std::string response = buildHttpResponseFromCgi(cgiMap[fd].buffer);

        // クライアント送信キューに追加
        queueSend(clientFd, response);

        // 後片付け
        close(fd);
        waitpid(cgiMap[fd].pid, NULL, 0);
        cgiMap.erase(fd);
    } else  // n < 0
    {
        // readエラー発生 → CGI異常扱いとして 500 を返す
        perror("read from CGI");

        int clientFd = cgiMap[fd].clientFd;

        std::string response =
            "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
        queueSend(clientFd, response);

        close(fd);
        waitpid(cgiMap[fd].pid, NULL, 0);
        cgiMap.erase(fd);
    }
}

std::string Server::buildHttpResponseFromCgi(const std::string& cgiOutput) {
    // CGIヘッダと本文を分離
    size_t headerEnd = cgiOutput.find("\r\n\r\n");
    std::string headers, content;
    if (headerEnd != std::string::npos) {
        headers = cgiOutput.substr(0, headerEnd);
        content = cgiOutput.substr(headerEnd + 4);
    } else {
        headers = cgiOutput;
    }

    // --- Statusヘッダを探す ---
    std::string statusLine = "HTTP/1.1 200 OK";  // デフォルト
    size_t statusPos = headers.find("Status:");
    if (statusPos != std::string::npos) {
        size_t lineEnd = headers.find("\r\n", statusPos);
        std::string statusValue =
            headers.substr(statusPos + 7, lineEnd - (statusPos + 7));
        // 前後の空白除去
        size_t start = statusValue.find_first_not_of(" \t");
        size_t end = statusValue.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos)
            statusValue = statusValue.substr(start, end - start + 1);
        statusLine = "HTTP/1.1 " + statusValue;
        // "Status:" 行はHTTPレスポンスヘッダには不要なので削除
        headers.erase(statusPos, lineEnd - statusPos + 2);
    }

    // --- HTTPレスポンスを組み立て ---
    std::ostringstream oss;
    oss << statusLine << "\r\n";
    oss << "Content-Length: " << content.size() << "\r\n";
    if (!headers.empty())
        oss << headers << "\r\n";
    oss << "\r\n";
    oss << content;

    return oss.str();
}

// ----------------------------
// クライアント送信処理
// ----------------------------

// クライアント送信バッファのデータ送信
void Server::handleClientSend(int fd) {
    ClientInfo& client = clients[fd];
    std::string& buf = client.sendBuffer;

    if (buf.empty())
        return;

    ssize_t sent = send(fd, buf.c_str(), buf.size(), 0);
    if (sent > 0)
        buf.erase(0, sent);
	// POLLOUTのON/OFFは buildPollEntries() が自動で制御するのでここでは何もしない
}

// 送信キューにデータを追加する関数
void Server::queueSend(int fd, const std::string& data) {
    std::map<int, ClientInfo>::iterator it = clients.find(fd);
    if (it != clients.end()) {
        it->second.sendBuffer += data;
        // ⚠ pollfd に触らない
        // 次の poll サイクルで buildPollEntries() が POLLOUT を設定してくれる
    }
}

// ----------------------------
// クライアント接続終了処理
// ----------------------------

// クライアント接続クローズ処理
void Server::handleConnectionClose(int fd) {
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
        close(fd);                   // ソケットを閉じる
        fds[index] = fds[nfds - 1];  // fds 配列の詰め替え
        nfds--;
        clients.erase(fd);  // clients から削除
    }
}

// ----------------------------
// ヘッダ解析・リクエスト処理
// ----------------------------

std::string Server::extractNextRequest(std::string& recvBuffer,
                                       Request& currentRequest) {
    RequestParser parser;
    if (!parser.isRequestComplete(recvBuffer)) {
        return "";
    }
    currentRequest = parser.parse(recvBuffer);
    // printRequest(currentRequest);
    if (currentRequest.method == "POST" &&
        currentRequest.headers.find("content-length") ==
            currentRequest.headers.end() &&
        currentRequest.headers.find("transfer-encoding") ==
            currentRequest.headers.end()) {
        int fd = findFdByRecvBuffer(recvBuffer);
        if (fd != -1) {
            std::string res =
                "HTTP/1.1 411 Length Required\r\n"
                "Content-Length: 0\r\n\r\n";
            queueSend(fd, res);
        }

        recvBuffer.erase(0, parser.getParsedLength());
        return "";
    }
    return recvBuffer.substr(0, parser.getParsedLength());
}

int Server::findFdByRecvBuffer(const std::string& buffer) const {
    for (std::map<int, ClientInfo>::const_iterator it = clients.begin();
         it != clients.end(); ++it) {
        if (&(it->second.recvBuffer) == &buffer) {
            return it->first;  // fd を返す
        }
    }
    return -1;  // 見つからなければ -1
}
