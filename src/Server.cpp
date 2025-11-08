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
#include "CgiProcess.hpp"

// ----------------------------
// コンストラクタ・デストラクタ
// ----------------------------

// サーバー初期化（ポート指定）
Server::Server(const ServerConfig &c)
	: cfg(c), serverFd(-1), nfds(1), port(c.port), host(c.host), root(c.root),
	  errorPages(c.errorPages) {}

// サーバー破棄（全クライアントFDクローズ）
Server::~Server()
{
	for (int i = 0; i < nfds; i++)
	{
		close(fds[i].fd);
	}
	clients.clear();
}

// ----------------------------
// 初期化系関数
// ----------------------------

// サーバー全体の初期化（ソケット作成＋バインド＋リッスン）
bool Server::init()
{
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
bool Server::createSocket()
{
	serverFd = socket(AF_INET, SOCK_STREAM, 0);
	if (serverFd < 0)
	{
		logMessage(ERROR, "socket() failed");
		return false;
	}

	int opt = 1;
	if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
	{
		logMessage(ERROR, "setsockopt() failed");
		return false;
	}
	if (!setNonBlocking(serverFd))
        return false;

	return true;
}

bool Server::setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        logMessage(ERROR, "fcntl(F_GETFL) failed");
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        logMessage(ERROR, "fcntl(O_NONBLOCK) failed");
        return false;
    }
    return true;
}

