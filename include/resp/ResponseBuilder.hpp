#ifndef RESPONSE_BUILDER_HPP
#define RESPONSE_BUILDER_HPP

#include <string>
#include <map>

#include "RequestParser.hpp"       // struct Request { method, uri, headers, body ... }
#include "ConfigParser.hpp"  // struct ServerConfig { root, errorPages, ... }

class ResponseBuilder {
public:
    // ① ルータ: メソッドに応じて適切なハンドラを呼ぶ
    std::string generateResponse(const Request &req,
                                 const ServerConfig &cfg);

    // ② GET / HEAD (静的ファイル返却)
    std::string handleGetLike(const Request &req,
                              const ServerConfig &cfg);

    // ③ DELETE
    std::string handleDelete(const Request &req,
                             const ServerConfig &cfg);

    // ④ 405 Method Not Allowed
    std::string buildMethodNotAllowed(const std::string &allow,
                                      const ServerConfig &cfg);

    // ⑤ 汎用でヘッダだけ返すレスポンス（204 No Contentなど）
    std::string buildSimpleResponse(
        int statusCode,
        const std::string &reason,
        bool close,
        const std::map<std::string, std::string> &extraHeaders
    );

    // extraHeadersなし版
    std::string buildSimpleResponse(
        int statusCode,
        const std::string &reason,
        bool close
    );

    // すでに実装済みのはずのやつら（このヘッダで宣言してあるなら残してOK）
    std::string buildErrorResponse(int statusCode,
                                   bool close,
                                   const std::string &docRoot);

    std::string buildFromCgi(const std::string &cgiRaw,
                             bool close);

    std::string buildErrorResponseFromFile(const std::string &filePath,
                                           int statusCode = 500,
                                           bool close = true) const;

private:
    // --- ヘルパ群 ---

    // ディレクトリトラバーサル防止 ("..", "%2e%2e")
    bool isTraversal(const std::string &uri) const;

    // GET/HEAD用: docRoot + uri を安全に解決
    //  - realpath()してdocRoot配下チェック
    //  - "/" や ディレクトリアクセスなら index.html にフォールバックする等
    // 返り値: 決定した絶対パス。失敗時は空文字
    std::string resolvePathForGet(const std::string &docRoot,
                                  const std::string &uri,
                                  bool &isDirOut) const;

    // DELETE用: index.html補完なし、ディレクトリは想定外
    std::string resolvePathForDelete(const std::string &docRoot,
                                     const std::string &uri) const;

    // 実ファイルの中身を読んで200を返す（HEADならボディなし）
    std::string buildOkResponseFromFile(
        const std::string &absPath,
        bool headOnly,
        bool close
    );

    // 拡張子からContent-Typeを推測
    std::string guessContentType(const std::string &path) const;

    // 日付ヘッダをつくる(既存にあるやつをそのまま使う想定)
    std::string httpDate_() const;
};

#endif // RESPONSE_BUILDER_HPP