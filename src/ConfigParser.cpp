#include "../include/ConfigParser.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

std::vector<ServerConfig>
ConfigParser::getServerConfigs(const std::string &path) {
  std::ifstream file;
  init_ServerConfig();
  file.open(path.c_str(), std::ios::in); // 読み込み専用で開く

  if (!file.is_open()) {
    std::cout << path << std::endl;
    throw std::runtime_error("Failed to open configuration file");
  }
  std::string line;
  _inside_server = false;
  _inside_location = false;
  while (std::getline(file, line)) {
    line = trim_first_last_space(line);
    if (line.empty() || line[0] == '#') {
      line.clear();
      continue;
    }
    if (_inside_server == false && _inside_location == false) {
      if (line.substr(0, 6) == "server") {
        line = trim_first_last_space(line.substr(6, line.length()));
        if (line.length() == 1 && line[0] == '{') {
          _inside_server = true;
          init_ServerConfig();
          continue;
        } else {
          throw std::runtime_error(
              "Invalid Configuration File - server format");
        }
      } else {
        throw std::runtime_error("Invalid Configuration File - not server");
      }
    } else if (_inside_server == true && _inside_location == false) {
      if (line.length() == 1 && line[0] == '}') {
        if (!is_necessary_item()) { // 必須の項目がconfig
                                    // fileに記載されていなかったらエラー
          throw std::runtime_error(
              "Invalid Configuration File - Necessary items don't exist");
        }
        _inside_server = false;
        _serverConfigs.push_back(_cfg);
        continue;
      }
    } else if (_inside_server == true && _inside_location == true) {
      if (line.length() == 1 && line[0] == '}') {
        _inside_location = false;
        continue;
      }
    }
    if (!(line.length() > 7 && line.substr(0, 8) == "location") &&
        line[line.length() - 1] != ';') { //";"で終わってなかったらエラー
      throw std::runtime_error("Invalid Configuration File - not }");
    }
    if (!(line.length() > 7 && line.substr(0, 8) == "location")) {
      line = line.substr(0, line.length() - 1);
    }
    parse_server_inside(line);
  }
  if (_inside_server == true) {
    throw std::runtime_error("Invalid Configuration File - not close {}");
  }
  file.close();
  print_configServers(); // for test　後で消す
  return _serverConfigs;
}

bool ConfigParser::is_necessary_item() {
  if (_cfg.port == -1) {
    return false;
  }
  if (_cfg.root == "") {
    return false;
  }
  return true;
}

std::string ConfigParser::trim_first_last_space(const std::string &str) {
  std::string::size_type first = str.find_first_not_of(" \t");
  std::string::size_type last = str.find_last_not_of(" \t");
  if (first == std::string::npos) {
    return "";
  } else {
    return str.substr(first, last - first + 1);
  }
}

std::vector<std::string> ConfigParser::parse_by_space(const std::string &line) {
  std::istringstream iss(line);
  std::vector<std::string> words;
  std::string word;

  while (iss >> word) {
    words.push_back(word);
  }
  return words;
}

