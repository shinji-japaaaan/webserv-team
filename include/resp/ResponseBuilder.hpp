#pragma once
#include <string>
#include "RequestParser.hpp"

//src/respへと飛ばす関数
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
    std::string buildError_(int code, const std::string& msg, bool close);
    std::string httpDate_() const;
};