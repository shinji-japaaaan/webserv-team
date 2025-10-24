#pragma once
#include <map>
#include <string>
#include <vector>

struct ServerConfig {
  int port;
  std::string host;
  std::string root;
  std::map<int, std::string> errorPages;
  size_t clientMaxBodySize;
};

class ConfigParser {
private:
  std::vector<ServerConfig> _serverConfigs;
  ServerConfig _cfg;

  void parseServerBlock(const std::vector<std::string> &lines);
  std::string trim_first_last_space(const std::string &input);
  void parse_space(const std::string &str);
  void init_ServerConfig();
  void print_configServers();

public:
//   void parse(const std::string &path);
  std::vector<ServerConfig> getServerConfigs(const std::string &path);
};
