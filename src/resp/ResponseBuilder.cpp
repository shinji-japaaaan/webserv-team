#include "resp/ResponseBuilder.hpp"
#include "resp/Mime.hpp"
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <ctime>

//HTTPレスポンスを生成しているコードたち

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

//ファイルを読み取って文字列として返す（画像などのバイナリ含む）
static std::string readFile_(const std::string& p){
    std::ifstream ifs(p.c_str(), std::ios::in | std::ios::binary);
    std::ostringstream oss; oss << ifs.rdbuf(); return oss.str();
}

//現在時刻を「HTTP用フォーマット」で作る（Dateヘッダ用）
std::string ResponseBuilder::httpDate_() const {
    char buf[128]; std::time_t t = std::time(0); std::tm g;
    gmtime_r(&t, &g);
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &g);
    return std::string(buf);
}

//404や405などのエラー応答を作る
std::string ResponseBuilder::buildError_(int code, const std::string& msg, bool close){
    std::string body = "<!doctype html><title>" + msg + "</title><h1>" + msg + "</h1>";
    std::ostringstream res;
    res << "HTTP/1.1 " << code << " " << msg << "\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: " << (close ? "close" : "keep-alive") << "\r\n"
        << "Date: " << httpDate_() << "\r\n"
        << "Server: webserv/0.1\r\n\r\n"
        << body;
    return res.str();
}

//ファイルが見つかった場合の「200 OK」レスポンスを組み立てる関数
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

//リクエストを受けて、適切なレスポンスを作る本体関数
std::string ResponseBuilder::build(const Request& req,
                                   const std::string& docRoot,
                                   const std::string& indexName)
{
    // 1) メソッド制限（今はGET/HEADのみ）
    if (req.method != "GET" && req.method != "HEAD") {
        return buildError_(405, "Method Not Allowed", true);
    }

    // 2) パス結合＆index解決
    std::string uri = req.uri.empty() ? "/" : req.uri;
    // かんたん防御：ディレクトリトラバーサル抑止（本格化は後PR）
    if (uri.find("..") != std::string::npos) return buildError_(403, "Forbidden", true);

    std::string path = join_(docRoot, uri);
    if (isDir_(path)) path = join_(path, indexName);

    if (!fileExists_(path)) {
        // assets/errors/404.html があればそれを返す（任意）
        std::string custom404 = join_("assets/errors", "404.html");
        if (fileExists_(custom404)) {
            std::string body = readFile_(custom404);
            std::ostringstream res;
            res << "HTTP/1.1 404 Not Found\r\n"
                << "Content-Type: text/html\r\n"
                << "Content-Length: " << body.size() << "\r\n"
                << "Connection: close\r\n"
                << "Date: " << httpDate_() << "\r\n"
                << "Server: webserv/0.1\r\n\r\n"
                << body;
            return res.str();
        }
        return buildError_(404, "Not Found", true);
    }

    // 3) HEADはボディ無し
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