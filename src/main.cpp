#include "Server.hpp"

int main() {
    Server server(8080);

    if (!server.init()) {
        return 1;
    }

    server.run();
    return 0;
}
