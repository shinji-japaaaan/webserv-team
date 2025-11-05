<?php
/*

// 簡単なCGIスクリプト（POST/GET対応）
header("Content-Type: text/plain");

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $input = file_get_contents('php://input');
    echo "Received POST data:\n";
    echo $input;
} else {
    echo "Hello from CGI script!";
}
*/

// タイムアウトテスト。故意に5秒待つ
sleep(10);
echo "Hello World";


?>

