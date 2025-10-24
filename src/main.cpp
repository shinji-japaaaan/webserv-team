#include "ServerManager.hpp"

int main() {
    ServerManager manager;
    if (!manager.loadConfig("./conf/config2.conf"))
        return 1;
    if (!manager.initAllServers())
        return 1;
    manager.runAllServers();
    return 0;
}