// bind & listen 設定
bool Server::bindAndListen()
{
	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(host.c_str());

	if (addr.sin_addr.s_addr == INADDR_NONE)
	{
		// "0.0.0.0" の場合などは明示的に ANY に
		addr.sin_addr.s_addr = INADDR_ANY;
	}

	if (bind(serverFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		logMessage(ERROR, "bind() failed");
		return false;
	}

	if (listen(serverFd, SOMAXCONN) < 0) // 5 → SOMAXCONN
    {
        logMessage(ERROR, "listen() failed");
        return false;
    }

	return true;
}

// ----------------------------
// クライアント接続処理
// ----------------------------

// 新規接続ハンドラ
void Server::handleNewConnection()
{
	int clientFd = acceptClient();
	if (clientFd < 0)
		return; // accept 失敗時は何もしない

	if (nfds >= MAX_CLIENTS)
	{
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
int Server::acceptClient()
{
	int clientFd = accept(serverFd, NULL, NULL);
	if (clientFd < 0)
	{
		logMessage(ERROR, "accept() failed");
		return -1;
	}

	if (!setNonBlocking(clientFd))
    {
        close(clientFd);
        return -1;
    }

	return clientFd;
}

// ----------------------------
// クライアント受信処理
// ----------------------------

bool Server::handleRedirect(int fd, const ServerConfig::Location *loc)
{
	if (!loc || loc->ret.empty())
		return false; // 続行してOK

	std::map<int, std::string>::const_iterator it = loc->ret.begin();
	int code = it->first;
	const std::string &target = it->second;

	std::ostringstream res;
	res << "HTTP/1.1 " << code << " Moved Permanently\r\n"
		<< "Location: " << target << "\r\n"
		<< "Content-Length: 0\r\n"
		<< "Connection: close\r\n\r\n";

	queueSend(fd, res.str()); // Server 内の関数を呼ぶ

	return true;
}

void Server::handleClient(int index)
{
	char buffer[1024];
	int fd = fds[index].fd;
	int bytes = recv(fd, buffer, sizeof(buffer) - 1, 0);

	if (bytes <= 0)
	{
		handleDisconnect(fd, index, bytes);
		return;
	}
	else if (bytes > 0)
	{

		buffer[bytes] = '\0';
		clients[fd].recvBuffer.append(buffer);

		// もしヘッダ解析済みなら max_body_size チェック
		Request &req = clients[fd].currentRequest;
		LocationMatch m = getLocationForUri(req.uri);
		const ServerConfig::Location *loc = m.loc;

		if (loc && clients[fd].receivedBodySize + bytes >
					static_cast<size_t>(loc->max_body_size))
		{
			ResponseBuilder res_build;
			std::string res = res_build.buildErrorResponse(cfg, loc, 413, true);
			queueSend(fd, res);
			return;
		}

		// 累積ボディサイズを更新
		clients[fd].receivedBodySize += req.body.size();

		// 1リクエストずつ処理
		while (true)
		{
			std::string requestStr =
				extractNextRequest(fd, clients[fd].recvBuffer, clients[fd].currentRequest);
			if (requestStr.empty())
				break;

			Request &req = clients[fd].currentRequest;
			LocationMatch m = getLocationForUri(req.uri);
			const ServerConfig::Location *loc = m.loc;
			const std::string &locPath = m.path;

			// 1リクエスト分の body が max_body_size を超えていないかチェック
			if (!checkMaxBodySize(fd, req.body.size(), cfg, loc))
			{
				// handleDisconnect(fd, index, 0);
				break;
			}

			printf("Request complete from fd=%d\n", fd);

			// メソッド許可チェック
			if (!handleMethodCheck(fd, req, loc, requestStr.size()))
				continue;

			// CGI / POST / GET 処理
			// --- リダイレクト処理 ---
			if (handleRedirect(fd, loc))
			{
				// redirect を queueSend したらこのリクエスト処理は完了
				// ループを抜けて次の recv まで待つ
				break;
			}

			// CGI / POST / GET 処理
			processRequest(fd, req, loc, locPath, requestStr.size());
		}
	}
}

// Server.cpp に実装
bool Server::checkMaxBodySize(int fd, int bytes, const ServerConfig &cfg, const ServerConfig::Location *loc)
{
	if (!loc)
		return true;

	clients[fd].receivedBodySize += bytes;
	if ((static_cast<size_t>(loc->max_body_size) != 0) &&
		(clients[fd].receivedBodySize >
		 static_cast<size_t>(loc->max_body_size)))
	{
		ResponseBuilder res_build;
		std::string res = res_build.buildErrorResponse(cfg, loc, 413, true);
		queueSend(fd, res);
		clients[fd].recvBuffer.clear();
		return false; // 超過
	}
	return true;
}

bool Server::handleMethodCheck(int fd, Request &req,
                               const ServerConfig::Location *loc,
                               size_t reqSize) {
	// 実装済みのMethodかチェック。PUTは未実装なので501で返す。
	if (req.method != "GET" && req.method != "POST" && req.method != "DELETE" && req.method != "HEAD")
	{
		ResponseBuilder res_build;
		std::string res = res_build.buildErrorResponse(cfg, loc, 501, true);
		queueSend(fd, res);
		clients[fd].recvBuffer.erase(0, reqSize);
		return false;
	}
  if (!isMethodAllowed(req.method, loc)) {
    ResponseBuilder res_build;
	std::string res = res_build.buildErrorResponse(cfg, loc, 405, true);
	queueSend(fd, res);
    clients[fd].recvBuffer.erase(0, reqSize);
    return false;
  }
  return true;
}

void Server::processRequest(int fd, Request &req,
							const ServerConfig::Location *loc,
							const std::string &locPath, size_t reqSize)
{
	if (isCgiRequest(req))
	{
		startCgiProcess(fd, req, *loc);
	}
	else if (req.method == "POST")
	{
		handlePost(fd, req, loc);
	}
	else
	{
		ResponseBuilder rb;
		queueSend(fd, rb.generateResponse(req, cfg, loc, locPath));
	}
	clients[fd].recvBuffer.erase(0, reqSize);
}

std::string generateUniqueFilename() {
    static unsigned long counter = 0;

    // プロセスIDとカウンタで擬似一意化
    int pid = getpid();
    int randNum = std::rand() % 10000;

    std::ostringstream oss;
    oss << "file_" << pid << "_" << counter++ << "_" << randNum << ".txt";
    return oss.str();
}

std::string buildHttpResponse(int statusCode, const std::string &body,
							  const std::string &contentType = "text/plain")
{
	std::stringstream ss;
	ss << "HTTP/1.1 " << statusCode << " "
       << (statusCode == 201 ? "Created"
           : statusCode == 403 ? "Forbidden"
           : statusCode == 500 ? "Internal Server Error"
           : "")
       << "\r\n";

    ss << "Content-Length: " << body.size() << "\r\n";
    ss << "Content-Type: " << contentType << "\r\n";
    ss << "Connection: close\r\n"; // ← 追加
    ss << "\r\n";                  // ヘッダーと本文の区切り
    ss << body;
	return ss.str();
}

void Server::handlePost(int fd, Request &req, const ServerConfig::Location *loc)
{
	std::string contentType;
	if (req.headers.find("content-type") != req.headers.end())
	{
		contentType = req.headers.at("content-type");
	}
	else
	{
		contentType = "";
	}

	bool isChunked = false;
	std::map<std::string, std::string>::iterator it = req.headers.find("transfer-encoding");
	if (it != req.headers.end() && it->second.find("chunked") != std::string::npos)
		isChunked = true;
	if (isChunked)
	{
		handleChunkedBody(fd, req, loc);
		return;
	}

	if (contentType.find("application/x-www-form-urlencoded") != std::string::npos)
	{
		handleUrlEncodedForm(fd, req, loc);
		return;
	}
	else if (contentType.find("multipart/form-data") != std::string::npos)
	{
		handleMultipartForm(fd, req, loc);
	}
	else
	{
		std::string body = "Unsupported Content-Type: " + contentType + "\n";
		queueSend(fd, buildHttpResponse(415, body));
	}
}

void saveBodyToFile(const std::string &body, const std::string &uploadDir) {
    static unsigned long counter = 0;
    int pid = getpid();
    int randNum = std::rand() % 10000;

    std::ostringstream oss;
    oss << uploadDir;
    if (!uploadDir.empty() && uploadDir[uploadDir.size() - 1] != '/')
        oss << '/';

    oss << "POST_" << pid << "_" << counter++ << "_" << randNum << ".txt";

    std::string filename = oss.str();

    std::ofstream ofs(filename.c_str(), std::ios::binary);
    if (!ofs.is_open()) {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        return;
    }

    ofs.write(body.c_str(), body.size());
    ofs.close();

    std::cout << "[INFO] Saved POST body to: " << filename << std::endl;
}

void Server::handleChunkedBody(int fd, Request &req, const ServerConfig::Location *loc)
{
	// すでに unchunk された req.body を使って処理
	// 例: ファイル保存や CGI に渡すなど
	if (loc->upload_path.empty())
	{
		queueSend(fd, buildHttpResponse(200, "Chunked data received\n"));
	}
	else
	{
		saveBodyToFile(req.body, loc->upload_path);
		queueSend(fd, buildHttpResponse(201, "File saved\n"));
	}
}

// URLデコード用
std::string urlDecode(const std::string &str)
{
	std::string ret;
	char hex[3] = {0};
	for (size_t i = 0; i < str.size(); ++i)
	{
		if (str[i] == '+')
		{
			ret += ' ';
		}
		else if (str[i] == '%' && i + 2 < str.size())
		{
			hex[0] = str[i + 1];
			hex[1] = str[i + 2];
			ret += static_cast<char>(strtol(hex, NULL, 16));
			i += 2;
		}
		else
		{
			ret += str[i];
		}
	}
	return ret;
}

// x-www-form-urlencoded を処理する関数
void Server::handleUrlEncodedForm(int fd, Request &req,
                                  const ServerConfig::Location *loc)
{
    if (loc->upload_path.empty()) {
        std::string res = buildHttpResponse(400, "No upload path configured\n");
        queueSend(fd, res);
        return;
    }

    // ファイル名生成
    static unsigned long counter = 0;
    int pid = getpid();
    int randNum = std::rand() % 10000;

    std::ostringstream filenameStream;
    filenameStream << loc->upload_path
                   << "/form_" << pid << "_" << counter++ << "_" << randNum
                   << ".txt";
    std::string filename = filenameStream.str();

    std::ofstream ofs(filename.c_str());
    if (!ofs) {
        std::string res = buildHttpResponse(500, "Internal Server Error\n");
        queueSend(fd, res);
        return;
    }

    std::string &body = req.body;
    size_t pos = 0;

    while (pos < body.size()) {
        size_t amp = body.find('&', pos);
        if (amp == std::string::npos) amp = body.size();

        size_t eq = body.find('=', pos);
        if (eq != std::string::npos && eq < amp) {
            std::string key = urlDecode(body.substr(pos, eq - pos));
            std::string value = urlDecode(body.substr(eq + 1, amp - eq - 1));
            ofs << key << "=" << value << "\n";
        } else {
            // key=value 形式でない場合はそのまま書き込む
            ofs << body.substr(pos, amp - pos) << "\n";
        }

        pos = amp + 1;
    }

    ofs.close();

    std::string res = buildHttpResponse(201, "Form received successfully\n");
    queueSend(fd, res);
}


std::string extractBoundary(const std::string &contentType)
{
	std::string key = "boundary=";
	size_t pos = contentType.find(key);
	if (pos == std::string::npos)
		return "";
	return "--" + contentType.substr(pos + key.size());
}

std::vector<std::string> splitParts(const std::string &body,
									const std::string &boundary)
{
	std::vector<std::string> parts;
	size_t start = 0, end;

	while ((end = body.find(boundary, start)) != std::string::npos)
	{
		std::string part = body.substr(start, end - start);

		// 末尾の余分な改行を削除
		if (part.size() >= 2 && part.substr(part.size() - 2) == "\r\n")
			part.erase(part.size() - 2);

		// 空文字や Content-Disposition を持たないパートは無視
		if (!part.empty() &&
			part.find("Content-Disposition") != std::string::npos)
		{
			parts.push_back(part);
		}

		start = end + boundary.size();
		if (body.substr(start, 2) == "--")
			break; // 終端ならループ終了
	}

	return parts;
}

void parsePart(const std::string &part, std::string &filename,
			   std::string &content)
{
	size_t headerEnd = part.find("\r\n\r\n");
	if (headerEnd == std::string::npos)
		return;

	std::string header = part.substr(0, headerEnd);
	content = part.substr(headerEnd + 4);

	// filename 抽出
	size_t pos = header.find("filename=\"");
	if (pos != std::string::npos)
	{
		pos += 10;
		size_t end = header.find("\"", pos);
		filename = header.substr(pos, end - pos);
	}
	else
	{
		filename = "upload.bin";
	}
}

void Server::handleMultipartForm(int fd, Request &req,
								 const ServerConfig::Location *loc)
{
	std::cerr << "=== Multipart Raw Body ===\n"
			  << req.body << "\n=========================\n";

	if (loc->upload_path.empty())
	{
		queueSend(fd, buildHttpResponse(403, "Upload path not configured.\n"));
		return;
	}

	std::string boundary = extractBoundary(req.headers["content-type"]);
	if (boundary.empty())
	{
		queueSend(fd,
				  buildHttpResponse(400, "Missing boundary in Content-Type.\n"));
		return;
	}

	std::vector<std::string> parts = splitParts(req.body, boundary);
	if (parts.empty())
	{
		queueSend(fd, buildHttpResponse(400, "No multipart data found.\n"));
		return;
	}

	for (size_t i = 0; i < parts.size(); ++i)
	{
		std::string filename, content;
		parsePart(parts[i], filename, content);
		std::string fullpath = loc->upload_path + "/" + filename;

		std::ofstream ofs(fullpath.c_str(), std::ios::binary);
		if (!ofs)
		{
			queueSend(fd, buildHttpResponse(500, "Failed to open file.\n"));
			return;
		}
		ofs.write(content.data(), content.size());
		ofs.close();
	}

	queueSend(fd, buildHttpResponse(201, "File uploaded successfully.\n"));
}

bool Server::isMethodAllowed(const std::string &method,
							 const ServerConfig::Location *loc)
{
	if (!loc)
		return false;
	for (size_t i = 0; i < loc->method.size(); i++)
	{
		if (loc->method[i] == method)
			return true;
	}
	return false;
}

std::string normalizePath(const std::string &path)
{
	if (path == "/")
		return "/"; // ルートはそのまま
	if (!path.empty() && path[path.size() - 1] == '/')
		return path.substr(0, path.size() - 1);
	return path;
}

Server::LocationMatch Server::getLocationForUri(const std::string &uri) const
{
	LocationMatch bestMatch;
	size_t bestLen = 0;

	std::string normUri = normalizePath(uri);

	for (std::map<std::string, ServerConfig::Location>::const_iterator it =
			 cfg.location.begin();
		 it != cfg.location.end(); ++it)
	{
		std::string normLoc = normalizePath(it->first);
		if (normLoc.empty())
			normLoc = "/";
		if (normUri.compare(0, normLoc.size(), normLoc) == 0)
		{
			if (normLoc.size() > bestLen)
			{
				bestLen = normLoc.size();
				bestMatch.loc = &it->second;
				bestMatch.path = it->first; // 元のパスはそのまま
			}
		}
	}
	return bestMatch;
}

bool Server::isCgiRequest(const Request &req)
{
    // C++98対応版: リスト初期化禁止なので手動で初期化
    static const char *exts[] = {".php", ".py"};
    static const size_t extCount = sizeof(exts) / sizeof(exts[0]);

    // クエリストリングを除去
    std::string uri = req.uri;
    size_t q = uri.find('?');
    if (q != std::string::npos)
        uri = uri.substr(0, q);

    // 拡張子取得
    size_t dot = uri.find_last_of('.');
    if (dot == std::string::npos)
        return false;

    std::string ext = uri.substr(dot);

    // 対応拡張子と比較
    for (size_t i = 0; i < extCount; ++i)
    {
        if (ext == exts[i])
            return true;
    }

    return false;
}

// ----------------------------
// CGI実行用関数
// ----------------------------

std::pair<std::string, std::string> splitUri(const std::string &uri)
{
	size_t pos = uri.find('?');
	if (pos == std::string::npos)
	{
		return std::make_pair(uri, "");
	}
	else
	{
		return std::make_pair(uri.substr(0, pos), uri.substr(pos + 1));
	}
}

// 外部関数（Serverクラス外でも良い）
std::pair<std::string, std::string> buildCgiScriptPath(
	const std::string &uri,
	const ServerConfig::Location &loc,
	const std::map<std::string, ServerConfig::Location> &locations)
{
	std::pair<std::string, std::string> parts = splitUri(uri);
	std::string path_only = parts.first;
	std::string query_str = parts.second;

	std::string scriptPath = loc.root;
	if (!scriptPath.empty() && scriptPath[scriptPath.size() - 1] == '/')
		scriptPath.erase(scriptPath.size() - 1);

	// location キーを探す
	std::string locKey;
	for (std::map<std::string, ServerConfig::Location>::const_iterator it = locations.begin();
		 it != locations.end(); ++it)
	{
		if (&it->second == &loc)
			locKey = it->first;
	}

	if (path_only.find(locKey) == 0)
	{
		std::string rest = path_only.substr(locKey.length());
		if (!rest.empty() && rest[0] != '/')
			scriptPath += '/';
		scriptPath += rest;
	}
	else
	{
		scriptPath += path_only;
	}

	return std::make_pair(scriptPath, query_str);
}

// env 設定を作る関数
std::map<std::string, std::string> buildCgiEnv(const Request &req,
											   const ServerConfig::Location &loc,
											   const std::map<std::string, ServerConfig::Location> &locations)
{
	std::map<std::string, std::string> env;

	env["REQUEST_METHOD"] = req.method;

	std::ostringstream len;
	len << req.body.size();
	env["CONTENT_LENGTH"] = len.str();

	std::pair<std::string, std::string> envPaths = buildCgiScriptPath(req.uri, loc, locations);
	env["SCRIPT_FILENAME"] = envPaths.first;
	env["QUERY_STRING"] = envPaths.second;
	env["REDIRECT_STATUS"] = "200";

	return env;
}

// 子プロセス側の設定・exec
void executeCgiChild(int inFd, int outFd, const std::string &cgiPath,
					 const std::map<std::string, std::string> &env)
{
	dup2(inFd, STDIN_FILENO);
	dup2(outFd, STDOUT_FILENO);
	close(inFd);
	close(outFd);

	for (std::map<std::string, std::string>::const_iterator it = env.begin(); it != env.end(); ++it)
		setenv(it->first.c_str(), it->second.c_str(), 1);

	// CGIスクリプトの実際のファイルパスを取得
    std::string scriptPath;
    std::map<std::string, std::string>::const_iterator it = env.find("SCRIPT_FILENAME");
    if (it != env.end())
        scriptPath = it->second;
    else
        scriptPath = "";

    // Pythonや他のインタプリタ系は scriptPath を argv[1] に渡す必要がある
    char *argv[3];
    argv[0] = const_cast<char *>(cgiPath.c_str());
    argv[1] = const_cast<char *>(scriptPath.c_str());
    argv[2] = NULL;
    // execveに動的なcgiPathを渡す
    execve(argv[0], argv, environ);
	exit(1);
}

// 親プロセス側でのパイプ送信
void Server::registerCgiProcess(int clientFd, pid_t pid,
                                int inFd, int outFd, const std::string &body,
                                std::map<int, CgiProcess> &cgiMap)
{
    // 1. 非ブロッキング設定
    fcntl(outFd, F_SETFL, O_NONBLOCK);
    fcntl(inFd, F_SETFL, O_NONBLOCK);

    // 2. CGI プロセス情報作成
    CgiProcess proc;
    proc.clientFd = clientFd;
    proc.pid = pid;
    proc.inFd = inFd;
    proc.outFd = outFd;
    proc.inputBuffer = body;  // 受信済み body をバッファに保持

    // 3. 非ブロッキングで可能な範囲だけ書き込み
    ssize_t written = 0;
    const char* data = proc.inputBuffer.c_str();
    size_t len = proc.inputBuffer.size();
    while (written < static_cast<ssize_t>(len))
    {
        ssize_t n = write(inFd, data + written, len - written);
        if (n > 0)
            written += n;
        else
            break; // 書けない場合は次回 poll で再送
    }
    if (written > 0)
        proc.inputBuffer.erase(0, written);

    // 4. イベント初期化
    proc.events = POLLIN;  // 出力監視は常に
    if (!proc.inputBuffer.empty())
        proc.events |= POLLOUT;  // 書き込み残があれば POLLOUT 追加

    // 5. CGI 管理マップに登録
    proc.remainingMs = 5000; // タイムアウト5秒
    cgiMap[outFd] = proc;
}

void Server::startCgiProcess(int clientFd, const Request &req, const ServerConfig::Location &loc)
{
	int inPipe[2], outPipe[2];
	if (pipe(inPipe) < 0 || pipe(outPipe) < 0)
		return;

	pid_t pid = fork();
	if (pid == 0)
	{
		// 子プロセス
		std::map<std::string, std::string> env = buildCgiEnv(req, loc, cfg.location);
		executeCgiChild(inPipe[0], outPipe[1], loc.cgi_path, env);
	}

	// 親プロセス
	close(inPipe[0]);
	close(outPipe[1]);
	registerCgiProcess(clientFd, pid, inPipe[1], outPipe[0], req.body, cgiMap);
}

void Server::handleCgiOutput(int fd)
{
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf));

    if (n > 0)
    {
        // バッファ上限チェック（例: 1MB）
        if (cgiMap[fd].buffer.size() + n > 1024 * 1024)
        {
            std::cerr << "CGI buffer overflow on fd=" << fd << std::endl;
            handleCgiError(fd);
            return;
        }
		cgiMap[fd].buffer.append(buf, n);
    }
    else if (n == 0)
    {
        // EOF → 正常終了
        handleCgiClose(fd);
    }
    else  // n < 0
    {
        // 読み取りエラー
        handleCgiError(fd);
    }
}

void Server::handleCgiInput(int fd)
{
    // CGIエントリ取得
    if (cgiMap.count(fd) == 0)
        return;

    CgiProcess *proc = getCgiProcess(fd);
	if (!proc)
		return;

    if (proc->inputBuffer.empty()) {
        // 書き込むものがない → POLLOUT解除
        proc->events &= ~POLLOUT;
        if (proc->inFd > 0)
            close(proc->inFd);
        proc->inFd = -1;
        return;
    }

    // 残りデータを書き込み
    const char *data = proc->inputBuffer.c_str();
    ssize_t len = proc->inputBuffer.size();
    ssize_t written = write(proc->inFd, data, len);

	if (written < 0) {
        // --- 一時的な書き込み失敗 ---
        // → poll の次回 POLLOUT で再試行
        // ただし、パイプ切断など致命的な場合に備えて確認
        perror("write to CGI inFd failed");
        return;
    }

    if (written > 0) {
        proc->inputBuffer.erase(0, written);
    }

    // すべて書けたら POLLOUT解除 + inFd クローズ
    if (proc->inputBuffer.empty()) {
        proc->events &= ~POLLOUT;
        if (proc->inFd > 0){
            close(proc->inFd);
            proc->inFd = -1;
        }
    }
}

std::string Server::buildHttpErrorPage(int code, const std::string &message)
{
    std::ostringstream oss;
    oss << "<html><head><title>" << code << " Error</title></head><body>";
    oss << "<h1>" << code << " " << message << "</h1>";
    oss << "<hr><p>Webserv CGI Engine</p></body></html>";
    return oss.str();
}

void Server::handleCgiError(int fd)
{
    if (cgiMap.count(fd) == 0)
        return;

    int clientFd = cgiMap[fd].clientFd;
    std::cerr << "[ERROR] CGI read failed on fd=" << fd << std::endl;

    std::string body = buildHttpErrorPage(500, "Internal Server Error");
    std::ostringstream oss;
    oss << "HTTP/1.1 500 Internal Server Error\r\n";
    oss << "Content-Type: text/html\r\n";
    oss << "Content-Length: " << body.size() << "\r\n\r\n";
	oss << "Connection: close\r\n\r\n"; // ← 追加
    oss << body;

    queueSend(clientFd, oss.str());
    close(fd);
    waitpid(cgiMap[fd].pid, NULL, 0);
    cgiMap.erase(fd);
}

void Server::handleCgiClose(int fd)
{
    // --- 1️⃣ 登録確認 ---
    if (cgiMap.count(fd) == 0)
        return;

    CgiProcess &proc = cgiMap[fd];
    int clientFd = proc.clientFd;

    // --- 2️⃣ 子プロセス終了確認 (非ブロッキング) ---
    int status = 0;
    pid_t result = waitpid(proc.pid, &status, WNOHANG);
    if (result == 0) {
        // まだ終了していない（再びpollで呼ばれる）
        std::cout << "[DEBUG] CGI still running pid=" << proc.pid << std::endl;
        return;
    } else if (result < 0) {
        perror("waitpid");
    }

    // --- 子プロセス異常終了チェック ---
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        // 🚨 CGIが異常終了 → HTTP500を返す
        std::string body = buildHttpErrorPage(500, "Internal Server Error");
        std::ostringstream oss;
		oss << "HTTP/1.1 500 Internal Server Error\r\n";
		oss << "Content-Type: text/html\r\n";
		oss << "Content-Length: " << body.size() << "\r\n";
		oss << "Connection: close\r\n\r\n";  // ← 追加
		oss << body;
        queueSend(clientFd, oss.str());
    }
    else
    {
        // ✅ 正常終了 → 通常のレスポンス処理
        std::string response = buildHttpResponseFromCgi(proc.buffer);
        queueSend(clientFd, response);
    }

    // --- 4️⃣ パイプを確実に閉じる ---
    if (proc.inFd > 0) {
        close(proc.inFd);
        proc.inFd = -1;
    }
    if (proc.outFd > 0) {
        close(proc.outFd);
        proc.outFd = -1;
    }

    // --- 5️⃣ poll監視解除（次ループで再構築される） ---
    proc.events = 0;

    // --- 6️⃣ CGIプロセス削除 ---
    cgiMap.erase(fd);

    std::cout << "[CGI] process pid=" << proc.pid << " cleaned up fd=" << fd << std::endl;
}

