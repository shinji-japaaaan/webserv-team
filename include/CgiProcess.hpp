#ifndef CGIPROCESS_HPP
#define CGIPROCESS_HPP

#include <sys/types.h>
#include <string>
#include "RequestParser.hpp"

    struct CgiProcess {
        pid_t pid;
        int inFd;      // CGIへの書き込み用
        int outFd;     // CGIからの読み取り用
        int clientFd;  // ←追加: このCGIリクエストのクライアントFD
        Request req;
        std::string buffer;  // ←追加: CGI出力を一時的に蓄積
        int elapsedLoops;    // poll ループ数タイムアウト用
        bool activeInLastPoll;
        time_t startTime;  // CGIプロセス開始時刻
    };

#endif
