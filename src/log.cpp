#include "log.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include "UniqueName.hpp"

// ★ タイムスタンプを取得
std::string getTimeStamp() {
    return makeUniqueName("log", "");
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
