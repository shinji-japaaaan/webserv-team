#include "resp/ResponseBuilder.hpp"
#include "resp/Mime.hpp"
#include <cerrno>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <cstdio>     // std::remove

// ====== 便利関数======
static bool isMethodAllowed(const std::string &m,
                            const ServerConfig::Location *loc) {
  if (!loc)
    return true; // Locationが無いなら全許可とみなす
  if (loc->method.empty())
    return true; // 空なら全許可

  for (size_t i = 0; i < loc->method.size(); ++i) {
    if (loc->method[i] == m)
      return true;
  }
  return false;
}

static std::string buildAllowHeader(const ServerConfig::Location *loc) {
  if (!loc || loc->method.empty()) {
    // デフォルトは3メソッド全部
    return "GET, HEAD, DELETE";
  }
  // loc->method を ", " で結合
  std::ostringstream oss;
  for (size_t i = 0; i < loc->method.size(); ++i) {
    if (i)
      oss << ", ";
    oss << loc->method[i];
  }
  return oss.str();
}

static std::string joinPath(const std::string &a, const std::string &b) {
  if (a.empty())
    return b;
  if (a[a.size() - 1] == '/' && b.size() && b[0] == '/')
    return a + b.substr(1);
  if (a[a.size() - 1] != '/' && b.size() && b[0] != '/')
    return a + "/" + b;
  return a + b;
}

static std::string slurpFile(const std::string &p) {
  std::ifstream ifs(p.c_str(), std::ios::in | std::ios::binary);
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

// tolower copy
static std::string lowerCopy(const std::string &s) {
  std::string r = s;
  for (size_t i = 0; i < r.size(); ++i) {
    if ('A' <= r[i] && r[i] <= 'Z') {
      r[i] = char(r[i] - 'A' + 'a');
    }
  }
  return r;
}

// 簡易 reason phrase
static std::string reasonPhrase(int code) {
  switch (code) {
  case 200:
    return "OK";
  case 204:
    return "No Content";
  case 400:
    return "Bad Request";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 413:
    return "Payload Too Large";
  case 415:
    return "Unsupported Media Type";
  case 500:
    return "Internal Server Error";
  case 501:
    return "Not Implemented";
  }
  return "Unknown";
}

// cfg.root と loc->root
// をマージして「最終的に使うルートディレクトリ」を決める。 ルール:
//  - loc が無い→ cfg.root を返す
//  - loc->root が空→ cfg.root を返す
//  - loc->root が '/' で始まる → プロセスカレント直下扱い (ex: "/upload/" →
//  "./upload/")
//  - それ以外 → cfg.root と連結 (ex: cfg.root="./docs/", loc->root="img/" →
//  "./docs/img/")
std::string
ResponseBuilder::mergeRoots(const ServerConfig &cfg,
                            const ServerConfig::Location *loc) const {
  if (!loc || loc->root.empty()) {
    return cfg.root;
  }

  const std::string &lr = loc->root;

  // --- case1: 絶対パス ---
  if (!lr.empty() && lr[0] == '/') {
    return lr;
  }

  // --- case2: ./ で始まる相対パス（カレントディレクトリ基準） ---
  if (lr.size() >= 2 && lr[0] == '.' && lr[1] == '/') {
    return lr; // サーバルートとは結合しない
  }

  // --- case3: 通常の相対パス（サーバルートに結合）---
  std::string merged = joinPath(cfg.root, lr);
  return merged;
}

// URIからlocationのマウントパスを剥がして、ローカルでの相対パスにする.
// 例: uri="/delete/test_delete.html", locPath="/delete/"
// → "test_delete.html"
// locPathが空なら、先頭の'/'を落としただけのパスにする
std::string
ResponseBuilder::stripLocationPrefix(const std::string &uri,
                                     const std::string &locPath) const {
  if (!locPath.empty() && uri.compare(0, locPath.size(), locPath) == 0) {
    std::string rest = uri.substr(locPath.size());
    // 先頭の'/'が二重にならないよう軽く整える
    while (!rest.empty() && rest[0] == '/')
      rest.erase(0, 1);
    return rest;
  }

  // fallback: "/"を外して相対に
  std::string tmp = uri;
  while (!tmp.empty() && tmp[0] == '/')
    tmp.erase(0, 1);
  return tmp;
}

// ====== ResponseBuilder メンバ ======

// Dateヘッダ向け日付
std::string ResponseBuilder::httpDate_() const {
    return "Sun, 09 Nov 2025 10:00:00 GMT";
}

bool ResponseBuilder::isTraversal(const std::string &uri) const {
  // ".." または 大文字小文字無視の "%2e%2e" を含んだらアウト
  if (uri.find("..") != std::string::npos)
    return true;
  std::string low = lowerCopy(uri);
  if (low.find("%2e%2e") != std::string::npos)
    return true;
  return false;
}

// GET/HEAD用: docRoot + uri を解決し、
// - ディレクトリなら index.html を補う
// - realpathでdocRoot外脱出を防ぎたいところだけど、まだ簡易版としてdocRoot直結
// + traversalチェックでOKとしておく
std::string ResponseBuilder::resolvePathForGet(
    const std::string &docRoot,
    const std::string &uri,
    const std::string &locationPath, // ← 追加
    bool &isDirOut) const
{
    isDirOut = false;

    // locationPath を URI から除去して相対パスを作る
    std::string relativeUri = uri;
    if (uri.compare(0, locationPath.size(), locationPath) == 0) {
        relativeUri = uri.substr(locationPath.size());
        if (relativeUri.empty()) relativeUri = "/";
    }

    // "./" の重複防止付きパス結合
    std::string path;
    if (!docRoot.empty() && docRoot[docRoot.size() - 1] == '/' && !relativeUri.empty() && relativeUri[0] == '/')
        path = docRoot + relativeUri.substr(1);
    else if (!docRoot.empty() && docRoot[docRoot.size() - 1] != '/' && !relativeUri.empty() && relativeUri[0] != '/')
        path = docRoot + "/" + relativeUri;
    else
        path = docRoot + relativeUri;

    // ディレクトリ判定
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        isDirOut = true;
        if (!path.empty() && path[path.size() - 1] != '/')
            path += "/";
        return path; // index.html はここでは付けない
    }

    return path;
}

