#pragma once
#include <string>
#include "RequestParser.hpp"

// src/resp へと飛ばす関数
class ResponseBuilder {
public:
    // docRoot と indexName は当面 Server 側から渡す（BのRouteが整ったら差し替え）
    std::string build(const Request& req,
                      const std::string& docRoot,
                      const std::string& indexName);

	//ラッパーをする
	std::string generateResponse(const Request& req);
private:
    std::string buildStatic200_(const std::string& absPath, bool close);
    std::string httpDate_() const;

    // 新シグネチャ：コード + close + docRoot + （任意）Allow ヘッダ
    std::string buildError_(int code,
                            bool close,
                            const std::string& docRoot,
                            const std::string& allowHdr = "");

    // 互換オーバーロード（旧シグネチャ呼び出しを吸収）
    inline std::string buildError_(int code, const std::string& /*msg*/, bool close) {
        return buildError_(code, close, std::string(), std::string());
    }
};