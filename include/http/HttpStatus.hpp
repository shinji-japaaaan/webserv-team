#pragma once
#include <map>
#include <string>

namespace http {

class Status {
public:
    // ステータスコードに対応する Reason-Phrase を返す
    static const std::string& reason(int code) {
        // C++98 なので static ローカル map を初期化時に構築
        static std::map<int, std::string> r;
        if (r.empty()) {
            // --- 2xx Success ---
            r[200] = "OK";
            r[201] = "Created";
            r[202] = "Accepted";
            r[204] = "No Content";

            // --- 3xx Redirection ---
            r[301] = "Moved Permanently";
            r[302] = "Found";
            r[303] = "See Other";
            r[307] = "Temporary Redirect";
            r[308] = "Permanent Redirect";

            // --- 4xx Client Error ---
            r[400] = "Bad Request";
            r[401] = "Unauthorized";
            r[403] = "Forbidden";
            r[404] = "Not Found";
            r[405] = "Method Not Allowed";
            r[408] = "Request Timeout";
            r[411] = "Length Required";
            r[413] = "Payload Too Large";
            r[414] = "URI Too Long";
            r[415] = "Unsupported Media Type";
            r[422] = "Unprocessable Entity";
            r[426] = "Upgrade Required";
            r[429] = "Too Many Requests";

            // --- 5xx Server Error ---
            r[500] = "Internal Server Error";
            r[501] = "Not Implemented";
            r[502] = "Bad Gateway";
            r[503] = "Service Unavailable";
            r[504] = "Gateway Timeout";
        }

        std::map<int, std::string>::const_iterator it = r.find(code);
        static const std::string kUnknown("Unknown");
        return (it == r.end()) ? kUnknown : it->second;
    }

    // 登録済みコードかどうか
    static bool known(int code) {
        return reason(code) != "Unknown";
    }
};

} // namespace http