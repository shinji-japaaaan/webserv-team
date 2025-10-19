#include "ServerManager.hpp"

int main() {
    try {
        ServerManager manager;
        if (!manager.loadConfig("./conf/config.conf"))
            return 1;
        if (!manager.initAllServers())
            return 1;
        manager.runAllServers();
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