std::string Server::buildHttpResponseFromCgi(const std::string &cgiOutput)
{
    std::string headers;
    std::string content;
    std::string statusLine = "HTTP/1.1 200 OK"; // デフォルト

    // --- 1️⃣ ヘッダと本文を分離 ---
    size_t headerEnd = cgiOutput.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        headerEnd = cgiOutput.find("\n\n");
    if (headerEnd != std::string::npos) {
        headers = cgiOutput.substr(0, headerEnd);
        content = cgiOutput.substr(headerEnd + (cgiOutput[headerEnd] == '\r' ? 4 : 2));
    } else {
        // ヘッダがない → 全部本文として扱う
        content = cgiOutput;
    }

    // --- 2️⃣ ヘッダ行を個別に処理 ---
    std::istringstream headerStream(headers);
    std::string line;
    std::ostringstream filteredHeaders;

    bool hasContentType = false;

    while (std::getline(headerStream, line)) {
        // 行末の \r を削除
        if (!line.empty() && line[line.size() - 1] == '\r')
    		line.erase(line.size() - 1);

        // 空行スキップ
        if (line.empty()) continue;

        // case-insensitive 検索のためにコピー
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // --- Status ヘッダ ---
        if (lower.find("status:") == 0) {
            std::string statusValue = line.substr(7);
            size_t start = statusValue.find_first_not_of(" \t");
            size_t end = statusValue.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos)
                statusValue = statusValue.substr(start, end - start + 1);
            statusLine = "HTTP/1.1 " + statusValue;
            continue; // StatusヘッダはHTTPヘッダには入れない
        }

        // --- Content-Type ヘッダ確認 ---
        if (lower.find("content-type:") == 0)
            hasContentType = true;

        // その他ヘッダはそのままコピー
        filteredHeaders << line << "\r\n";
    }

    // --- 3️⃣ Content-Type補完 ---
    if (!hasContentType)
        filteredHeaders << "Content-Type: text/html\r\n";

    // --- 4️⃣ HTTPレスポンス組み立て ---
    std::ostringstream oss;
    oss << statusLine << "\r\n";
    oss << "Content-Length: " << content.size() << "\r\n";
	oss << "Connection: close\r\n";  // ← ここで明示的に追加
    oss << filteredHeaders.str();
    oss << "\r\n" << content;

    return oss.str();
}

