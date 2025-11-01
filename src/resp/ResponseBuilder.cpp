#include "resp/ResponseBuilder.hpp"
#include "resp/Mime.hpp"
#include <sys/stat.h>
#include <unistd.h>     // unlink, access, etc
#include <limits.h>     // PATH_MAX
#include <fstream>
#include <sstream>
#include <ctime>
#include <cerrno>
#include <map>
#include <cstring>

// ====== 便利関数======
static bool isMethodAllowed(const std::string &m,
                            const ServerConfig::Location *loc)
{
    if (!loc) return true; // Locationが無いなら全許可とみなす
    if (loc->method.empty()) return true; // 空なら全許可

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
        if (i) oss << ", ";
        oss << loc->method[i];
    }
    return oss.str();
}

// ====== ローカルヘルパ ======

static bool isDirFs(const std::string& p){
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

//使われていなかったので一旦コメントアウト
// static bool isRegFs(const std::string& p){
//     struct stat st;
//     if (stat(p.c_str(), &st) != 0) return false;
//     return S_ISREG(st.st_mode);
// }

static std::string joinPath(const std::string& a, const std::string& b){
    if (a.empty()) return b;
    if (a[a.size()-1] == '/' && b.size() && b[0] == '/') return a + b.substr(1);
    if (a[a.size()-1] != '/' && b.size() && b[0] != '/') return a + "/" + b;
    return a + b;
}

static std::string slurpFile(const std::string& p){
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
            r[i] = char(r[i]-'A'+'a');
        }
    }
    return r;
}

// 簡易 reason phrase (本当は http::Status::reason() があるならそれを使う)
static std::string reasonPhrase(int code) {
    switch (code) {
        case 200: return "OK";
        case 204: return "No Content";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
		case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
    }
    return "Unknown";
}

// cfg.root と loc->root をマージして「最終的に使うルートディレクトリ」を決める。
// ルール:
//  - loc が無い→ cfg.root を返す
//  - loc->root が空→ cfg.root を返す
//  - loc->root が '/' で始まる → プロセスカレント直下扱い (ex: "/upload/" → "./upload/")
//  - それ以外 → cfg.root と連結 (ex: cfg.root="./docs/", loc->root="img/" → "./docs/img/")
std::string ResponseBuilder::mergeRoots(
    const ServerConfig &cfg,
    const ServerConfig::Location *loc
) const {
    if (!loc || loc->root.empty()) {
        return cfg.root;
    }

    const std::string &lr = loc->root;

    // case1: 絶対っぽい書き方 → プロジェクト直下フォルダ扱い
    if (!lr.empty() && lr[0] == '/') {
        // "/upload/" -> "./upload/"
        std::string trimmed = lr;
        // 先頭の'/'を削る
        while (!trimmed.empty() && trimmed[0] == '/')
            trimmed.erase(0, 1);
        // "./" を頭につける
        return std::string("./") + trimmed;
    }

    // case2: 相対 → サーバrootにぶら下げる
    // joinPathは既にstaticヘルパとして定義済み
    return joinPath(cfg.root, lr);
}

// URIからlocationのマウントパスを剥がして、ローカルでの相対パスにする.
// 例: uri="/delete/test_delete.html", locPath="/delete/"
// → "test_delete.html"
// locPathが空なら、先頭の'/'を落としただけのパスにする
std::string ResponseBuilder::stripLocationPrefix(
    const std::string &uri,
    const std::string &locPath
) const {
    if (!locPath.empty() &&
        uri.compare(0, locPath.size(), locPath) == 0)
    {
        std::string rest = uri.substr(locPath.size());
        // 先頭の'/'が二重にならないよう軽く整える
        while (!rest.empty() && rest[0] == '/')
            rest.erase(0,1);
        return rest;
    }

    // fallback: "/"を外して相対に
    std::string tmp = uri;
    while (!tmp.empty() && tmp[0] == '/')
        tmp.erase(0,1);
    return tmp;
}

