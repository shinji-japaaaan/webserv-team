#!/usr/bin/env python3
import os, sys

# Content-Length を取得
clen = os.environ.get("CONTENT_LENGTH", "0")

# バイナリで stdin を読む
body = b""
if clen.isdigit():
    body = sys.stdin.buffer.read(int(clen))  # ← buffer で bytes で読む

# 保存先ファイル
with open("uploaded_file.bin", "wb") as f:
    f.write(body)

# 簡単なレスポンス
print("Content-Type: text/plain")
print()
print("Received bytes:", len(body))