// ----------------------------
// クライアント送信処理
// ----------------------------

// クライアント送信バッファのデータ送信
void Server::handleClientSend(int index)
{
    int fd = fds[index].fd;
    std::map<int, ClientInfo>::iterator it = clients.find(fd);
    if (it == clients.end())
        return;

    ClientInfo &client = it->second;

    // 送信バッファが空なら何もしない
    while (!client.sendBuffer.empty())
    {
        // 1回あたりの送信サイズを制限（例: 4KB）
        size_t sendSize = std::min(client.sendBuffer.size(), static_cast<size_t>(4096));

        ssize_t n = write(fd, client.sendBuffer.data(), sendSize);

        if (n > 0)
        {
            // 書き込み済み分をバッファから削除
            client.sendBuffer.erase(0, n);
        }
        else
        {
            // n == 0 または n < 0 の場合は接続を閉じる
            std::cerr << "[ERROR] write() failed or returned 0, closing fd=" << fd << std::endl;
            handleConnectionClose(fd);
            return; // ループ終了
        }
		// 送信完了の場合は接続を閉じる
		if (client.sendBuffer.empty()) {
			handleConnectionClose(fd);
		}
    }
}

// 送信キューにデータを追加する関数
void Server::queueSend(int fd, const std::string &data)
{
	std::map<int, ClientInfo>::iterator it = clients.find(fd);
	if (it != clients.end())
	{
		// 送信バッファにデータを追加
		it->second.sendBuffer += data;
	}
}