// ====== ResponseBuilder メンバ ======

// Dateヘッダ向け日付
std::string ResponseBuilder::httpDate_() const {
    char buf[128];
    std::time_t t = std::time(0);
    std::tm g;
    gmtime_r(&t, &g);
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &g);
    return std::string(buf);
}

bool ResponseBuilder::isTraversal(const std::string &uri) const {
    // ".." または 大文字小文字無視の "%2e%2e" を含んだらアウト
    if (uri.find("..") != std::string::npos) return true;
    std::string low = lowerCopy(uri);
    if (low.find("%2e%2e") != std::string::npos) return true;
    return false;
}

// GET/HEAD用: docRoot + uri を解決し、
// - ディレクトリなら index.html を補う
// - realpathでdocRoot外脱出を防ぎたいところだけど、まだ簡易版としてdocRoot直結 + traversalチェックでOKとしておく
std::string ResponseBuilder::resolvePathForGet(
    const std::string &docRoot,
    const std::string &uri,
    bool &isDirOut
) const {
    isDirOut = false;
    std::string target = uri.empty() ? "/" : uri;

    // join
    std::string path = joinPath(docRoot, target);
    // ディレクトリだったら index.html を追加
    if (isDirFs(path)) {
        isDirOut = true;
        path = joinPath(path, "index.html");
    }
    return path;
}

