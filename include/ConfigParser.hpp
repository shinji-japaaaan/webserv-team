#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

struct ServerConfig {
  int port;
  //   std::string server_name;
  std::string host;
  std::string root;
  std::map<int, std::string> errorPages;
  struct Location {
    std::string root;
    std::string autoindex;
    std::string upload_path;
    std::string index;
    size_t max_body_size;
    std::string cgi_path;
	  std::vector<std::string> method;
    std::map<int, std::string> ret;
  };
  std::map<std::string, Location> location;
};

class ConfigParser {
private:
  std::vector<ServerConfig> _serverConfigs;
  ServerConfig _cfg;
  bool _inside_server;
  bool _inside_location;
  std::string _tmp_location_name;

  void parseServerBlock(const std::vector<std::string> &lines);
  std::string trim_first_last_space(const std::string &input);
  std::vector<std::string> parse_by_space(const std::string &str);
  void parse_server_inside(const std::string &str);
  void init_ServerConfig();
  // void print_configServers();
//   void printLocation(const Location &loc);
  void printLocation(const ServerConfig::Location &loc);
  bool is_necessary_item();
  bool is_duplicate_item(std::string nest, std::string item,
                                       ServerConfig::Location *loc);

public:
  //   void parse(const std::string &path);
  std::vector<ServerConfig> getServerConfigs(const std::string &path);
};
