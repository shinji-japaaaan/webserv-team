#include "../include/RequestParser.hpp"

bool RequestParser::isRequestComplete(const std::string &buffer) {
  // ヘッダー終了位置（\r\n\r\n）を探す
  size_t headerEnd = buffer.find("\r\n\r\n");
  if (headerEnd == std::string::npos) {
    return false; // ヘッダーがまだ届いていない
  }
  return true;
}

RequestParser::RequestParser()
    : parsedLength(0){}

Request RequestParser::parse(const std::string &buffer) {
  Request req;
  parsedLength = 0;

  // ヘッダー終了位置
  size_t headerEnd = buffer.find("\r\n\r\n");

  std::string headerPart = buffer.substr(0, headerEnd);
  std::string bodyPart = buffer.substr(headerEnd + 4); // header部分+\r\n\r\nがbody部分

  std::istringstream stream(headerPart);
  std::string line;

  // --- 1行目（リクエストライン） ---
  if (!std::getline(stream, line))
    return req;
  {
    std::istringstream lineStream(line);
    lineStream >> req.method >> req.uri >> req.version;
  }
  // ✅ HTTPバージョンを確認
  if (req.version != "HTTP/1.0" && req.version != "HTTP/1.1") {
    std::cerr << "Unsupported HTTP version: " << req.version << std::endl;
    req.method = "";  // 無効化（上位で検出できるように）
    return req;       // バージョンが不正なので即リターン

  // --- ヘッダー部 ---
  while (std::getline(stream, line)) {
    if (line == "\r" || line.empty())
      break;
    size_t pos = line.find(":");
    if (pos == std::string::npos)
      continue;
    std::string key = line.substr(0, pos);
    std::string value = line.substr(pos + 1);

    // トリム
    key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);

    req.headers[key] = value;
  }

  // --- Body ---
  std::map<std::string, std::string>::iterator it = req.headers.find("Content-Length");
  if (it != req.headers.end()) {
    size_t len = static_cast<size_t>(std::strtoul(it->second.c_str(), NULL, 10));
    if (bodyPart.size() >= len) {
      req.body = bodyPart.substr(0, len);
    }
  }

  parsedLength = headerEnd + 4 + req.body.size();
  return req;
}

void printRequest(const Request &req) {
  std::cout << "=== Request ===" << std::endl;
  std::cout << "Method: " << req.method << std::endl;
  std::cout << "URI: " << req.uri << std::endl;

  std::cout << "Headers:" << std::endl;
  std::map<std::string, std::string>::const_iterator it;
  for (it = req.headers.begin(); it != req.headers.end(); ++it) {
    std::cout << "  " << it->first << ": " << it->second << std::endl;
  }

  std::cout << "Body: " << req.body << std::endl;
  std::cout << "================" << std::endl;
}
