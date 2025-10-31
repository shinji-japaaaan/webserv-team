#include "../include/RequestParser.hpp"

bool RequestParser::isRequestComplete(const std::string &buffer) {
    size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return false; // ヘッダー未到着

    // Content-Length を取得
    std::string headerPart = buffer.substr(0, headerEnd);
    size_t contentLength = 0;
    std::istringstream stream(headerPart);
    std::string line;
    while (std::getline(stream, line)) {
        std::string key = line.substr(0, line.find(":"));
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        if (key == "content-length") {
            std::string val = line.substr(line.find(":")+1);
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t") + 1);
            contentLength = std::strtoul(val.c_str(), NULL, 10);
        }
    }

    // body が届いているか
    size_t bodySize = buffer.size() - (headerEnd + 4);
    return bodySize >= contentLength;
}

RequestParser::RequestParser()
    : parsedLength(0){}

Request RequestParser::parse(const std::string &buffer) {
    Request req;
    parsedLength = 0;

    // ヘッダ終了位置
    size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return req;

    std::string headerPart = buffer.substr(0, headerEnd);
    std::string bodyPart = buffer.substr(headerEnd + 4);

    std::istringstream stream(headerPart);
    std::string line;

    // リクエストライン
    if (!std::getline(stream, line))
        return req;

    std::istringstream lineStream(line);
    lineStream >> req.method >> req.uri >> req.version;

    if (req.version != "HTTP/1.0" && req.version != "HTTP/1.1") {
        req.method.clear();
        return req;
    }

    // ヘッダ解析
    std::string lastKey;
    while (std::getline(stream, line)) {
        if (line.empty() || line == "\r") break;

        if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
            if (!lastKey.empty())
                req.headers[lastKey] += " " + line.substr(1);
            continue;
        }

        size_t pos = line.find(":");
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r") + 1);

        req.headers[key] = value;
        lastKey = key;
    }

    // Body処理
    std::map<std::string, std::string>::iterator it = req.headers.find("Content-Length");
    if (it != req.headers.end()) {
        size_t contentLength = std::strtoul(it->second.c_str(), NULL, 10);
        if (bodyPart.size() >= contentLength) {
            req.body = bodyPart.substr(0, contentLength);
            parsedLength = headerEnd + 4 + contentLength;
        }
    } else {
        parsedLength = headerEnd + 4;
    }

    return req;
}

// void printRequest(const Request &req) {
//   std::cout << "=== Request ===" << std::endl;
//   std::cout << "Method: " << req.method << std::endl;
//   std::cout << "URI: " << req.uri << std::endl;

//   std::cout << "Headers:" << std::endl;
//   std::map<std::string, std::string>::const_iterator it;
//   for (it = req.headers.begin(); it != req.headers.end(); ++it) {
//     std::cout << "  " << it->first << ": " << it->second << std::endl;
//   }

//   std::cout << "Body: " << req.body << std::endl;
//   std::cout << "================" << std::endl;
// }
