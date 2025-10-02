#include "Server.hpp"

int main() {
    Server server(8080);   // 8080ポートでサーバー作成

    if (!server.init()) return 1; // 初期化失敗時は終了

    server.run(); // クライアント接続開始
    return 0;
}
