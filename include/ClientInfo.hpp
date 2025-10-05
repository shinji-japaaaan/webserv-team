#ifndef CLIENTINFO_HPP
#define CLIENTINFO_HPP

#include <string>

struct ClientInfo {
    std::string recvBuffer;    // 受信バッファ
    std::string sendBuffer;    // 送信バッファ
    bool requestComplete;      // リクエスト受信完了フラグ

    ClientInfo() : recvBuffer(""), sendBuffer(""), requestComplete(false) {}
};

#endif
