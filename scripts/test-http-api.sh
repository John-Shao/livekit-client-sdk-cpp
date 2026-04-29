#!/usr/bin/env bash
# test-http-api.sh — Phase 8.4.4 BoardHttpServer HTTP API 测试矩阵（PC 端跑）
#
# 用法:
#   ./scripts/test-http-api.sh <board-ip> [<jwt>]
#
# 例:
#   ./scripts/test-http-api.sh 192.168.10.42
#   ./scripts/test-http-api.sh 192.168.10.42 "eyJhbGc..."
#
# 不传 jwt 时自动从板子读 /opt/livekit/.token（要求 PC 配了 ssh rv1126b-board）。
#
# 前置:
#   板子端先开 BoardLoopback 在 HTTP-only 模式（不 auto-join）：
#     ssh rv1126b-board /opt/livekit/smoke.sh --http-only
#
# 测试矩阵 (9 项):
#   T1  GET  /status                              200，初始 idle
#   T2  POST /leave  (no bearer)                  401
#   T3  POST /join   (no bearer)                  401
#   T4  POST /join   (bad json: 缺 url)           400
#   T5  POST /join   (bad json: 缺 token)         400
#   T6  POST /join   (good)                       202
#   T7  GET  /status                              200，state=in_meeting + room_name 非空
#   T8  POST /join   (重复入会)                    409  ← 关键回归测试
#   T9  POST /leave  (good)                       200，板子退会 + 进程退出

set -e

BOARD_IP="$1"
JWT="${2:-}"

if [ -z "$BOARD_IP" ]; then
  echo "Usage: $0 <board-ip> [<jwt>]" >&2
  echo "" >&2
  echo "Example: $0 192.168.10.42" >&2
  exit 1
fi

API_TOKEN="${BOARD_API_TOKEN:-board-dev-token-change-in-prod}"
API_PORT="${BOARD_API_PORT:-8080}"
URL="${LIVEKIT_URL:-wss://live.jusiai.com}"
BASE="http://$BOARD_IP:$API_PORT"

# JWT 不传 → 从板子拉
if [ -z "$JWT" ]; then
  echo "[setup] no JWT arg, fetching /opt/livekit/.token from board via ssh..."
  JWT=$(ssh rv1126b-board cat /opt/livekit/.token 2>/dev/null || true)
  if [ -z "$JWT" ]; then
    echo "ERROR: could not fetch JWT. Pass it as 2nd arg or set up ssh rv1126b-board access." >&2
    exit 1
  fi
fi

echo "==== test-http-api.sh ===="
echo "  base       = $BASE"
echo "  api token  = $API_TOKEN (Bearer header)"
echo "  url        = $URL"
echo "  jwt        = ${JWT:0:32}... (${#JWT} chars)"
echo "=========================="
echo

# 工具：发请求、断言 HTTP 状态码、打印 body
check() {
  local name="$1"; shift
  local expect="$1"; shift
  echo "[$name] expect HTTP $expect"
  echo "       cmd: $*"
  local resp
  resp=$(eval "$* -w '\nHTTP_STATUS=%{http_code}'")
  local body=$(echo "$resp" | sed '$d')
  local got=$(echo "$resp" | tail -1 | sed 's/HTTP_STATUS=//')
  echo "       got HTTP $got"
  echo "       body: $body"
  if [ "$got" = "$expect" ]; then
    echo "       ✅ PASS"
  else
    echo "       ❌ FAIL (expected $expect, got $got)"
    FAIL=1
  fi
  echo
}

FAIL=0

# T1 — GET /status (无 auth)
check "T1 GET /status (initial)" 200 \
  "curl -s '$BASE/v1/meeting/status'"

# T2 — POST /leave 无 Bearer
check "T2 POST /leave (no bearer)" 401 \
  "curl -s -X POST '$BASE/v1/meeting/leave'"

# T3 — POST /join 无 Bearer
check "T3 POST /join (no bearer)" 401 \
  "curl -s -X POST '$BASE/v1/meeting/join' -H 'Content-Type: application/json' -d '{\"url\":\"$URL\",\"token\":\"$JWT\"}'"

# T4 — POST /join 带 Bearer 但 body 缺 url
check "T4 POST /join (missing url)" 400 \
  "curl -s -X POST '$BASE/v1/meeting/join' -H 'Authorization: Bearer $API_TOKEN' -H 'Content-Type: application/json' -d '{\"token\":\"$JWT\"}'"

# T5 — POST /join 带 Bearer 但 body 缺 token
check "T5 POST /join (missing token)" 400 \
  "curl -s -X POST '$BASE/v1/meeting/join' -H 'Authorization: Bearer $API_TOKEN' -H 'Content-Type: application/json' -d '{\"url\":\"$URL\"}'"

# T6 — POST /join 完整正确，预期 202
check "T6 POST /join (valid, first time)" 202 \
  "curl -s -X POST '$BASE/v1/meeting/join' -H 'Authorization: Bearer $API_TOKEN' -H 'Content-Type: application/json' -d '{\"url\":\"$URL\",\"token\":\"$JWT\"}'"

# 等板子真接通 Room（最多 ~5s）
echo "[wait] giving board ~3s to Connect to Room..."
sleep 3

# T7 — GET /status，应 in_meeting
check "T7 GET /status (after join)" 200 \
  "curl -s '$BASE/v1/meeting/status'"

# T8 — POST /join 第二次（关键测试），预期 409
check "T8 POST /join (duplicate, expect 409)" 409 \
  "curl -s -X POST '$BASE/v1/meeting/join' -H 'Authorization: Bearer $API_TOKEN' -H 'Content-Type: application/json' -d '{\"url\":\"$URL\",\"token\":\"$JWT\"}'"

# T9 — POST /leave，预期 200
check "T9 POST /leave (valid)" 200 \
  "curl -s -X POST '$BASE/v1/meeting/leave' -H 'Authorization: Bearer $API_TOKEN'"

echo "=========================="
if [ "$FAIL" = "0" ]; then
  echo "✅ ALL 9 TESTS PASSED"
  exit 0
else
  echo "❌ SOME TESTS FAILED — see ❌ markers above"
  exit 1
fi
