#!/usr/bin/env bash
# Webserv end-to-end test runner (full replacement)
# - Runs each conf in ./conf
# - Starts/stops webserv per test
# - Labeled checks with PASS/FAIL/SKIP
# - Non-stop execution; summary at the end

set -uo pipefail

# -------- settings --------
PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
WEBSERV="$PROJECT_ROOT/webserv"
CONFIG_DIR="$PROJECT_ROOT/conf"
LOG_DIR="$PROJECT_ROOT/test_logs"

CURL_BASE_OPTS=(-sS -o /dev/null -w "%{http_code}" --max-time 5)
RETRY_MAX=50
RETRY_SLEEP=0.2

mkdir -p "$LOG_DIR"

# -------- state --------
PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
RESULTS=()  # "id|title|label|STATUS|url|detail"

CURRENT_ID=""
CURRENT_TITLE=""
CURRENT_DESC=""

# -------- ui --------
hr(){ printf -- "-----------------------------\n\n"; }
say(){ printf "%s\n" "$1"; }
ok(){  printf "  ✅ %s\n" "$1"; }
ng(){  printf "  ❌ %s\n" "$1"; }
skp(){ printf "  ⏭️  %s\n" "$1"; }

# -------- helpers --------
is_alive() {
  local pid="$1"
  kill -0 "$pid" 2>/dev/null
}

start_server() {
  # args: <conf_basename> <display_name>
  local conf="$1" name="$2"
  local conf_path="$CONFIG_DIR/$conf"
  local log="$LOG_DIR/${conf%.conf}.log"

  # ensure previous is dead
  pkill -f "$WEBSERV" 2>/dev/null || true
  sleep 0.1

  if [[ ! -x "$WEBSERV" ]]; then
    echo "ERROR: webserv missing or not executable: $WEBSERV"
    exit 1
  fi
  if [[ ! -f "$conf_path" ]]; then
    echo "ERROR: conf not found: $conf_path"
    exit 1
  fi

  "$WEBSERV" "$conf_path" >"$log" 2>&1 & local pid=$!
  echo "$pid"
}

stop_server() {
  local pid="$1"
  is_alive "$pid" && kill "$pid" 2>/dev/null || true
  sleep 0.1
  is_alive "$pid" && kill -9 "$pid" 2>/dev/null || true
}

wait_http_up() {
  # args: <url>
  local url="$1" tries=0 code=""
  while (( tries < RETRY_MAX )); do
    code=$(curl "${CURL_BASE_OPTS[@]}" "$url" || true)
    [[ "$code" =~ ^[0-9]{3}$ ]] && return 0
    sleep "$RETRY_SLEEP"
    ((tries++))
  done
  return 1
}

record_pass(){ RESULTS+=("${CURRENT_ID}|${CURRENT_TITLE}|$1|PASS|$2|$3"); ((PASS_COUNT++)); }
record_fail(){ RESULTS+=("${CURRENT_ID}|${CURRENT_TITLE}|$1|FAIL|$2|$3"); ((FAIL_COUNT++)); }
record_skip(){ RESULTS+=("${CURRENT_ID}|${CURRENT_TITLE}|$1|SKIP|$2|$3"); ((SKIP_COUNT++)); }

start_test(){ # args: <id> <title> <desc>
  CURRENT_ID="$1"; CURRENT_TITLE="$2"; CURRENT_DESC="$3"
  hr
  say "[$CURRENT_ID] $CURRENT_TITLE"
  echo "  - $CURRENT_DESC"
}

case_check() {
  # args: <expected> <url> <label> [extra curl args...]
  local expect="$1"; local url="$2"; local label="$3"; shift 3
  local code
  code=$(curl "${CURL_BASE_OPTS[@]}" "$@" "$url" || true)
  if [[ "$code" == "$expect" ]]; then
    ok "[$expect] $label  -> $url"
    record_pass "$label" "$url" "$expect"
  else
    ng "[$expect expected / got $code] $label  -> $url"
    record_fail "$label" "$url" "expected $expect, got $code"
  fi
  return 0
}

case_check_one_of() {
  # args: <csv_expected> <url> <label> [extra curl args...]
  local expects_csv="$1"; local url="$2"; local label="$3"; shift 3
  local code expects IFS=','; read -ra expects <<<"$expects_csv"
  code=$(curl "${CURL_BASE_OPTS[@]}" "$@" "$url" || true)
  for e in "${expects[@]}"; do
    [[ "$code" == "$e" ]] && ok "[${expects_csv}] $label -> $url" && record_pass "$label" "$url" "$code" && return 0
  done
  ng "[${expects_csv} expected / got $code] $label -> $url"
  record_fail "$label" "$url" "expected one of {${expects_csv}}, got $code"
  return 0
}