// ----------------------------
// クライアント接続終了処理
// ----------------------------

// クライアント接続クローズ処理
void Server::handleConnectionClose(int fd)
{
    // clients から削除
    std::map<int, ClientInfo>::iterator it = clients.find(fd);
    if (it != clients.end())
    {
        std::cout << "[INFO] Closing connection fd=" << fd << std::endl;

        // ソケットを閉じる
        close(fd);

        // 送受信バッファもクリア
        it->second.sendBuffer.clear();
        it->second.recvBuffer.clear();

        // クライアントマップから削除
        clients.erase(it);
    }
    // pollfd 配列の更新は ServerManager が担当
}

// 接続切断処理（recv エラーや切断時の処理）
void Server::handleDisconnect(int fd, int index, int bytes)
{
	// bytes が 0 または負の場合は接続終了とみなす
	if (bytes <= 0)
	{
		std::ostringstream oss;
		if (bytes == 0)
		{
			oss << "Client disconnected: fd=" << fd;
		}
		else
		{
			oss << "Client read error or disconnected: fd=" << fd;
		}
		logMessage(INFO, oss.str());
		close(fd);					// ソケットを閉じる
		fds[index] = fds[nfds - 1]; // fds 配列の詰め替え
		nfds--;
		clients.erase(fd); // clients から削除
	}
}

