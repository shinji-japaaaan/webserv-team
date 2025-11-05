#!/bin/bash
# ============================
# Setup test.conf for Webserv
# ============================

CONF_DIR=./conf
CONF_FILE=$CONF_DIR/test.conf
WWW_DIR=./www

mkdir -p $CONF_DIR
mkdir -p $WWW_DIR/cgi-bin
mkdir -p $WWW_DIR/html

cat > $CONF_FILE <<'EOF'
server {
    listen 8080;
    host 127.0.0.1;
    root ./www;
    error_page 404 ./www/html/404.html;

    location / {
        method GET POST;
        root ./www;
    }

    location /cgi-bin/ {
        method GET POST;
        root ./www/cgi-bin;
        cgi_path /usr/bin/python3;
    }
}
EOF

# 404ページを軽く用意
echo "<h1>404 Not Found</h1>" > $WWW_DIR/html/404.html

echo "✅ Created $CONF_FILE"
