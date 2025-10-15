#ifndef CLIENTINFO_HPP
#define CLIENTINFO_HPP

#include <string>
#include "RequestParser.hpp"

struct ClientInfo {
    std::string recvBuffer;    // 受信バッファ
    std::string sendBuffer;    // 送信バッファ
    bool requestComplete;      // リクエスト受信完了フラグ
	Request currentRequest;

    ClientInfo(): recvBuffer(""), sendBuffer(""), requestComplete(false), currentRequest() {}
};

#endif
