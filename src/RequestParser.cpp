#include "../include/RequestParser.hpp"

bool RequestParser::isRequestComplete(const std::string &buffer) {
    if (buffer.empty())
        return false;
    
    size_t headerEnd = buffer.find("\r\n\r\n");
    
     // --- 不正・非HTTPデータ対策 ---
    if (headerEnd == std::string::npos) {
        if (isClearlyInvalidRequest(buffer))
            return true;
        return false;
    }

    std::string headerPart = buffer.substr(0, headerEnd);
    std::map<std::string, std::string> headers = parseHeaders(headerPart);

    bool isChunked = false;
    size_t contentLength = 0;

    if (headers.find("transfer-encoding") != headers.end() &&
        headers["transfer-encoding"].find("chunked") != std::string::npos)
        isChunked = true;

    if (headers.find("content-length") != headers.end())
        contentLength = std::strtoul(headers["content-length"].c_str(), NULL, 10);

    std::string bodyPart = buffer.substr(headerEnd + 4);

    if (isChunked)
        return bodyPart.find("0\r\n\r\n") != std::string::npos;
    else
        return bodyPart.size() >= contentLength;
}

// --- 非HTTP・不正データを早期判定する補助関数 ---
bool RequestParser::isClearlyInvalidRequest(const std::string &buffer) {
    // 改行があれば完了扱い（例: "BAD_REQUEST\n"）
    if (buffer.find("\n") != std::string::npos)
        return true;

    // 明らかにHTTPでない1行メッセージ（例: "HELLO", "BAD_REQUEST"）
    if (buffer.size() < 64 && buffer.find(' ') == std::string::npos)
        return true;

    // 異常に長い（DoS防止）
    if (buffer.size() > 8192)
        return true;

    return false;
}

RequestParser::RequestParser()
    : parsedLength(0){}

std::string unchunkBody(const std::string &chunkedBody) {
    std::string unchunked;
    std::istringstream stream(chunkedBody);

     bool firstLine = true;

    while (true) {
        std::string line;
        if (!std::getline(stream, line)) break;

        // CR (\r) を削除
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);

        if (line.empty()) continue;

        // 先頭行が余計なデータの場合は無視
        if (firstLine) {
            firstLine = false;
            continue;
        }

        // chunk サイズ
        size_t chunkSize = std::strtoul(line.c_str(), 0, 16); // nullptr -> 0

        if (chunkSize == 0) break; // 最終 chunk

        // データ部分を読み込む
        std::string data;
        data.resize(chunkSize);
        stream.read(&data[0], chunkSize);
        unchunked += data;

        // データ後の CRLF を読み飛ばす
        stream.get(); // \r
        stream.get(); // \n
    }

    return unchunked;
}

Request RequestParser::parse(const std::string &buffer) {
    Request req;
    parsedLength = 0;

    size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return req;

    std::string headerPart = buffer.substr(0, headerEnd);
    std::string bodyPart   = buffer.substr(headerEnd + 4);

    // ✅ 同じ関数を利用してヘッダ解析（小文字化も共通化）
    req.headers = parseHeaders(headerPart);

    std::istringstream lineStream(headerPart);
    std::string requestLine;
    if (!std::getline(lineStream, requestLine) || requestLine.empty()) {
		req.method.clear(); // ← 明示的に不正を示す
		return req;
	}

    std::istringstream requestLineStream(requestLine);
    requestLineStream >> req.method >> req.uri >> req.version;
    // --- ✅ 妥当性チェック ---
	if (req.method.empty() || req.uri.empty() || req.version.empty()) {
		req.method.clear(); // 不正リクエストの印
		return req;
	}
    // 例: "HTTP/" で始まらないなら不正
	if (req.version.find("HTTP/") != 0) {
		req.method.clear();
		return req;
	}

    bool isChunked = false;
    if (req.headers.find("transfer-encoding") != req.headers.end() &&
        req.headers["transfer-encoding"].find("chunked") != std::string::npos)
        isChunked = true;

    if (isChunked) {
        size_t chunkEnd = bodyPart.find("0\r\n\r\n");
		if (chunkEnd == std::string::npos) {
			req.method.clear(); // チャンク終端がない → 不正
			return req;
		}
        req.body = unchunkBody(req.body);
        parsedLength = headerEnd + 4 + bodyPart.find("0\r\n\r\n") + 5;
    } else {
        std::map<std::string, std::string>::iterator it = req.headers.find("content-length");
        if (it != req.headers.end()) {
            size_t len = std::strtoul(it->second.c_str(), NULL, 10);
            if (bodyPart.size() < len) {
				req.method.clear(); // 不正（ボディが足りない）
				return req;
			}
            req.body = bodyPart.substr(0, len);
            parsedLength = headerEnd + 4 + len;
        } else {
            parsedLength = headerEnd + 4;
        }
    }

    return req;
}

std::map<std::string, std::string> RequestParser::parseHeaders(const std::string &headerPart) {
    std::map<std::string, std::string> headers;
    std::istringstream stream(headerPart);
    std::string line;

    while (std::getline(stream, line)) {
        size_t pos = line.find(":");
        if (pos == std::string::npos)
            continue;

        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);

        // ここで小文字化
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        // トリム
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t\r") + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t\r") + 1);

        headers[key] = val;
    }

    return headers;
}


// void printRequest(const Request &req) {
//   std::cout << "=== Request ===" << std::endl;
//   std::cout << "Method: " << req.method << std::endl;
//   std::cout << "URI: " << req.uri << std::endl;

//   std::cout << "Headers:" << std::endl;
//   std::map<std::string, std::string>::const_iterator it;
//   for (it = req.headers.begin(); it != req.headers.end(); ++it) {
//     std::cout << "  " << it->first << ": " << it->second << std::endl;
//   }

//   std::cout << "Body: " << req.body << std::endl;
//   std::cout << "================" << std::endl;
// }
