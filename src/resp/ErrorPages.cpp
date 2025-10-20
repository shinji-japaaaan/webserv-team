#include "resp/ErrorPages.hpp"
#include "http/HttpStatus.hpp"
#include "ServerManager.hpp" // ServerConfig の構造体に root がある想定

#include <fstream>
#include <sstream>
#include <map>
#include <string>

namespace {

static bool readFileToString(const std::string& path, std::string& out) {
    std::ifstream ifs(path.c_str(), std::ios::in | std::ios::binary);
    if (!ifs)
        return false;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    out = oss.str();
    return true;
}

} // anonymous namespace

namespace resp {

std::string ErrorPages::getErrorBody(const ServerConfig& srv, int status) {
    // 1) サーバ設定にエラーページ指定がある場合
    std::map<int, std::string>::const_iterator it = srv.errorPages.find(status);
    if (it != srv.errorPages.end()) {
        std::string body;
        if (readFileToString(it->second, body))
            return body;
    }

    // 2) root/assets/errors/<code>.html の存在チェック
    {
        std::ostringstream oss;
        oss << status;
        const std::string code = oss.str();

        if (!srv.root.empty()) {
            std::string cand = srv.root;
            if (cand[cand.size() - 1] != '/')
                cand += "/";
            cand += "assets/errors/" + code + ".html";
            std::string body;
            if (readFileToString(cand, body))
                return body;
        }
    }

    // 3) プロジェクトルート（カレント）から assets/errors/<code>.html
    {
        std::ostringstream oss;
        oss << status;
        const std::string code = oss.str();

        std::string cand = "assets/errors/" + code + ".html";
        std::string body;
        if (readFileToString(cand, body))
            return body;
    }

    // 4) 既知でなければ 500 としてデフォルトHTMLを返す
    if (!http::Status::known(status))
        status = 500;
    return defaultHtml(status);
}

const std::string& ErrorPages::contentType() {
    static const std::string kType("text/html; charset=utf-8");
    return kType;
}

std::string ErrorPages::defaultHtml(int status) {
    const std::string& reason = http::Status::reason(status);
    std::ostringstream oss;
    oss << "<!DOCTYPE html>"
           "<html><head><meta charset=\"utf-8\">"
           "<title>" << status << " " << reason << "</title>"
           "<style>"
           "body{font-family:sans-serif;margin:40px;color:#222}"
           "h1{font-size:1.6rem;margin-bottom:.5rem}"
           "p{color:#666}"
           "</style></head><body>"
           "<h1>" << status << " " << reason << "</h1>"
           "<p>The server responded with an error.</p>"
           "</body></html>";
    return oss.str();
}

} // namespace resp