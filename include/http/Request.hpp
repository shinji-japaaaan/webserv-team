#pragma once
#include <string>
#include <map>

struct Request {
    std::string method;     // "GET" など
    std::string uri;        // "/index.html"
    std::string version;    // "HTTP/1.1"
    std::map<std::string, std::string> headers;
    std::string body;
    bool connectionClose;   // 今は true 固定でOK（次PRでkeep-alive化）
};
