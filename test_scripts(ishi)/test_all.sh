#!/bin/bash
# 総合テストスクリプト（curl -i -v 使用）
# 事前条件: ./webserv を起動済み, config.conf を指定している

SERVER="http://localhost:8080"
UPLOAD_FILE="../www/test_upload.txt"
CGI_SCRIPT="../www/cgi-bin/test.php"

echo "=== GET リクエスト ==="
curl -i  $SERVER/index.html
echo -e "\n--------------------\n"

echo "=== POST ファイルアップロード ==="
curl -i  -X POST -F "file=@$UPLOAD_FILE" $SERVER/upload
echo -e "\n--------------------\n"

echo "=== DELETE リクエスト ==="
curl -i  -X DELETE $SERVER/testfile.txt
echo -e "\n--------------------\n"

echo "=== CGI リクエスト (GET) ==="
curl -i  $SERVER/cgi-bin/test.php
echo -e "\n--------------------\n"

echo "=== CGI リクエスト (POST) ==="
curl -i  -X POST -d "param1=hello&param2=world" $SERVER/cgi-bin/test.php
echo -e "\n--------------------\n"
