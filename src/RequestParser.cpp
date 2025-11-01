#include "../include/RequestParser.hpp"

bool RequestParser::isRequestComplete(const std::string &buffer) {
    size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return false;

    std::string headerPart = buffer.substr(0, headerEnd);
    std::map<std::string, std::string> headers = parseHeaders(headerPart);

    bool isChunked = false;
    size_t contentLength = 0;

    if (headers.find("transfer-encoding") != headers.end() &&
        headers["transfer-encoding"].find("chunked") != std::string::npos)
        isChunked = true;

    if (headers.find("content-length") != headers.end())
        contentLength = std::strtoul(headers["content-length"].c_str(), NULL, 10);

    std::string bodyPart = buffer.substr(headerEnd + 4);

    if (isChunked)
        return bodyPart.find("0\r\n\r\n") != std::string::npos;
    else
        return bodyPart.size() >= contentLength;
}

RequestParser::RequestParser()
    : parsedLength(0){}

std::string unchunkBody(const std::string &chunkedBody) {
    std::string unchunked;
    std::istringstream stream(chunkedBody);
    std::string line;

    while (std::getline(stream, line)) {
        if (line == "\r" || line.empty()) continue;

        // chunk size 行
        size_t chunkSize = std::strtoul(line.c_str(), NULL, 16);
        if (chunkSize == 0)
            break;

        char *buf = new char[chunkSize + 2];
        stream.read(buf, chunkSize + 2); // データ＋CRLFを読み飛ばす
        unchunked.append(buf, chunkSize);
        delete[] buf;
    }
    return unchunked;
}

Request RequestParser::parse(const std::string &buffer) {
    Request req;
    parsedLength = 0;

    size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return req;

    std::string headerPart = buffer.substr(0, headerEnd);
    std::string bodyPart   = buffer.substr(headerEnd + 4);

    // ✅ 同じ関数を利用してヘッダ解析（小文字化も共通化）
    req.headers = parseHeaders(headerPart);

    std::istringstream lineStream(headerPart);
    std::string requestLine;
    std::getline(lineStream, requestLine);
    std::istringstream requestLineStream(requestLine);
    requestLineStream >> req.method >> req.uri >> req.version;

    bool isChunked = false;
    if (req.headers.find("transfer-encoding") != req.headers.end() &&
        req.headers["transfer-encoding"].find("chunked") != std::string::npos)
        isChunked = true;

    if (isChunked) {
        req.body = bodyPart.substr(0, bodyPart.find("0\r\n\r\n") + 5);
        req.body = unchunkBody(req.body);
        parsedLength = headerEnd + 4 + bodyPart.find("0\r\n\r\n") + 5;
    } else {
        std::map<std::string, std::string>::iterator it = req.headers.find("content-length");
        if (it != req.headers.end()) {
            size_t len = std::strtoul(it->second.c_str(), NULL, 10);
            req.body = bodyPart.substr(0, len);
            parsedLength = headerEnd + 4 + len;
        } else {
            parsedLength = headerEnd + 4;
        }
    }

    return req;
}

std::map<std::string, std::string> RequestParser::parseHeaders(const std::string &headerPart) {
    std::map<std::string, std::string> headers;
    std::istringstream stream(headerPart);
    std::string line;

    while (std::getline(stream, line)) {
        size_t pos = line.find(":");
        if (pos == std::string::npos)
            continue;

        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);

        // ここで小文字化
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        // トリム
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t\r") + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t\r") + 1);

        headers[key] = val;
    }

    return headers;
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
