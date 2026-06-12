#!/usr/bin/env bash
# ==================================================================
# Sprint 0 — infra bootstrap
# Chạy 1 lệnh duy nhất khi clone repo về máy mới:
#   cd infra && ./setup.sh
#
# Script này:
#   1. Tạo .env từ env.example.txt nếu chưa có (TRƯỚC khi docker compose up)
#      → tránh lỗi TimescaleDB initdb với env trống → role postgres không tạo
#   2. Cảnh báo nếu port quan trọng đang bị chiếm
#   3. docker compose up -d
#   4. Đợi healthy và verify từng service
# ==================================================================
set -euo pipefail

cd "$(dirname "$0")"

COMPOSE_FILE="docker-compose.dev.yml"
ENV_FILE=".env"
ENV_EXAMPLE="env.example.txt"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; NC='\033[0m'
ok()   { echo -e "${GREEN}✓${NC} $*"; }
warn() { echo -e "${YELLOW}⚠${NC} $*"; }
fail() { echo -e "${RED}✗${NC} $*"; }

# --- Bước 1: .env ----------------------------------------------------
if [[ -f "$ENV_FILE" ]]; then
  ok ".env đã có"
else
  if [[ ! -f "$ENV_EXAMPLE" ]]; then
    fail "Thiếu $ENV_EXAMPLE — clone lại repo"
    exit 1
  fi
  cp "$ENV_EXAMPLE" "$ENV_FILE"
  ok "Tạo .env từ $ENV_EXAMPLE"
fi

# --- Bước 2: port check ----------------------------------------------
# shellcheck disable=SC1091
set -a; source "$ENV_FILE"; set +a

check_port() {
  local port=$1 name=$2
  if lsof -nP -iTCP:"$port" -sTCP:LISTEN 2>/dev/null | grep -q LISTEN; then
    warn "Port $port ($name) đang bị chiếm — docker compose có thể fail"
    lsof -nP -iTCP:"$port" -sTCP:LISTEN | tail -n +1 | head -3
  else
    ok "Port $port ($name) free"
  fi
}

echo ""
echo "--- Port check ---"
check_port "${POSTGRES_PORT:-5432}"           "TimescaleDB"
check_port "${REDIS_PORT:-6379}"              "Redis"
check_port "${RABBITMQ_PORT:-5672}"           "RabbitMQ AMQP"
check_port "${RABBITMQ_MGMT_PORT:-15672}"     "RabbitMQ Mgmt"
check_port "${EMQX_MQTT_PORT:-11883}"         "EMQX MQTT"
check_port "${EMQX_DASHBOARD_PORT:-18083}"    "EMQX Dashboard"

# --- Bước 3: docker compose up ---------------------------------------
echo ""
echo "--- docker compose up ---"
docker compose -f "$COMPOSE_FILE" up -d

# --- Bước 4: wait healthy --------------------------------------------
echo ""
echo "--- Đợi service healthy (tối đa 60s) ---"
deadline=$((SECONDS + 60))
while [[ $SECONDS -lt $deadline ]]; do
  unhealthy=$(docker compose -f "$COMPOSE_FILE" ps --format '{{.Name}}\t{{.Status}}' \
              | grep -cv 'healthy)' || true)
  if [[ "$unhealthy" -eq 0 ]]; then
    break
  fi
  sleep 2
done

docker compose -f "$COMPOSE_FILE" ps

# --- Bước 5: smoke verify --------------------------------------------
echo ""
echo "--- Verify ---"

if docker compose -f "$COMPOSE_FILE" exec -T timescaledb \
   psql -U "${POSTGRES_USER:-postgres}" -d battery_service_dev -tAc \
   "SELECT extversion FROM pg_extension WHERE extname='timescaledb';" 2>/dev/null \
   | grep -q '^2\.'; then
  ok "TimescaleDB extension OK trên battery_service_dev"
else
  fail "TimescaleDB init lỗi — chạy: docker compose -f $COMPOSE_FILE down -v && ./setup.sh"
  exit 1
fi

if docker compose -f "$COMPOSE_FILE" exec -T redis redis-cli ping 2>/dev/null | grep -q PONG; then
  ok "Redis PONG"
else
  fail "Redis không phản hồi"
fi

if curl -fsS -u "${RABBITMQ_USER:-guest}:${RABBITMQ_PASS:-guest}" \
   "http://localhost:${RABBITMQ_MGMT_PORT:-15672}/api/overview" >/dev/null 2>&1; then
  ok "RabbitMQ Mgmt OK (http://localhost:${RABBITMQ_MGMT_PORT:-15672})"
else
  fail "RabbitMQ Mgmt không truy cập được"
fi

if curl -fsS "http://localhost:${EMQX_DASHBOARD_PORT:-18083}/status" >/dev/null 2>&1; then
  ok "EMQX Dashboard OK (http://localhost:${EMQX_DASHBOARD_PORT:-18083})"
else
  fail "EMQX Dashboard không truy cập được"
fi

echo ""
ok "Sprint 0 infra ready."
