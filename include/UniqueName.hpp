#ifndef UNIQUENAME_HPP
#define UNIQUENAME_HPP

#include <string>
#include <sstream>
#include <ctime>
#include <cstdlib>
#include <iomanip>

inline std::string makeUniqueName(const std::string &prefix, const std::string &ext)
{
    static unsigned long counter = 0;  // 同一プロセス内での重複回避
    std::ostringstream oss;

    // 現在時刻の取得
    std::time_t now = std::time(NULL);
    struct std::tm *tm_now = std::localtime(&now);

    // 日時を YYYYMMDD_HHMMSS の形式で出力
    oss << prefix << "_"
        << (tm_now->tm_year + 1900)
        << std::setw(2) << std::setfill('0') << (tm_now->tm_mon + 1)
        << std::setw(2) << std::setfill('0') << tm_now->tm_mday << "_"
        << std::setw(2) << std::setfill('0') << tm_now->tm_hour
        << std::setw(2) << std::setfill('0') << tm_now->tm_min
        << std::setw(2) << std::setfill('0') << tm_now->tm_sec
        << "_" << counter++;

    // 拡張子を追加
    if (!ext.empty())
        oss << "." << ext;

    return oss.str();
}

#endif