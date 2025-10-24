#include "../include/ConfigParser.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

std::vector<ServerConfig> ConfigParser::getServerConfigs(const std::string &path) {
  std::ifstream file;
  file.open(path.c_str(), std::ios::in); // 読み込み専用で開く

  if (!file.is_open()) {
    {
	std::cout<< path << std::endl;
      throw std::runtime_error("Failed to open configuration file");
    }
  }

  std::string line;
  bool inside_brace = false;
  while (std::getline(file, line)) {
    line = trim_first_last_space(line);
    if (line.empty() || line[0] == '#') {
      line.clear();
      continue;
    }
    // std::cout << line << std::endl;
	if (inside_brace == false){
        if (line.substr(0, 6) == "server"){
			line = trim_first_last_space(line.substr(6,line.length()));
			if (line.length() == 1 && line[0] == '{'){
				inside_brace = true;
				init_ServerConfig();
				continue;
			}
			else{
				throw std::runtime_error("Invalid Configuration File");
            }
		}
		else{
			throw std::runtime_error("Invalid Configuration File");
		}
	}
	else{
		if (line.length() == 1 && line[0] == '}') {
			inside_brace = false;
			_serverConfigs.push_back(_cfg);
			continue;
		}
		if (line[line.length()-1] != ';'){
                  throw std::runtime_error("Invalid Configuration File");
        }
		line = line.substr(0,line.length()-1);
		parse_space(line);
        }
  }
  if (inside_brace == true){
    throw std::runtime_error("Invalid Configuration File");
  }
  file.close();
//   print_configServers(); // for test
  return _serverConfigs;
}

std::string ConfigParser::trim_first_last_space(const std::string &str) {
  std::string::size_type first = str.find_first_not_of(" \t");
  std::string::size_type last = str.find_last_not_of(" \t");
  if (first == std::string::npos){
	return "";
  }
  else{
	return str.substr(first, last - first + 1);
  }
}

void ConfigParser::parse_space(const std::string &str) {
  std::istringstream iss(str);
  std::string word;
  std::string first_word;
  int second_word;
  bool is_first_word = true;
  int i = 0;
  while (iss >> word) {
	if (is_first_word == true)
	{
		is_first_word = false;
		first_word = word;
	}
	else{
		if ((first_word == "listen" || first_word == "host" || first_word == "root") && (i != 1))
		{
                  throw std::runtime_error("Invalid Configuration File");
        }
        if (first_word == "error_page" && i > 2){
          throw std::runtime_error("Invalid Configuration File");
        }
		if (first_word == "listen")
		{
                  _cfg.port = std::atoi(word.c_str());
        }
		else if (first_word == "host")
		{
			_cfg.host = word;
		}
		else if (first_word == "root")
		{
			_cfg.root = word;
		}
		else if (first_word == "error_page" && i == 1)
		{
			second_word = std::atoi(word.c_str());
		}
		else if (first_word == "error_page" && i == 2)
		{
			_cfg.errorPages[second_word] = word;
		}
		else
		{

                  throw std::runtime_error("Invalid Configuration File");
		}
	}
    i += 1;
  }
}

void ConfigParser::init_ServerConfig() {
  _cfg.port = 0;
  _cfg.host = "";
  _cfg.root = "";
  _cfg.errorPages.clear();
  _cfg.clientMaxBodySize = 512; // ← ここでデフォルト512B
}

void ConfigParser::print_configServers(){
  for (size_t i = 0; i < _serverConfigs.size(); ++i) {
    const ServerConfig &server = _serverConfigs[i];
    std::cout << "=== Server " << i + 1 << " ===" << std::endl;
    std::cout << "Host: " << server.host << std::endl;
    std::cout << "Port: " << server.port << std::endl;
    std::cout << "Root: " << server.root << std::endl;

    std::cout << "Error pages:" << std::endl;
    for (std::map<int, std::string>::const_iterator it =
             server.errorPages.begin();
         it != server.errorPages.end(); ++it) {
      std::cout << "  " << it->first << " -> " << it->second << std::endl;
    }
    std::cout << std::endl;
  }
}

