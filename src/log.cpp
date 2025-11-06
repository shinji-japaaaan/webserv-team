#include "log.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <unistd.h>

// ★ タイムスタンプを取得
std::string getTimeStamp() {
    static unsigned long counter = 0;
    int pid = getpid();  // 許可関数
    std::ostringstream oss;
    oss << "log_" << pid << "_" << counter++;
    return oss.str();
}

// --- 通常ログ出力 ---
// level: "INFO" / "WARNING" / "ERROR"
void logMessage(LogLevel level, const std::string &msg) {
    std::ostringstream oss;
    oss << "[" << getTimeStamp() << "] "
        << level << ": " << msg;

    std::cout << oss.str() << std::endl;
}

// --- エラーログ出力 ---
void logError(const std::string &func, const std::string &msg) {
    std::ostringstream oss;
    oss << "[" << getTimeStamp() << "] "
        << "ERROR (" << func << "): " << msg;

    std::cerr << oss.str() << std::endl;
}
