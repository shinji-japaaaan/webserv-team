#include "ServerManager.hpp"

int main(int argc, char *argv[]) {
	std::string conf_file_path = "./conf/config.conf";
	if (argc > 2)
	{
		std::cerr << "Argument is more than 2" << std::endl;
		return 1;
	}
	if (argc == 2)
	{
		conf_file_path = argv[1];
	}
    ServerManager manager;
    if (!manager.loadConfig(conf_file_path))
        return 1;
    if (!manager.initAllServers())
        return 1;
    manager.runAllServers();
    return 0;
}
