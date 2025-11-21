#!/bin/bash
# =================================
# CGI テスト用アップロードスクリプト作成
# =================================

CGI_DIR="./www/cgi-bin"
mkdir -p "$CGI_DIR"

cat > "$CGI_DIR/post_upload.py" << 'EOF'
#!/usr/bin/env python
import os, sys

# POSTされたバイナリデータの長さを取得
clen = os.environ.get("CONTENT_LENGTH", "0")
body = sys.stdin.read(int(clen)) if clen.isdigit() else b""

# サーバに保存
save_path = "uploaded_file.bin"
with open(save_path, "wb") as f:
    f.write(body)

# 確認用レスポンス
print("Content-Type: text/plain")
print()
print("Saved", len(body), "bytes to", save_path)
EOF

chmod +x "$CGI_DIR/post_upload.py"
echo "✅ CGI アップロードスクリプト作成完了: $CGI_DIR/post_upload.py"
