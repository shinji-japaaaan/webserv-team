#include "log.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>

// ★ タイムスタンプを取得
std::string getTimeStamp() {
    std::time_t t = std::time(NULL);
    std::tm *tm_ptr = std::localtime(&t);

    std::ostringstream oss;
    oss << (tm_ptr->tm_year + 1900) << "-"
        << (tm_ptr->tm_mon + 1) << "-"
        << tm_ptr->tm_mday << " "
        << tm_ptr->tm_hour << ":"
        << tm_ptr->tm_min << ":"
        << tm_ptr->tm_sec;

    return oss.str();
}

// ★ 通常ログ出力
void logMessage(LogLevel level, const std::string &msg) {
    const char *levelStr[] = {"INFO", "WARNING", "ERROR"};

    std::string logLine = "[" + getTimeStamp() + "] "
        + levelStr[level] + ": " + msg;

    std::cout << logLine << std::endl;

    // --- 任意: ファイル出力を有効化したい場合はコメント解除 ---
    // std::ofstream ofs("server.log", std::ios::app);
    // ofs << logLine << std::endl;
}

// ★ エラーログ出力
void logError(const std::string &func, const std::string &msg) {
    std::string logLine = "[" + getTimeStamp() + "] ERROR (" + func + "): " + msg;
    std::cerr << logLine << std::endl;

    // --- 任意: ファイル出力 ---
    // std::ofstream ofs("server.log", std::ios::app);
    // ofs << logLine << std::endl;
}
