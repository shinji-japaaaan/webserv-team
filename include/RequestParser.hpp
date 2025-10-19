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

public:
  RequestParser();
  bool isRequestComplete(const std::string &buffer);
  Request parse(const std::string &buffer);
  size_t getParsedLength() const { return parsedLength; }
};

void printRequest(const Request &req);