// DELETE用は index.html など補完しない想定
std::string
ResponseBuilder::resolvePathForDelete(const std::string &docRoot,
                                      const std::string &uri) const {
  std::string target = uri.empty() ? "/" : uri;
  return joinPath(docRoot, target);
}

std::string ResponseBuilder::guessContentType(const std::string &path) const {
  // 拡張子を安全に取り出す
  size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) {
    // 拡張子がない場合のデフォルト
    return "application/octet-stream";
  }

  std::string ext = path.substr(dot); // ".html" / ".txt" / ".png" など

  // よくあるやつだけ軽くマッピング
  if (ext == ".html" || ext == ".htm")
    return "text/html; charset=utf-8";
  if (ext == ".txt")
    return "text/plain; charset=utf-8";
  if (ext == ".css")
    return "text/css";
  if (ext == ".js")
    return "application/javascript";
  if (ext == ".json")
    return "application/json";
  if (ext == ".png")
    return "image/png";
  if (ext == ".gif")
    return "image/gif";
  if (ext == ".jpg" || ext == ".jpeg")
    return "image/jpeg";

  // よく分からない拡張子はバイナリ扱い
  return "application/octet-stream";
}

// 200 OK (GET/HEAD用). headOnlyならボディ付けない
std::string ResponseBuilder::buildOkResponseFromFile(const std::string &absPath,
                                                     bool headOnly,
                                                     bool close) {
  std::string body = slurpFile(absPath);
  std::string ct = guessContentType(absPath);

  std::ostringstream res;
  res << "HTTP/1.1 200 OK\r\n"
      << "Content-Type: " << ct << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: " << (close ? "close" : "keep-alive") << "\r\n"
      << "Date: " << httpDate_() << "\r\n"
      << "Server: webserv/0.1\r\n\r\n";

  if (!headOnly) {
    res << body;
  }
  return res.str();
}

