#ifndef CGIPROCESS_HPP
#define CGIPROCESS_HPP

#include <string>
#include <ctime>
#include "RequestParser.hpp"  // Request構造体を使うために必要

struct CgiProcess {
    pid_t pid;
    int inFd;                // CGIへの書き込み用
    int outFd;               // CGIからの読み取り用
    int clientFd;            // このCGIリクエストのクライアントFD
    Request req;
    std::string buffer;      // CGI出力の一時保存
    std::string inputBuffer; // CGIへの入力データ残り
    int events;              // 現在監視するpollイベント (POLLIN / POLLOUT)
    int remainingMs;       // タイムアウトまでの残り時間（ミリ秒）
};

#endif
