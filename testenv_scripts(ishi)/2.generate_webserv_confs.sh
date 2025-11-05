#!/bin/bash
# ==============================================
# Webserv 評価用 conf セット（tenpapa版テンプレート自動生成）
# ==============================================

set -e

CONFIG_DIR="./conf"

# ディレクトリ作成
mkdir -p "$CONFIG_DIR"

echo "✅ Generating Webserv config templates into $CONFIG_DIR ..."

# 01_ports.conf
cat > "$CONFIG_DIR/01_ports.conf" <<'EOF'
server {
listen 8080 ;
host 127.0.0.1 ;
root ./docs/ ;
error_page 404 ./assets/errors/404.html ;

location / {
method GET ;
index index.html ;
autoindex off ;
max_body_size 100000 ;
}
}

server {
listen 8081 ;
host 127.0.0.1 ;
root ./www/ ;
error_page 404 ./assets/errors/404.html ;

location / {
method GET ;
index index.html ;
autoindex off ;
}
}
EOF

# 02_hostnames.conf
cat > "$CONFIG_DIR/02_hostnames.conf" <<'EOF'
server {
	listen 8080 ;
	host 127.0.0.1 ;
	root ./docs/ ;
	error_page 404 ./assets/errors/404.html ;

	location / {
		method GET ;
		index index.html ;
		autoindex off ;
	}
}

server {
	listen 8081 ;
	host 127.0.0.1 ;
	root ./www/ ;
	error_page 404 ./assets/errors/404.html ;

	location / {
		method GET ;
		index index.html ;
		autoindex off ;
	}
}
EOF

# 03_error_page.conf
cat > "$CONFIG_DIR/03_error_page.conf" <<'EOF'
server {
	listen 8082 ;
	host 127.0.0.1 ;
	root ./www/ ;

	error_page 404 ./assets/errors/404.html ;
	error_page 400 ./assets/errors/404_default.html ;
	error_page 403 ./assets/errors/404_default.html ;
	error_page 405 ./assets/errors/404_default.html ;
	error_page 413 ./assets/errors/404_default.html ;
	error_page 500 ./assets/errors/404_default.html ;

	location / {
		method GET ;
		index index.html ;
		autoindex off ;
	}
}
EOF

# 04_body_limit.conf
cat > "$CONFIG_DIR/04_body_limit.conf" <<'EOF'
server {
	listen 8083 ;
	host 127.0.0.1 ;
	root ./docs/ ;
	error_page 404 ./assets/errors/404.html ;

	location / {
		method GET ;
		index index.html ;
		autoindex off ;
	}

	location /upload/ {
		method POST ;
		root /upload/ ;
		upload_path ./upload/ ;
		max_body_size 1048576 ;
		autoindex off ;
	}
}
EOF

# 05_routes.conf
cat > "$CONFIG_DIR/05_routes.conf" <<'EOF'
server {
	listen 8084 ;
	host 127.0.0.1 ;
	root ./www/ ;
	error_page 404 ./assets/errors/404.html ;

	location / {
		method GET ;
		index index.html ;
		autoindex off ;
	}

	location /docs {
		method GET ;
		root ./docs ;
		index index.html ;
		autoindex off ;
	}

	location /redirect/ {
		return 301 https://example.org ;
	}
}
EOF

# 06_index.conf
cat > "$CONFIG_DIR/06_index.conf" <<'EOF'
server {
	listen 8085 ;
	host 127.0.0.1 ;
	root ./www/ ;
	error_page 404 ./assets/errors/404.html ;

	location / {
		method GET ;
		index index.html ;
		autoindex off ;
	}

	location /list {
		method GET ;
		root ./www/list ;
		autoindex on ;
	}
}
EOF

# 07_methods.conf
cat > "$CONFIG_DIR/07_methods.conf" <<'EOF'
server {
	listen 8086 ;
	host 127.0.0.1 ;
	root ./www/ ;
	error_page 404 ./assets/errors/404.html ;

	location /readonly/ {
		method GET ;
		root ./www/ ;
		index index.html ;
		autoindex off ;
	}

	location /delete/ {
		method DELETE ;
		root ./www/ ;
		autoindex off ;
	}

	location /upload/ {
		method POST ;
		root /upload/ ;
		upload_path ./upload/ ;
		max_body_size 1048576 ;
		autoindex off ;
	}
}
EOF

# 08_multiple_ports.conf
cat > "$CONFIG_DIR/08_multiple_ports.conf" <<'EOF'
server {
	listen 8087 ;
	host 127.0.0.1 ;
	root ./www/ ;
	error_page 404 ./assets/errors/404.html ;

	location / {
		method GET ;
		index index.html ;
		autoindex off ;
	}
}

server {
	listen 8088 ;
	host 127.0.0.1 ;
	root ./www/ ;
	error_page 404 ./assets/errors/404.html ;

	location / {
		method GET ;
		index index.html ;
		autoindex off ;
	}
}
EOF

# 09_same_ports.conf
cat > "$CONFIG_DIR/09_same_ports.conf" <<'EOF'
server {
	listen 8089 ;
	host 127.0.0.1 ;
	root ./www/a_local/ ;
	error_page 404 ./assets/errors/404.html ;

	location / {
		method GET ;
		index index.html ;
		autoindex off ;
	}
}

server {
	listen 8089 ;
	host 127.0.0.1 ;
	root ./www/b_local/ ;
	error_page 404 ./assets/errors/404.html ;

	location / {
		method GET ;
		index index.html ;
		autoindex off ;
	}
}

# Virtual Host 未対応
EOF

# 10_multiple_servers.conf
cat > "$CONFIG_DIR/10_multiple_servers.conf" <<'EOF'
server {
	listen 8090 ;
	host 127.0.0.1 ;
	root ./docs/ ;
	error_page 404 ./assets/errors/404.html ;

	location / {
		method GET ;
		index index.html ;
		autoindex off ;
	}
}

server {
	listen 8091 ;
	host 127.0.0.1 ;
	root ./www/ ;
	error_page 404 ./assets/errors/404.html ;

	location / {
		method GET ;
		index index.html ;
		autoindex off ;
	}

	location /upload/ {
		method POST ;
		root /upload/ ;
		upload_path ./upload/ ;
		max_body_size 262144 ;
		autoindex off ;
	}
}

server {
	listen 8092 ;
	host 127.0.0.1 ;
	root ./docs/ ;
	error_page 404 ./assets/errors/404.html ;

	location /cgi-bin/ {
		method GET POST ;
		root ./docs/ ;
		index test.php ;
		cgi_path /usr/bin/php-cgi ;
		autoindex off ;
		max_body_size 1048576 ;
	}
}
EOF

echo "🎉 All config templates created successfully!"

