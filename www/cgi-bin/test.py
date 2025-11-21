#!/usr/bin/env python3
import os
import sys

# リクエストメソッドを取得
method = os.environ.get("REQUEST_METHOD", "")
# Content-Type と Content-Length
content_type = os.environ.get("CONTENT_TYPE", "")
content_length = os.environ.get("CONTENT_LENGTH", "0")

# POST の場合は標準入力から body を読む
body = ""
if method == "POST" and content_length.isdigit():
    body = sys.stdin.read(int(content_length))

# レスポンスを返す
print("Content-Type: text/plain")  # 必須ヘッダ
print()  # ヘッダと本文の間に空行
print("REQUEST_METHOD:", method)
print("CONTENT_TYPE:", content_type)
print("CONTENT_LENGTH:", content_length)
print("Body:", body)