// 405
std::string ResponseBuilder::buildMethodNotAllowed(const std::string &allow,
                                                   const ServerConfig &cfg) {
  (void)cfg; // いまcfg未使用だけど将来errorPages使うかもなので引数は残す

  std::string body = "<!doctype html><title>Method Not Allowed</title>"
                     "<h1>Method Not Allowed</h1>";

  std::ostringstream res;
  res << "HTTP/1.1 405 " << reasonPhrase(405) << "\r\n"
      << "Content-Type: text/html\r\n"
      << "Allow: " << allow << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n"
      << "Date: " << httpDate_() << "\r\n"
      << "Server: webserv/0.1\r\n\r\n"
      << body;
  return res.str();
}

// 汎用「本文なし」レスポンス（例：204, 403, 404の最小版に使える）
std::string ResponseBuilder::buildSimpleResponse(
    int statusCode, const std::string &reason, bool close,
    const std::map<std::string, std::string> &extraHeaders) const {
  std::ostringstream res;
  res << "HTTP/1.1 " << statusCode << " " << reason << "\r\n";
  for (std::map<std::string, std::string>::const_iterator it =
           extraHeaders.begin();
       it != extraHeaders.end(); ++it) {
    res << it->first << ": " << it->second << "\r\n";
  }
  res << "Content-Length: 0\r\n"
      << "Connection: " << (close ? "close" : "keep-alive") << "\r\n"
      << "Date: " << httpDate_() << "\r\n"
      << "Server: webserv/0.1\r\n\r\n";
  return res.str();
}

std::string ResponseBuilder::buildErrorResponseFromFile(const std::string &path,
                                                        int code,
                                                        bool close) const {
  std::ifstream ifs(path.c_str());
  std::ostringstream body;

  if (ifs.good()) {
    body << ifs.rdbuf();
  } else {
    body << "<html><body><h1>" << code << " " << reasonPhrase(code)
         << "</h1><p>Could not open custom error page.</p></body></html>";
  }

  std::ostringstream res;
  res << "HTTP/1.1 " << code << " " << reasonPhrase(code) << "\r\n"
      << "Content-Type: text/html\r\n"
      << "Content-Length: " << body.str().size() << "\r\n"
      << "Connection: " << (close ? "close" : "keep-alive") << "\r\n"
      << "Date: " << httpDate_() << "\r\n"
      << "Server: webserv/0.1\r\n\r\n"
      << body.str();
  return res.str();
}

std::string ResponseBuilder::buildSimpleResponse(int statusCode,
                                                 const std::string &reason,
                                                 bool close) const {
  std::map<std::string, std::string> dummy;
  return buildSimpleResponse(statusCode, reason, close, dummy);
}

