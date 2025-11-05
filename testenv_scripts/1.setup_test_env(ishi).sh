#!/bin/bash
# setup_test_env.sh
# 総合テスト用の環境を準備するスクリプト
# (テスト自体は含まない)

# プロジェクトルートを基準に移動
PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$PROJECT_ROOT"

# www ディレクトリ作成
mkdir -p www
mkdir -p www/upload
mkdir -p www/cgi-bin

# index.html
cat > www/index.html <<EOL
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Webserv Test</title>
</head>
<body>
    <h1>Hello from Webserv!</h1>
</body>
</html>
EOL

# GET / DELETE 用テストファイル
echo "This is a test file for GET and DELETE requests." > www/testfile.txt

# アップロード用ファイル
echo "This is a test upload file." > www/test_upload.txt

# CGIスクリプト (PHP)
cat > www/cgi-bin/test.php <<'EOL'
<?php
// 簡単なCGIスクリプト（POST/GET対応）
header("Content-Type: text/plain");

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $input = file_get_contents('php://input');
    echo "Received POST data:\n";
    echo $input;
} else {
    echo "Hello from CGI script!";
}
?>
EOL

echo "テスト用ディレクトリ・ファイルを作成しました。"
echo "作成されたファイル:"
echo " - www/index.html"
echo " - www/testfile.txt"
echo " - www/test_upload.txt"
echo " - www/cgi-bin/test.php"
echo " - www/upload/ (空ディレクトリ)"

