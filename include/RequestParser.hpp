#pragma once
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <iostream>

struct Request {
  std::string method;
  std::string uri;
  std::string version;
  std::map<std::string, std::string> headers;
  std::string body;
};

class RequestParser {
private:
  size_t parsedLength;
  bool isClearlyInvalidRequest(const std::string &buffer);

public:
  RequestParser();
  bool isRequestComplete(const std::string &buffer);
  Request parse(const std::string &buffer);
  size_t getParsedLength() const { return parsedLength; }
  std::map<std::string, std::string> parseHeaders(const std::string &headerPart);
};

// void printRequest(const Request &req);
