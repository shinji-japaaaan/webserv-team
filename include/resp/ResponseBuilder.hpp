#ifndef RESPONSE_BUILDER_HPP
#define RESPONSE_BUILDER_HPP

#include <string>
#include <map>
#include "RequestParser.hpp"
#include "ConfigParser.hpp"

class ResponseBuilder {
public:
    // メインディスパッチャ
    std::string generateResponse(
        const Request &req,
        const ServerConfig &cfg,
        const ServerConfig::Location *loc,
        const std::string &locPath);

    // GET / HEAD
    std::string handleGetLike(
        const Request &req,
        const ServerConfig &cfg,
        const ServerConfig::Location *loc,
        const std::string &locPath);

    // DELETE
    std::string handleDelete(
        const Request &req,
        const ServerConfig &cfg,
        const ServerConfig::Location *loc,
        const std::string &locPath);

    // 405
    std::string buildMethodNotAllowed(const std::string &allow,
                                      const ServerConfig &cfg);

    // 汎用レスポンス
    std::string buildSimpleResponse(
        int statusCode,
        const std::string &reason,
        bool close,
        const std::map<std::string, std::string> &extraHeaders
    ) const;

    std::string buildSimpleResponse(
        int statusCode,
        const std::string &reason,
        bool close
    ) const;

    // 追加分: エラーページ
    std::string buildErrorResponse(
        const ServerConfig &cfg,
        const ServerConfig::Location *loc,
        int statusCode,
        bool close = true) const;

    std::string buildErrorResponseFromFile(
        const std::string &filePath,
        int statusCode = 500,
        bool close = true) const;

  private:
    bool isTraversal(const std::string &uri) const;
    std::string mergeRoots(const ServerConfig &cfg,
                           const ServerConfig::Location *loc) const;
    std::string stripLocationPrefix(const std::string &uri,
                                    const std::string &locPath) const;
    std::string resolvePathForGet(const std::string &docRoot,
                                  const std::string &relativeUri,
                                  bool &isDirOut) const;
    std::string resolvePathForDelete(const std::string &docRoot,
                                     const std::string &relativeUri) const;
    std::string buildOkResponseFromFile(
        const std::string &absPath,
        bool headOnly,
        bool close);
    std::string guessContentType(const std::string &path) const;
    std::string httpDate_() const;

  // ★ここを追加：内部用の3引数版は private ヘルパとして別名にする
  std::string handleGetLikeCore(const Request&, const ServerConfig&,
                                const ServerConfig::Location*);
  std::string handleDeleteCore(const Request&, const ServerConfig&,
                               const ServerConfig::Location*);
};

#endif // RESPONSE_BUILDER_HPP
