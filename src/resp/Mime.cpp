#include "resp/Mime.hpp"
#include <map>

//ファイルの拡張子から、返すファイルの種類を判別するコード
//MIMEはMultipurpose Internet Mail Extensionsの省略
static const std::map<std::string, std::string> kMime = {
    {".html","text/html"}, {".htm","text/html"},
    {".css","text/css"},   {".js","application/javascript"},
    {".png","image/png"},  {".jpg","image/jpeg"},
    {".jpeg","image/jpeg"},{".gif","image/gif"},
    {".ico","image/x-icon"},{".txt","text/plain"}
};

static std::string extOf(const std::string& path) {
    std::string::size_type p = path.rfind('.');
    return (p == std::string::npos) ? "" : path.substr(p);
}

std::string mime::fromPath(const std::string& path) {
    std::string ext = extOf(path);
    std::map<std::string,std::string>::const_iterator it = kMime.find(ext);
    return (it != kMime.end()) ? it->second : "application/octet-stream";
}