// ----------------------------
// ヘッダ解析・リクエスト処理
// ----------------------------

std::string Server::extractNextRequest(int clientFd, std::string &recvBuffer,
                                       Request &currentRequest)
{
    RequestParser parser;
    if (!parser.isRequestComplete(recvBuffer))
        return "";

    currentRequest = parser.parse(recvBuffer);

	// --- Content-Length 超過チェック ---
	if (isContentLengthExceeded(currentRequest, recvBuffer)) {
		sendHttpError(clientFd, 400, "Bad Request", parser.getParsedLength(), recvBuffer);
		return "";
	}

    // --- 不正リクエストかどうかをチェック ---
    if (currentRequest.method.empty()) {
        sendHttpError(clientFd, 400, "Bad Request", parser.getParsedLength(), recvBuffer);
        return "";
    }

    // --- POST の長さチェック ---
    if (currentRequest.method == "POST" &&
        currentRequest.headers.find("content-length") == currentRequest.headers.end() &&
        currentRequest.headers.find("transfer-encoding") == currentRequest.headers.end())
    {
        sendHttpError(clientFd, 411, "Length Required", parser.getParsedLength(), recvBuffer);
        return "";
    }

    // --- 正常リクエスト ---
    std::string completeRequest = recvBuffer.substr(0, parser.getParsedLength());
    recvBuffer.erase(0, parser.getParsedLength());
    return completeRequest;
}