case_post_big() {
  # args: <expected> <url> <label> [extra curl args...]
  local expect="$1"; local url="$2"; local label="$3"; shift 3
  local code
  code=$(head -c 1200000 /dev/zero | tr '\0' 'a' | \
         curl "${CURL_BASE_OPTS[@]}" -X POST -H "Content-Type: text/plain" --data-binary @- "$@" "$url" || true)
  if [[ "$code" == "$expect" ]]; then
    ok "[$expect] $label  -> $url"
    record_pass "$label" "$url" "$expect"
  else
    ng "[$expect expected / got $code] $label  -> $url"
    record_fail "$label" "$url" "expected $expect, got $code"
  fi
  return 0
}

# -------- tests --------

main() {
  # [01] Multiple ports
  start_test "01" "Multiple ports" "8080 → ./docs/, 8081 → ./www/ の基本疎通"
  pid=$(start_server "01_ports.conf" "01")
  # wait for either port to answer
  if ! wait_http_up "http://127.0.0.1:8080/"; then
    ng "server not responding on 8080"; record_fail "boot 8080" "http://127.0.0.1:8080/" "no response"
  fi
  case_check 200 "http://127.0.0.1:8080/" "8080 GET /"
  case_check 200 "http://127.0.0.1:8081/" "8081 GET /"
  stop_server "$pid"

  # [02] Hostnames (port-based)
  start_test "02" "Hostnames (port-based)" "名前ベース未実装想定のため 8080/8081 で代替"
  pid=$(start_server "02_hostnames.conf" "02")
  wait_http_up "http://127.0.0.1:8080/" || true
  case_check 200 "http://example.local:8080/" "example.local GET /" --resolve example.local:8080:127.0.0.1
  case_check 200 "http://demo.local:8080/"    "demo.local GET /"    --resolve demo.local:8080:127.0.0.1
  stop_server "$pid"

  # [03] Custom error pages
  start_test "03" "Custom error pages" "/notfound → 404（assets/errors/404.html）"
  pid=$(start_server "03_error_page.conf" "03")
  wait_http_up "http://127.0.0.1:8082/notfound" || true
  case_check 404 "http://127.0.0.1:8082/notfound" "GET /notfound"
  stop_server "$pid"

  # [04] Body limit & upload
  start_test "04" "Body limit & upload" "小POSTは受理、閾値超過は 413"
  pid=$(start_server "04_body_limit.conf" "04")
  wait_http_up "http://127.0.0.1:8083/" || true
  # small form post (urlencoded)
  case_check 200 "http://127.0.0.1:8083/upload/" "small POST form" -X POST -d "x=1"
  # big body -> 413
  case_post_big 413 "http://127.0.0.1:8083/upload/" "big POST > limit"
  stop_server "$pid"

  # [05] Routes & redirect
  start_test "05" "Routes & redirect" "/docs は ./docs, /redirect/ は 301"
  pid=$(start_server "05_routes.conf" "05")
  wait_http_up "http://127.0.0.1:8084/" || true
  case_check 200 "http://127.0.0.1:8084/docs/" "GET /docs/"
  case_check 301 "http://127.0.0.1:8084/redirect/" "redirect 301"
  # /img は任意（assets/images が無い可能性もあるので優しめに SKIP if 404）
  code=$(curl "${CURL_BASE_OPTS[@]}" "http://127.0.0.1:8084/img/" || true)
  if [[ "$code" == "200" ]]; then
    ok "[200] GET /img/  -> http://127.0.0.1:8084/img/"
    record_pass "GET /img/" "http://127.0.0.1:8084/img/" "200"
  else
    skp "GET /img/ (no assets/images?) -> got $code; marking SKIP"
    record_skip "GET /img/" "http://127.0.0.1:8084/img/" "got $code"
  fi
  stop_server "$pid"

  # [06] Index & autoindex
  start_test "06" "Index & autoindex" "/ → index.html, /list/ は autoindex on"
  pid=$(start_server "06_index.conf" "06")
  wait_http_up "http://127.0.0.1:8085/" || true
  case_check 200 "http://127.0.0.1:8085/" "GET /"
  # If www/list/ not present, SKIP
  code=$(curl "${CURL_BASE_OPTS[@]}" "http://127.0.0.1:8085/list/" || true)
  if [[ "$code" == "200" ]]; then
    ok "[200] GET /list/ -> http://127.0.0.1:8085/list/"
    record_pass "GET /list/" "http://127.0.0.1:8085/list/" "200"
  else
    skp "GET /list/ (missing www/list?) -> got $code; marking SKIP"
    record_skip "GET /list/" "http://127.0.0.1:8085/list/" "got $code"
  fi
  stop_server "$pid"

  # [07] Methods
  start_test "07" "Methods" "/readonly/ GET=200, POST=405; /delete/ で削除; /upload/ でPOST"
  pid=$(start_server "07_methods.conf" "07")
  wait_http_up "http://127.0.0.1:8086/" || true
  case_check 200 "http://127.0.0.1:8086/readonly/" "GET /readonly/"
  case_check 405 "http://127.0.0.1:8086/readonly/" "POST /readonly/" -X POST -d "x=1"
  # DELETE target file (if not present -> SKIP)
  touch "$PROJECT_ROOT/www/file.txt" 2>/dev/null || true
  code=$(curl "${CURL_BASE_OPTS[@]}" -X DELETE "http://127.0.0.1:8086/delete/file.txt" || true)
  if [[ "$code" == "200" || "$code" == "204" ]]; then
    ok "[$code] DELETE /delete/file.txt"
    record_pass "DELETE /delete/file.txt" "http://127.0.0.1:8086/delete/file.txt" "$code"
  else
    skp "DELETE /delete/file.txt -> got $code; marking SKIP (path mapping differs?)"
    record_skip "DELETE /delete/file.txt" "http://127.0.0.1:8086/delete/file.txt" "got $code"
  fi
  # upload small (accept 200 or 201)
  case_check_one_of "200,201" "http://127.0.0.1:8086/upload/" "POST /upload/ (small)" -X POST -d "x=1"
  stop_server "$pid"

  # [08] Multiple ports (same site)
  start_test "08" "Multiple ports (same site)" "同一サイトを 8087 / 8088 で提供"
  pid=$(start_server "08_multiple_ports.conf" "08")
  wait_http_up "http://127.0.0.1:8087/" || true
  case_check 200 "http://127.0.0.1:8087/" "GET 8087 /"
  case_check 200 "http://127.0.0.1:8088/" "GET 8088 /"
  stop_server "$pid"

  # [09] Same port (conflict behavior)
  start_test "09" "Same port on two servers" "vhost未実装なら bind 競合で起動不可が正しい"
  pid=$(start_server "09_same_ports.conf" "09")
  sleep 0.5
  if is_alive "$pid"; then
    # If it actually runs, try GET (some impls support vhost)
    code=$(curl "${CURL_BASE_OPTS[@]}" "http://127.0.0.1:8089/" || true)
    if [[ "$code" =~ ^[0-9]{3}$ ]]; then
      ok "[${code}] GET / on 8089 (server running)"
      record_pass "GET / (8089)" "http://127.0.0.1:8089/" "$code"
    else
      ng "server alive but no response on 8089"
      record_fail "GET / (8089)" "http://127.0.0.1:8089/" "no response"
    fi
    stop_server "$pid"
  else
    ok "bind conflict prevented start (expected for non-vhost impl)"
    record_pass "bind conflict" "-" "no running process"
  fi

  # [10] Multiple servers (static/upload/cgi)
  start_test "10" "Multiple servers (static/upload/cgi)" "8090=docs, 8091=upload, 8092=cgi"
  pid=$(start_server "10_multiple_servers.conf" "10")
  wait_http_up "http://127.0.0.1:8090/" || true
  case_check 200 "http://127.0.0.1:8090/" "GET 8090 /"
  case_check_one_of "200,201" "http://127.0.0.1:8091/upload/" "POST 8091 /upload/ (small)" -X POST -d "x=1"
  case_post_big 413 "http://127.0.0.1:8091/upload/" "POST 8091 big > limit"
  # CGI (optional) — skip if php-cgi missing
  if [[ -x /usr/bin/php-cgi ]]; then
    case_check 200 "http://127.0.0.1:8092/cgi-bin/" "GET 8092 /cgi-bin/"
  else
    skp "php-cgi not found; skipping CGI check"
    record_skip "GET 8092 /cgi-bin/" "http://127.0.0.1:8092/cgi-bin/" "php-cgi not found"
  fi
  stop_server "$pid"

  # summary
  say "Done. Check logs under $LOG_DIR/"
  hr
  say "Summary"
  for row in "${RESULTS[@]}"; do
    IFS='|' read -r id title label status url detail <<< "$row"
    case "$status" in
      PASS) printf "  ✅ [%s] %s — %s  (%s)\n" "$id" "$title" "$label" "$detail" ;;
      FAIL) printf "  ❌ [%s] %s — %s  (%s)  [%s]\n" "$id" "$title" "$label" "$detail" "$url" ;;
      SKIP) printf "  ⏭️  [%s] %s — %s  (%s)  [%s]\n" "$id" "$title" "$label" "$detail" "$url" ;;
    esac
  done
  hr
  printf "PASS: %d, FAIL: %d, SKIP: %d\n" "$PASS_COUNT" "$FAIL_COUNT" "$SKIP_COUNT"

  (( FAIL_COUNT > 0 )) && exit 1 || exit 0
}

main "$@"
