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
    int elapsedLoops;        // pollループ回数（タイムアウト管理）
    bool activeInLastPoll;   // 直近pollで動作があったか
    time_t startTime;        // CGI開始時刻
    std::string inputBuffer; // CGIへの入力データ残り
    int events;              // 現在監視するpollイベント (POLLIN / POLLOUT)
};

#endif