bool Server::isContentLengthExceeded(const Request &req,
                                     const std::string &recvBuffer) {
    std::map<std::string, std::string>::const_iterator it =
        req.headers.find("content-length");
    if (it == req.headers.end())
        return false; // Content-Lengthがない

    size_t declaredLength = std::strtoul(it->second.c_str(), NULL, 10);

    size_t headerEnd = recvBuffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return false;

    size_t bodySize = recvBuffer.size() - (headerEnd + 4);
    return bodySize > declaredLength;
}

// ヘルパー関数: HTTPエラー送信 + バッファ調整
void Server::sendHttpError(int clientFd, int status, const std::string &msg,
                           size_t parsedLength, std::string &recvBuffer)
{
    std::ostringstream res;
    res << "HTTP/1.1 " << status << " " << msg << "\r\n"
        << "Content-Length: " << msg.size() << "\r\n"
        << "Content-Type: text/plain\r\n"
        << "Connection: close\r\n\r\n"
        << msg;
    queueSend(clientFd, res.str());
    recvBuffer.erase(0, parsedLength);
}

int Server::findFdByRecvBuffer(const std::string &buffer) const
{
	for (std::map<int, ClientInfo>::const_iterator it = clients.begin();
		 it != clients.end(); ++it)
	{
		if (&(it->second.recvBuffer) == &buffer)
		{
			return it->first; // fd を返す
		}
	}
	return -1; // 見つからなければ -1
}

