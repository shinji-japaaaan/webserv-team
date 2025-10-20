#include "resp/ResponseBuilder.hpp"
#include "resp/Mime.hpp"
#include "resp/ErrorPages.hpp"     // resp::ErrorPages
#include "http/HttpStatus.hpp"     // http::Status
#include "ServerManager.hpp"       // ServerConfig（定義がここにある前提）
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <ctime>

// パスがフォルダか・ファイルかを確認するヘルパー
//HTTPレスポンスを生成しているコードたち

// 追加：仕様名に合わせたラッパー
std::string ResponseBuilder::generateResponse(const Request& req) {
    // TODO: BのConfig/Routeが入ったら docRoot/index を外から渡す設計に差し替え
    return build(req, "./www", "index.html");
}

//パスがフォルダか・ファイルかを確認するヘルパー
static bool isDir_(const std::string& p){
    struct stat st; if (stat(p.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}
static bool fileExists_(const std::string& p){
    struct stat st; return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
static std::string join_(const std::string& a, const std::string& b){
    if (a.empty()) return b;
    if (a[a.size()-1] == '/' && b.size() && b[0] == '/') return a + b.substr(1);
    if (a[a.size()-1] != '/' && b.size() && b[0] != '/') return a + "/" + b;
    return a + b;
}

// ファイルを読み取って文字列として返す（画像などのバイナリ含む）
static std::string readFile_(const std::string& p){
    std::ifstream ifs(p.c_str(), std::ios::in | std::ios::binary);
    std::ostringstream oss; oss << ifs.rdbuf(); return oss.str();
}

// 現在時刻を「HTTP用フォーマット」で作る（Dateヘッダ用）
std::string ResponseBuilder::httpDate_() const {
    char buf[128]; std::time_t t = std::time(0); std::tm g;
    gmtime_r(&t, &g);
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &g);
    return std::string(buf);
}

// エラー応答を作る（ErrorPages + HttpStatus を使用）
// allowHdr は 405 のときだけ付与（空なら付けない）
std::string ResponseBuilder::buildError_(int code,
                                         bool close,
                                         const std::string& docRoot,
                                         const std::string& allowHdr /* = "" */) {
    ServerConfig tmp; tmp.root = docRoot;
    std::string body = resp::ErrorPages::getErrorBody(tmp, code);
    const std::string& reason = http::Status::reason(code);

    std::ostringstream res;
    res << "HTTP/1.1 " << code << " " << reason << "\r\n";
    if (code == 405 && !allowHdr.empty())
        res << "Allow: " << allowHdr << "\r\n";
    res << "Content-Type: " << resp::ErrorPages::contentType() << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: " << (close ? "close" : "keep-alive") << "\r\n"
        << "Date: " << httpDate_() << "\r\n"
        << "Server: webserv/0.1\r\n\r\n"
        << body;
    return res.str();
}

// ファイルが見つかった場合の「200 OK」レスポンスを組み立てる関数
std::string ResponseBuilder::buildStatic200_(const std::string& absPath, bool close){
    std::string body = readFile_(absPath);
    std::string ct = mime::fromPath(absPath);
    std::ostringstream res;
    res << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: " << ct << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: " << (close ? "close" : "keep-alive") << "\r\n"
        << "Date: " << httpDate_() << "\r\n"
        << "Server: webserv/0.1\r\n\r\n"
        << body;
    return res.str();
}

// リクエストを受けて、適切なレスポンスを作る本体関数
std::string ResponseBuilder::build(const Request& req,
                                   const std::string& docRoot,
                                   const std::string& indexName)
{
    // 1) メソッド制限（今はGET/HEADのみ）
    if (req.method != "GET" && req.method != "HEAD") {
        // 新シグネチャで Allow を付与（暫定：GET, HEAD）
        return buildError_(405, /*close=*/true, docRoot, "GET, HEAD");
    }

    // 2) パス結合＆index解決
    std::string uri = req.uri.empty() ? "/" : req.uri;

    // かんたん防御：ディレクトリトラバーサル抑止（%2e%2e もチェック）
    std::string lower = uri;
    for (size_t i = 0; i < lower.size(); ++i) {
        char c = lower[i];
        if ('A' <= c && c <= 'Z') lower[i] = c - 'A' + 'a';
    }
    if (uri.find("..") != std::string::npos ||
        lower.find("%2e%2e") != std::string::npos) {
        return buildError_(403, /*close=*/true, docRoot);
    }

    std::string path = join_(docRoot, uri);
	// 結合後にも ".." が残っていたら拒否（/a/../b など）
    if (path.find("..") != std::string::npos) {
        return buildError_(403, /*close=*/true, docRoot);
    }
    if (isDir_(path)) path = join_(path, indexName);

    // 3) 存在しない → 404 を ErrorPages で統一生成
    if (!fileExists_(path)) {
        return buildError_(404, /*close=*/true, docRoot);
    }

    // 4) HEADはボディ無し
    if (req.method == "HEAD") {
        std::string ct = mime::fromPath(path);
        std::ostringstream res;
        res << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: " << ct << "\r\n"
            << "Content-Length: " << readFile_(path).size() << "\r\n"
            << "Connection: close\r\n"
            << "Date: " << httpDate_() << "\r\n"
            << "Server: webserv/0.1\r\n\r\n";
        return res.str();
    }

    return buildStatic200_(path, /*close=*/true); // 既存挙動に合わせてclose。次PRでkeep-alive化
}