// DELETE用は index.html など補完しない想定
std::string ResponseBuilder::resolvePathForDelete(
    const std::string &docRoot,
    const std::string &uri
) const {
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
std::string ResponseBuilder::buildOkResponseFromFile(
    const std::string &absPath,
    bool headOnly,
    bool close
) {
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
std::string ResponseBuilder::buildMethodNotAllowed(
    const std::string &allow,
    const ServerConfig &cfg
) {
    (void)cfg; // いまcfg未使用だけど将来errorPages使うかもなので引数は残す

    std::string body =
        "<!doctype html><title>Method Not Allowed</title>"
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
    int statusCode,
    const std::string &reason,
    bool close,
    const std::map<std::string, std::string> &extraHeaders
) const {
    std::ostringstream res;
    res << "HTTP/1.1 " << statusCode << " " << reason << "\r\n";
    for (std::map<std::string,std::string>::const_iterator it = extraHeaders.begin();
         it != extraHeaders.end(); ++it)
    {
        res << it->first << ": " << it->second << "\r\n";
    }
    res << "Content-Length: 0\r\n"
        << "Connection: " << (close ? "close" : "keep-alive") << "\r\n"
        << "Date: " << httpDate_() << "\r\n"
        << "Server: webserv/0.1\r\n\r\n";
    return res.str();
}

std::string ResponseBuilder::buildErrorResponseFromFile(
    const std::string &path,
    int code,
    bool close
) const {
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

std::string ResponseBuilder::buildSimpleResponse(
    int statusCode,
    const std::string &reason,
    bool close
) const{
    std::map<std::string,std::string> dummy;
    return buildSimpleResponse(statusCode, reason, close, dummy);
}

std::string ResponseBuilder::buildErrorResponse(
    const ServerConfig &cfg,
    const ServerConfig::Location* loc,
    int statusCode,
    bool close
) const {
    std::string filePath;

    // 1. Location にカスタムページがある場合
    if (loc && loc->ret.count(statusCode)) {
        filePath = loc->ret.at(statusCode);
    }
    // 2. サーバ全体にカスタムページがある場合
    else if (cfg.errorPages.count(statusCode)) {
        filePath = cfg.errorPages.at(statusCode);
    }

    // ファイルがあれば返す
    if (!filePath.empty()) {
        std::ifstream ifs(filePath.c_str(), std::ios::binary);
        if (ifs.is_open()) {
            std::ostringstream oss;
            oss << ifs.rdbuf();
            std::string body = oss.str();
            std::ostringstream res;
            res << "HTTP/1.1 " << statusCode << " " << reasonPhrase(statusCode) << "\r\n"
                << "Content-Type: text/html\r\n"
                << "Content-Length: " << body.size() << "\r\n"
                << "Connection: " << (close ? "close" : "keep-alive") << "\r\n"
                << "Date: " << httpDate_() << "\r\n"
                << "Server: webserv/0.1\r\n\r\n"
                << body;
            return res.str();
        }
    }

    // ファイルがなければ従来どおりシンプル版
    return buildSimpleResponse(statusCode, reasonPhrase(statusCode), close);
}

// --- GET/HEAD 処理 (3引数版) ---
std::string ResponseBuilder::handleGetLikeCore(
    const Request &req,
    const ServerConfig &cfg,
    const ServerConfig::Location *loc
) {
    if (isTraversal(req.uri)) {
        return buildSimpleResponse(403, reasonPhrase(403), true);
    }

    // 最終的なルートを決定
    std::string effectiveRoot = mergeRoots(cfg, loc);
    bool isDirFlag = false;
    std::string absPath = resolvePathForGet(effectiveRoot, req.uri, isDirFlag);

    std::ifstream ifs(absPath.c_str(), std::ios::binary);
    if (!ifs.is_open()) {
        return buildErrorResponse(cfg, loc, 404);
    }

    bool headOnly = (req.method == "HEAD");
    return buildOkResponseFromFile(absPath, headOnly, true);
}

// --- DELETE 処理 (3引数版) ---
std::string ResponseBuilder::handleDeleteCore(
    const Request &req,
    const ServerConfig &cfg,
    const ServerConfig::Location *loc
) {
    std::cout << "[DEBUG] DELETE uri=" << req.uri << std::endl;

    if (isTraversal(req.uri)) {
        std::cout << "[DEBUG] DELETE reject: traversal" << std::endl;
        return buildErrorResponse(cfg, loc, 403, true);
    }

    std::string effectiveRoot = mergeRoots(cfg, loc);
    std::string absPath = resolvePathForDelete(effectiveRoot, req.uri);
    std::cout << "[DEBUG] DELETE resolved path=" << absPath << std::endl;

    struct stat st;
    if (stat(absPath.c_str(), &st) != 0) {
        std::cout << "[DEBUG] DELETE stat failed: " << std::strerror(errno) << std::endl;
        // ★ここ重要：404は buildErrorResponse で確実にボディ付き or カスタムページを返す
        return buildErrorResponse(cfg, loc, 404, true);
    }

    if (unlink(absPath.c_str()) == 0) {
        std::cout << "[DEBUG] DELETE ok -> 204" << std::endl;
        return buildSimpleResponse(204, "No Content", true);
    } else {
        int err = errno;
        std::cout << "[DEBUG] DELETE failed: " << std::strerror(err) << std::endl;
        if (err == EACCES || err == EPERM) {
            return buildErrorResponse(cfg, loc, 403, true);
        }
        return buildErrorResponse(cfg, loc, 500, true);
    }
}

// GET / HEAD 処理
// --- エントリーポイント ---
std::string ResponseBuilder::generateResponse(
    const Request &req,
    const ServerConfig &cfg,
    const ServerConfig::Location *loc,
    const std::string &locPath
) {
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
std::string ResponseBuilder::handleGetLike(
    const Request &req,
    const ServerConfig &cfg,
    const ServerConfig::Location *loc,
    const std::string &locPath
) {
    (void)locPath; // まだ使用していないが将来的に使う可能性あり
    return handleGetLikeCore(req, cfg, loc); // 既存の3引数版を再利用
}

// --- DELETE (4引数版ラッパー) ---
std::string ResponseBuilder::handleDelete(
    const Request &req,
    const ServerConfig &cfg,
    const ServerConfig::Location *loc,
    const std::string &locPath
) {
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