int Server::getServerFd() const { return serverFd; }

std::vector<int> Server::getClientFds() const
{
	std::vector<int> fds;
	for (std::map<int, ClientInfo>::const_iterator it = clients.begin();
		 it != clients.end(); ++it)
	{
		fds.push_back(it->first);
	}
	return fds;
}

void Server::onPollEvent(int fd, short revents)
{
    // --------------------------
    // 1. サーバーFD（新しい接続受付）
    // --------------------------
    if (fd == serverFd) {
        if (revents & POLLIN)
            handleNewConnection();           // 新しい接続受け入れ
        if (revents & (POLLERR | POLLHUP))
            handleServerError(fd);          // listen socketにエラー
        return;
    }

    // --------------------------
    // 2. CGI FD（出力 or 入力 監視）
    // --------------------------
    if (cgiMap.count(fd)) {
        // --- CGI出力（子→親） ---
        if (revents & POLLIN)
            handleCgiOutput(fd);

        // --- CGI入力（親→子） ---
        if (revents & POLLOUT)
            handleCgiInput(fd);

        // --- 終了またはエラー ---
        if (revents & (POLLHUP | POLLERR))
            handleCgiClose(fd);

        return;
    }

    // --------------------------
    // 3. 通常クライアントFD
    // --------------------------
    if (clients.count(fd)) {
		int idx = findIndexByFd(fd);
        if (revents & POLLIN){
            // 🔹 通常クライアント
			if (revents & POLLIN)
				handleClient(idx); 			// クライアントからのリクエスト受信
		}
        if (revents & POLLOUT)
            handleClientSend(idx);           // クライアントへのレスポンス送信
        if (revents & (POLLERR | POLLHUP))
            handleConnectionClose(fd);      // エラーや切断時の後処理
    }
}

// listenソケット（サーバーFD）でエラーが発生したときの処理
void Server::handleServerError(int fd)
{
    std::cerr << "[ERROR] Server socket error on fd " << fd << std::endl;

    // listenソケットは通常閉さない
    // 必要に応じてログ出力や管理者通知などをここで行う
    // 例: std::cerr << "Check network/bind settings\n";

    // サーバーを停止する場合はここでclose(fd)するが、
    // Webservでは通常そのまま運用
}

// fdからindexを見つける補助関数
int Server::findIndexByFd(int fd)
{
	for (int i = 0; i < nfds; ++i)
	{
		if (fds[i].fd == fd)
			return i;
	}
	return -1;
}

CgiProcess* Server::getCgiProcess(int fd) {
    std::map<int, CgiProcess>::iterator it = cgiMap.find(fd);
    if (it == cgiMap.end()) {
        throw std::runtime_error("getCgiProcess: fd not found in cgiMap");
    }
    return &(it->second); // ✅ オブジェクトのアドレスを返す
}
