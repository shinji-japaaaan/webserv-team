#ifndef LOG_HPP
#define LOG_HPP

#include <string>

enum LogLevel { INFO, WARNING, ERROR };

// タイムスタンプ付きメッセージ出力
void logMessage(LogLevel level, const std::string &msg);

// 関数名＋エラーメッセージを出力
void logError(const std::string &func, const std::string &msg);

#endif