std::string
ResponseBuilder::buildErrorResponse(const ServerConfig &cfg,
                                    const ServerConfig::Location *loc,
                                    int statusCode, bool close) const {
  std::string filePath;

  // 1. Location にカスタムページがある場合
  // ここはエラーじゃなくて、returnで捨て去れたファイルを返す処理しているけど、いいのか？
  if (loc && loc->ret.count(statusCode)) {
    filePath = loc->ret.at(statusCode);
  }
  // 2. サーバ全体にカスタムページがある場合
  else if (cfg.errorPages.count(statusCode)) {
    filePath = cfg.errorPages.at(statusCode);
  }
  // ファイルpathの指定があれば返す
  if (!filePath.empty()) {
    std::ifstream ifs(filePath.c_str(), std::ios::binary);
    if (ifs.is_open()) {
      std::ostringstream oss;
      oss << ifs.rdbuf();
      std::string body = oss.str();
      std::ostringstream res;
      res << "HTTP/1.1 " << statusCode << " " << reasonPhrase(statusCode)
          << "\r\n"
          << "Content-Type: text/html\r\n"
          << "Content-Length: " << body.size() << "\r\n"
          << "Connection: " << (close ? "close" : "keep-alive") << "\r\n"
          << "Date: " << httpDate_() << "\r\n"
          << "Server: webserv/0.1\r\n\r\n"
          << body;
      return res.str();
    }
  }
  // close); シンプル版ではなく、標準のエラーページを返すようにする。
  filePath = DEFAULT_ERROR_PAGE;
  std::ifstream ifs(filePath.c_str(), std::ios::binary);
  if (ifs.is_open()) {
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string body = oss.str();
    std::ostringstream res;
    res << "HTTP/1.1 " << statusCode << " " << reasonPhrase(statusCode)
        << "\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: " << (close ? "close" : "keep-alive") << "\r\n"
        << "Date: " << httpDate_() << "\r\n"
        << "Server: webserv/0.1\r\n\r\n"
        << body;
    return res.str();
  } else {
    // デフォルトファイルも見つからなければ、Bodyなしで返す。
    return buildSimpleResponse(statusCode, reasonPhrase(statusCode), close);
  }
}

std::string buildAutoIndexHtml(const std::string &dirPath,
                               const std::string &uri) {
  std::stringstream html;
  html << "<html><head><title>Index of " << uri << "</title></head>";
  html << "<body><h1>Index of " << uri << "</h1><ul>";

  DIR *dir = opendir(dirPath.c_str());
  if (!dir) {
    // ディレクトリが開けなければ 403 として扱う（呼び出し側で処理可）
    return "";
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    std::string name = entry->d_name;

    // 「.」と「..」は表示しない
    if (name == "." || name == "..")
      continue;

    // パス組み立て
    std::string fullPath = dirPath + "/" + name;

    struct stat st;
    if (stat(fullPath.c_str(), &st) != 0)
      continue;

    // ディレクトリは末尾に /
    if (S_ISDIR(st.st_mode))
      name += "/";

    html << "<li><a href=\"" << uri;
    if (!uri.empty() && uri[uri.size() - 1] != '/')
      html << "/";
    html << name << "\">" << name << "</a></li>";
  }

  closedir(dir);

  html << "</ul><hr><address>Webserv/1.0</address></body></html>";
  return html.str();
}

std::string buildOkResponseFromString(const std::string &body,
                                      const std::string &contentType) {
  std::stringstream response;

  // ステータスライン
  response << "HTTP/1.1 200 OK\r\n";

  // ヘッダ
  response << "Content-Type: " << contentType << "\r\n";
  response << "Content-Length: " << body.size() << "\r\n";
  response << "Connection: close\r\n";
  response << "\r\n";                  // ヘッダ終端

  // ボディ
  response << body;

  return response.str();
}

// --- GET/HEAD 処理 (3引数版) ---
std::string
ResponseBuilder::handleGetLikeCore(const Request &req, const ServerConfig &cfg,
                                   const ServerConfig::Location *loc,
                                   const std::string &locPath) {
  if (isTraversal(req.uri)) {
    return buildSimpleResponse(403, reasonPhrase(403), true);
  }
  std::string effectiveRoot = mergeRoots(cfg, loc);
  bool isDirFlag = false;
  std::string absPath = resolvePathForGet(effectiveRoot, req.uri, locPath, isDirFlag);

  // --- ディレクトリ処理 ---
  if (isDirFlag) {
    if (loc->autoindex == "on") {
      std::string body = buildAutoIndexHtml(absPath, req.uri);
      return buildOkResponseFromString(body, "text/html");
    } else {
      // index.htmlが存在すれば返す（将来的な拡張）
      std::string indexPath = absPath;
      if (indexPath[indexPath.size() - 1] != '/')
        indexPath += '/';
      //   indexPath += "index.html";
      // index.htmlを直書きじゃなくて、locaiton /のindexから参照するようにする。
      indexPath += loc->index;
      std::ifstream indexFile(indexPath.c_str(), std::ios::binary);
      if (indexFile.is_open()) {
        bool headOnly = (req.method == "HEAD");
        return buildOkResponseFromFile(indexPath, headOnly, true);
      }
      return buildErrorResponse(cfg, loc, 403);
    }
  }

  // --- 通常のファイル処理 ---
  std::ifstream ifs(absPath.c_str(), std::ios::binary);
  if (!ifs.is_open()) {
    return buildErrorResponse(cfg, loc, 404);
  }


  bool headOnly = (req.method == "HEAD");
  return buildOkResponseFromFile(absPath, headOnly, true);
}

