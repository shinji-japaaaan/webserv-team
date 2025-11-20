#!/usr/bin/env python
import os, sys

# リクエスト情報を取得
method = os.environ.get("REQUEST_METHOD", "")
ctype  = os.environ.get("CONTENT_TYPE", "")
clen   = os.environ.get("CONTENT_LENGTH", "0")

# BODY を読む（CONTENT_LENGTH バイト）
body = sys.stdin.read(int(clen)) if clen.isdigit() else ""

# HTTP レスポンス出力
print("Content-Type: text/plain")
print()
print("REQUEST_METHOD:", method)
print("CONTENT_LENGTH:", clen)
print("CONTENT_TYPE:", ctype)
print("Body:", body)