void ConfigParser::parse_server_inside(const std::string &str) {
	std::vector<std::string> words;
	words = parse_by_space(str);
	if (_inside_server == true && _inside_location == false) {
		if (words[0] == "listen") {
			if (words.size() != 2) {
				throw std::runtime_error("Invalid Configuration File - listen");
			}
			_cfg.port = std::atoi(words[1].c_str());
		} else if (words[0] == "host") {
			if (words.size() != 2) {
				throw std::runtime_error("Invalid Configuration File - host");
			}
			_cfg.host = words[1];
		} else if (words[0] == "root") {
			if (words.size() != 2) {
				throw std::runtime_error("Invalid Configuration File - root");
			}
			_cfg.root = words[1];
		} else if (words[0] == "error_page") {
			if (words.size() != 3) {
				throw std::runtime_error("Invalid Configuration File - error_page");
			}
			_cfg.errorPages[std::atoi(words[1].c_str())] = words[2];
		} else if (words[0] == "location") {
			if (words.size() != 3 || words[2] != "{") {
				throw std::runtime_error("Invalid Configuration File - location");
			}
			_tmp_location_name = words[1];
			_inside_location = true;
		} else if (words[0] == "server_name") {
			//server_nameは不要
		// 	if (words.size() != 2) {
		// 		throw std::runtime_error("Invalid Configuration File - server_name");
		// 	}
		// 	_cfg.server_name = words[1];
		// } else {
		// 	throw std::runtime_error("Invalid Configuration File - invalid word in server");
		}
	} else if (_inside_server == true && _inside_location == true) {
		if (words[0] == "root") {
			if (words.size() != 2) {
				throw std::runtime_error("Invalid Configuration File - root");
			}
			_cfg.location[_tmp_location_name].root = words[1];
		} else if (words[0] == "autoindex") {
			if (words.size() != 2) {
				throw std::runtime_error("Invalid Configuration File - autoindex");
			}
			_cfg.location[_tmp_location_name].autoindex = words[1];
		} else if (words[0] == "upload_path") {
			if (words.size() != 2) {
				throw std::runtime_error("Invalid Configuration File - upload_path");
			}
			_cfg.location[_tmp_location_name].upload_path = words[1];
		} else if (words[0] == "index") {
			if (words.size() != 2) {
				throw std::runtime_error("Invalid Configuration File - index");
			}
			_cfg.location[_tmp_location_name].index = words[1];
		} else if (words[0] == "max_body_size") {
			if (words.size() != 2) {
				throw std::runtime_error("Invalid Configuration File - max_body_size");
			}
			_cfg.location[_tmp_location_name].max_body_size =
			std::atoi(words[1].c_str());
		} else if (words[0] == "cgi_path") {
			if (words.size() != 2) {
				throw std::runtime_error("Invalid Configuration File - cgi_path");
			}
			_cfg.location[_tmp_location_name].cgi_path = words[1];
		} else if (words[0] == "return") {
			if (words.size() != 3) {
				throw std::runtime_error("Invalid Configuration File - return");
			}
			_cfg.location[_tmp_location_name].ret[std::atoi(words[1].c_str())] = words[2];
		} else if (words[0] == "method") {
			for (size_t i = 1; i < words.size(); ++i) {
				_cfg.location[_tmp_location_name].method.push_back(words[i]);
			}
		} else {
			throw std::runtime_error("Invalid Configuration File - noting");
		}
	}
}

void ConfigParser::init_ServerConfig() {
  _cfg.port = -1;
  _cfg.host = "";
  _cfg.root = "";
//   _cfg.server_name = "";
  _cfg.location.clear();
  _cfg.errorPages.clear();
}

void ConfigParser::print_configServers() {
  std::cout << "==== PRINT CONFIG PARSER FOR TEST (提出時は消す) ==="
            << std::endl;
  for (size_t i = 0; i < _serverConfigs.size(); ++i) {
    const ServerConfig &server = _serverConfigs[i];
    std::cout << "=== Server " << i + 1 << " ===" << std::endl;
    std::cout << "Host: " << server.host << std::endl;
    std::cout << "Port: " << server.port << std::endl;
    std::cout << "Root: " << server.root << std::endl;

    // エラーページ
    std::cout << "Error pages:" << std::endl;
    for (std::map<int, std::string>::const_iterator it =
             server.errorPages.begin();
         it != server.errorPages.end(); ++it) {
      std::cout << "  " << it->first << " -> " << it->second << std::endl;
    }

    // ロケーション
    std::cout << "Locations:" << std::endl;
    for (std::map<std::string, ServerConfig::Location>::const_iterator it =
             server.location.begin();
         it != server.location.end(); ++it) {
      const std::string &path = it->first;
      const ServerConfig::Location &loc = it->second;
      std::cout << "- Path: " << path << std::endl;
      std::cout << "  Root: " << loc.root << std::endl;
      std::cout << "  Autoindex: " << loc.autoindex << std::endl;
      std::cout << "  Upload path: " << loc.upload_path << std::endl;
      std::cout << "  Index: " << loc.index << std::endl;
      std::cout << "  Max body size: " << loc.max_body_size << std::endl;
      std::cout << "  CGI path: " << loc.cgi_path << std::endl;

      if (!loc.ret.empty()) {
        std::cout << "  Ret redirects:" << std::endl;
        for (std::map<int, std::string>::const_iterator rit = loc.ret.begin();
             rit != loc.ret.end(); ++rit) {
          std::cout << "    " << rit->first << " -> " << rit->second
                    << std::endl;
        }
      }
    }

    std::cout << std::endl;
  }
}
