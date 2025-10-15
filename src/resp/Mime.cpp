#include "resp/Mime.hpp"
#include <map>
#include <string>

// --- そのまま流用OK ---
static std::string extOf(const std::string& path) {
    std::string::size_type p = path.rfind('.');
    return (p == std::string::npos) ? "" : path.substr(p);
}

// C++98対応: 初回呼び出し時に map を組み立てる
static const std::map<std::string, std::string>& mimeMap() {
    static std::map<std::string, std::string> m;
    if (m.empty()) {
        m.insert(std::make_pair(".html", "text/html"));
        m.insert(std::make_pair(".htm",  "text/html"));
        m.insert(std::make_pair(".css",  "text/css"));
        m.insert(std::make_pair(".js",   "application/javascript"));
        m.insert(std::make_pair(".png",  "image/png"));
        m.insert(std::make_pair(".jpg",  "image/jpeg"));
        m.insert(std::make_pair(".jpeg", "image/jpeg"));
        m.insert(std::make_pair(".gif",  "image/gif"));
        m.insert(std::make_pair(".ico",  "image/x-icon"));
        m.insert(std::make_pair(".txt",  "text/plain"));
    }
    return m;
}

std::string mime::fromPath(const std::string& path) {
    std::string ext = extOf(path);
    const std::map<std::string,std::string>& m = mimeMap();
    std::map<std::string,std::string>::const_iterator it = m.find(ext);
    return (it != m.end()) ? it->second : "application/octet-stream";
}