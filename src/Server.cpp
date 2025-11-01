#include "Server.hpp"
#include "RequestParser.hpp"
#include "log.hpp"
#include "resp/ResponseBuilder.hpp"
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/wait.h>
#include <utility>

// ----------------------------
// コンストラクタ・デストラクタ
// ----------------------------

// サーバー初期化（ポート指定）
Server::Server(const ServerConfig &c)
    : cfg(c), serverFd(-1), nfds(1), port(c.port), host(c.host), root(c.root),
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

  if (bind(serverFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
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
  if (clientFd < 0)
    return; // accept 失敗時は何もしない

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
    logMessage(ERROR,
               std::string("fcntl(F_GETFL client) failed: ") + strerror(errno));
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
  Request &req = clients[fd].currentRequest;
  LocationMatch m = getLocationForUri(req.uri);
  const ServerConfig::Location *loc = m.loc;

  if (loc && clients[fd].receivedBodySize + bytes >
                 static_cast<size_t>(loc->max_body_size)) {
    queueSend(fd,
              "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\n\r\n");
    handleDisconnect(fd, index, bytes);
    return;
  }

  // 累積ボディサイズを更新
  clients[fd].receivedBodySize += req.body.size();

  // 1リクエストずつ処理
  while (true) {
    std::string requestStr =
        extractNextRequest(clients[fd].recvBuffer, clients[fd].currentRequest);
    if (requestStr.empty())
      break;

    Request &req = clients[fd].currentRequest;
    LocationMatch m = getLocationForUri(req.uri);
    const ServerConfig::Location *loc = m.loc;
    const std::string &locPath = m.path;

    // 1リクエスト分の body が max_body_size を超えていないかチェック
    if (!checkMaxBodySize(fd, req.body.size(), loc)) {
      handleDisconnect(fd, index, 0);
      break;
    }

    printf("Request complete from fd=%d\n", fd);

    // メソッド許可チェック
    if (!handleMethodCheck(fd, req, loc, requestStr.size()))
      continue;

    // CGI / POST / GET 処理
    processRequest(fd, req, loc, locPath, requestStr.size());
  }
}

// Server.cpp に実装
bool Server::checkMaxBodySize(int fd, int bytes,
                              const ServerConfig::Location *loc) {
  if (!loc)
    return true;

  clients[fd].receivedBodySize += bytes;
  if ((static_cast<size_t>(loc->max_body_size) != 0) &&
      (clients[fd].receivedBodySize >
       static_cast<size_t>(loc->max_body_size))) {
    queueSend(fd,
              "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\n\r\n");
    clients[fd].recvBuffer.clear();
    return false; // 超過
  }
  return true;
}

bool Server::handleMethodCheck(int fd, Request &req,
                               const ServerConfig::Location *loc,
                               size_t reqSize) {
  if (!isMethodAllowed(req.method, loc)) {
    queueSend(fd,
              "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n");
    clients[fd].recvBuffer.erase(0, reqSize);
    return false;
  }
  return true;
}

void Server::processRequest(int fd, Request &req,
                            const ServerConfig::Location *loc,
                            const std::string &locPath, size_t reqSize) {
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
  std::tm *now = std::localtime(&t);

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

std::string buildHttpResponse(int statusCode, const std::string &body,
                              const std::string &contentType = "text/plain") {
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

void Server::handlePost(int fd, Request &req,
                        const ServerConfig::Location *loc) {
  std::string contentType;
  if (req.headers.find("Content-Type") != req.headers.end())
    contentType = req.headers.at("Content-Type");
  else
    contentType = "";

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

// URLデコード用
std::string urlDecode(const std::string &str) {
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
void Server::handleUrlEncodedForm(int fd, Request &req,
                                  const ServerConfig::Location *loc) {
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

std::string extractBoundary(const std::string &contentType) {
  std::string key = "boundary=";
  size_t pos = contentType.find(key);
  if (pos == std::string::npos)
    return "";
  return "--" + contentType.substr(pos + key.size());
}

std::vector<std::string> splitParts(const std::string &body,
                                    const std::string &boundary) {
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
      break; // 終端ならループ終了
  }

  return parts;
}

void parsePart(const std::string &part, std::string &filename,
               std::string &content) {
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

void Server::handleMultipartForm(int fd, Request &req,
                                 const ServerConfig::Location *loc) {
  std::cerr << "=== Multipart Raw Body ===\n"
            << req.body << "\n=========================\n";

  if (loc->upload_path.empty()) {
    queueSend(fd, buildHttpResponse(403, "Upload path not configured.\n"));
    return;
  }

  std::string boundary = extractBoundary(req.headers["Content-Type"]);
  if (boundary.empty()) {
    queueSend(fd,
              buildHttpResponse(400, "Missing boundary in Content-Type.\n"));
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

bool Server::isMethodAllowed(const std::string &method,
                             const ServerConfig::Location *loc) {
  if (!loc)
    return false;
  for (size_t i = 0; i < loc->method.size(); i++) {
    if (loc->method[i] == method)
      return true;
  }
  return false;
}

std::string normalizePath(const std::string &path) {
  if (path == "/")
    return "/"; // ルートはそのまま
  if (!path.empty() && path[path.size() - 1] == '/')
    return path.substr(0, path.size() - 1);
  return path;
}

Server::LocationMatch Server::getLocationForUri(const std::string &uri) const {
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
        bestMatch.path = it->first; // 元のパスはそのまま
      }
    }
  }
  return bestMatch;
}

bool Server::isCgiRequest(const Request &req) {

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

  std::string ext = uri.substr(dot); // ".php" とか
  return (ext == cgiExt);            // いまはPHPだけCGI扱い
}

// ----------------------------
// CGI実行用関数
// ----------------------------

std::pair<std::string, std::string> splitUri(const std::string &uri) {
  size_t pos = uri.find('?');
  if (pos == std::string::npos) {
    return std::make_pair(uri, "");
  } else {
    return std::make_pair(uri.substr(0, pos), uri.substr(pos + 1));
  }
}

void Server::startCgiProcess(int clientFd, const Request &req,
                             const ServerConfig::Location &loc) {
  int inPipe[2], outPipe[2];
  if (pipe(inPipe) < 0 || pipe(outPipe) < 0)
    return;

  pid_t pid = fork();
  if (pid == 0) { // --- 子プロセス ---
    dup2(inPipe[0], STDIN_FILENO);
    dup2(outPipe[1], STDOUT_FILENO);
    close(inPipe[1]);
    close(outPipe[0]);
    setenv("REQUEST_METHOD", req.method.c_str(), 1);
    std::ostringstream len;
    len << req.body.size();
    setenv("CONTENT_LENGTH", len.str().c_str(), 1);
    // URI 分割
    std::pair<std::string, std::string> parts = splitUri(req.uri);
    std::string path_only = parts.first;  // /cgi-bin/test_get.php
    std::string query_str = parts.second; // name=chatgpt&lang=ja
    // SCRIPT_FILENAME 設定
    std::string scriptPath = root + path_only;
    setenv("SCRIPT_FILENAME", scriptPath.c_str(), 1);
    // QUERY_STRING 設定
    setenv("QUERY_STRING", query_str.c_str(), 1);
    setenv("REDIRECT_STATUS", "200", 1);
    char *argv[] = {(char *)"php-cgi", NULL};
    execve(loc.cgi_path.c_str(), argv, environ);
    exit(1);
  }

  // --- 親プロセス ---
  close(inPipe[0]);
  close(outPipe[1]);

  // 非ブロッキング設定
  fcntl(outPipe[0], F_SETFL, O_NONBLOCK);

  // クライアント→CGI 入力送信
  if (!req.body.empty())
    write(inPipe[1], req.body.c_str(), req.body.size());
  close(inPipe[1]);

  // poll 監視に追加
  struct pollfd pfd;
  pfd.fd = outPipe[0];
  pfd.events = POLLIN;
  fds[nfds++] = pfd; // nfds は現在の要素数

  // 管理マップに登録
  CgiProcess proc;
  proc.clientFd = clientFd;
  proc.pid = pid;
  proc.outFd = outPipe[0];
  proc.elapsedLoops = 0;
  proc.startTime = time(NULL);
  cgiMap[outPipe[0]] = proc;
}

void Server::handleCgiOutput(int fd) {
  char buf[4096];
  ssize_t n = read(fd, buf, sizeof(buf));

  if (n > 0) {
    cgiMap[fd].buffer.append(buf, n);
    return;
  }

  if (n == 0) { // EOF
    int clientFd = cgiMap[fd].clientFd;
    // 関数を呼んでHTTPレスポンスを生成
    std::string response = buildHttpResponseFromCgi(cgiMap[fd].buffer);

    // クライアントへ送信キューに追加
    queueSend(clientFd, response);
    close(fd);
    waitpid(cgiMap[fd].pid, NULL, 0);
    cgiMap.erase(fd);
  }
}

std::string Server::buildHttpResponseFromCgi(const std::string &cgiOutput) {
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
  std::string statusLine = "HTTP/1.1 200 OK"; // デフォルト
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
void Server::handleClientSend(int index) {
  int fd = fds[index].fd;
  std::map<int, ClientInfo>::iterator it = clients.find(fd);
  if (it == clients.end())
    return;

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
    close(fd);                  // ソケットを閉じる
    fds[index] = fds[nfds - 1]; // fds 配列の詰め替え
    nfds--;
    clients.erase(fd); // clients から削除
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
  if (currentRequest.method == "POST" &&
      currentRequest.headers.find("Content-Length") ==
          currentRequest.headers.end()) {
    int fd = findFdByRecvBuffer(recvBuffer); // recvBufferに紐付くfd取得
    if (fd != -1) {
      std::string res = "HTTP/1.1 411 Length Required\r\n"
                        "Content-Length: 0\r\n\r\n";
      queueSend(fd, res);
    }

    // recvBuffer からこのリクエスト分を削除
    recvBuffer.erase(0, parser.getParsedLength());
    return ""; // リクエスト未完扱いで handleClient 側には渡さない
  }
  return recvBuffer.substr(0, parser.getParsedLength());
}

int Server::findFdByRecvBuffer(const std::string &buffer) const {
  for (std::map<int, ClientInfo>::const_iterator it = clients.begin();
       it != clients.end(); ++it) {
    if (&(it->second.recvBuffer) == &buffer) {
      return it->first; // fd を返す
    }
  }
  return -1; // 見つからなければ -1
}

int Server::getServerFd() const { return serverFd; }

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
  if (revents & POLLIN)
    handleClient(idx);
  if (revents & POLLOUT)
    handleClientSend(idx);
}

// fdからindexを見つける補助関数
int Server::findIndexByFd(int fd) {
  for (int i = 0; i < nfds; ++i) {
    if (fds[i].fd == fd)
      return i;
  }
  return -1;
}
