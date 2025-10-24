#ifndef CLIENTINFO_HPP
#define CLIENTINFO_HPP

#include <string>
#include "RequestParser.hpp"

struct ClientInfo {
    std::string recvBuffer;    // 受信バッファ
    std::string sendBuffer;    // 送信バッファ
    bool requestComplete;      // リクエスト受信完了フラグ
	Request currentRequest;
    bool shouldClose;         // 送信後に接続を閉じるフラグ

    ClientInfo(): recvBuffer(""), sendBuffer(""), requestComplete(false), currentRequest() {}
};

#endif
