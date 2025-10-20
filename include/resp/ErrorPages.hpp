#pragma once
#include <string>
#include "ServerManager.hpp" // ServerConfig の参照に必要

namespace resp {

class ErrorPages {
public:
    // エラーHTML本文を取得（config, root, assets/errors, デフォルトの順で探索）
    static std::string getErrorBody(const ServerConfig& srv, int status);

    // 統一 Content-Type
    static const std::string& contentType();

    // デフォルトHTML生成
    static std::string defaultHtml(int status);
};

} // namespace resp