// --- DELETE 処理 (3引数版) ---
std::string
ResponseBuilder::handleDeleteCore(const Request &req, const ServerConfig &cfg,
                                  const ServerConfig::Location *loc) {
  if (isTraversal(req.uri)) {
    return buildErrorResponse(cfg, loc, 403, true);
  }

  std::string effectiveRoot = mergeRoots(cfg, loc);
  std::string absPath = resolvePathForDelete(effectiveRoot, req.uri);

  struct stat st;
  if (stat(absPath.c_str(), &st) != 0) {
    // 物理ファイルが無い
    return buildErrorResponse(cfg, loc, 404, true);
  }

  // ディレクトリは削除対象外（誤ってrmdir相当の挙動をしないため）
  if (S_ISDIR(st.st_mode)) {
    return buildErrorResponse(cfg, loc, 403, true);
  }

  if (std::remove(absPath.c_str()) == 0) {
    return buildSimpleResponse(204, "No Content", true);
  } else {
    switch (errno) {
      case EACCES:
      case EPERM:
        return buildErrorResponse(cfg, loc, 403, true);
      case ENOENT: // raceで消えていた等
        return buildErrorResponse(cfg, loc, 404, true);
      default:
        return buildErrorResponse(cfg, loc, 500, true);
    }
  }
}

// GET / HEAD 処理
// --- エントリーポイント ---
std::string ResponseBuilder::generateResponse(const Request &req,
                                              const ServerConfig &cfg,
                                              const ServerConfig::Location *loc,
                                              const std::string &locPath) {

  // 1. メソッド許可チェック (Location の method ディレクティブ)
  if (!isMethodAllowed(req.method, loc)) {
    return buildMethodNotAllowed(buildAllowHeader(loc), cfg);
  }

  // 2. メソッド毎に処理を振り分け
  if (req.method == "GET" || req.method == "HEAD") {
    return handleGetLike(req, cfg, loc, locPath);
  }
  if (req.method == "DELETE") {
    return handleDelete(req, cfg, loc, locPath);
  }

  // 未サポートメソッド
  return buildMethodNotAllowed(buildAllowHeader(loc), cfg);
}

// --- GET/HEAD (4引数版ラッパー) ---
std::string ResponseBuilder::handleGetLike(const Request &req,
                                           const ServerConfig &cfg,
                                           const ServerConfig::Location *loc,
                                           const std::string &locPath) {
  (void)locPath; // まだ使用していないが将来的に使う可能性あり
  return handleGetLikeCore(req, cfg, loc, locPath); // 既存の3引数版を再利用
}

// --- DELETE (4引数版ラッパー) ---
std::string ResponseBuilder::handleDelete(const Request &req,
                                          const ServerConfig &cfg,
                                          const ServerConfig::Location *loc,
                                          const std::string &locPath) {
  (void)loc; // Core で使うのでこのまま
  // 1) locPath を剥がして相対URIを得る
  std::string rel = stripLocationPrefix(req.uri, locPath);

  // 2) Core は `req.uri` を見るので、相対をセットした仮の Request を作る
  Request tmp = req;
  // 先頭に '/' を付けておくと joinPath で綺麗に繋がる
  if (rel.empty() || rel[0] != '/')
    tmp.uri = "/" + rel;
  else
    tmp.uri = rel;

  // 3) 共通処理で物理パス解決 & unlink
  return handleDeleteCore(tmp, cfg, loc);
}
