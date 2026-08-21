#!/usr/bin/env bash
set -Eeuo pipefail

root="${SOLAR_IOT_ROOT:-/opt/solar-iot}"
host_env="${root}/config/host.env"
runtime_env="${root}/secrets/runtime.env"

for tool in docker curl openssl getent cosign sha256sum; do
  command -v "${tool}" >/dev/null 2>&1 || {
    printf 'missing required IoT production tool: %s\n' "${tool}" >&2
    exit 1
  }
done

read_env() {
  local key="$1"
  local file="$2"
  sed -n "s/^${key}=//p" "${file}" | tail -n 1 | tr -d '\r'
}

[[ -r "${host_env}" && -r "${runtime_env}" ]] || {
  printf 'IoT host or runtime environment file is not readable\n' >&2
  exit 1
}

cosign_public_key="${SOLAR_COSIGN_PUBLIC_KEY:-/opt/solar-platform/config/cosign.pub}"
[[ -r "${cosign_public_key}" ]] || {
  printf 'Cosign public key is not readable: %s\n' "${cosign_public_key}" >&2
  exit 1
}
openssl pkey -pubin -in "${cosign_public_key}" -noout

domain="$(read_env IOT_PUBLIC_DOMAIN "${host_env}")"
public_ip="$(read_env IOT_PUBLIC_IPV4 "${host_env}")"
username="$(read_env Mqtt__Username "${runtime_env}")"
password="$(read_env Mqtt__Password "${runtime_env}")"
tls_dir="$(read_env MQTT_TLS_DIR "${host_env}")"
auth_dir="$(read_env MQTT_AUTH_DIR "${host_env}")"
data_dir="$(read_env MQTT_DATA_DIR "${host_env}")"
log_dir="$(read_env MQTT_LOG_DIR "${host_env}")"
port="$(read_env MQTT_TLS_PORT "${host_env}")"

[[ -n "${domain}" && -n "${public_ip}" && -n "${username}" && -n "${password}" ]] || {
  printf 'IoT production environment is incomplete\n' >&2
  exit 1
}
[[ "${username}" == "backend-bridge" ]] || {
  printf 'Mqtt__Username must be backend-bridge because the production ACL grants that identity\n' >&2
  exit 1
}
[[ "${port}" == "8883" ]] || {
  printf 'MQTT_TLS_PORT must be 8883 for the backend, firewall and firmware contract\n' >&2
  exit 1
}
[[ ! "${password}" =~ (CHANGE_ME|PLACEHOLDER|YOUR_) ]] || {
  printf 'Mqtt__Password still contains a placeholder\n' >&2
  exit 1
}

resolved="$(getent ahostsv4 "${domain}" | awk 'NR == 1 {print $1}')"
[[ "${resolved}" == "${public_ip}" ]] || {
  printf 'DNS mismatch: %s resolves to %s, expected %s\n' \
    "${domain}" "${resolved:-nothing}" "${public_ip}" >&2
  exit 1
}

[[ -s "${tls_dir}/tls.crt" && -s "${tls_dir}/tls.key" ]] || {
  printf 'MQTT TLS files are missing; deploy backend Certificate and run TLS sync first\n' >&2
  exit 1
}
for directory in "${auth_dir}" "${data_dir}" "${log_dir}"; do
  [[ -d "${directory}" && -w "${directory}" ]] || {
    printf 'Mosquitto directory is missing or not writable: %s\n' "${directory}" >&2
    exit 1
  }
done

openssl x509 -in "${tls_dir}/tls.crt" -noout -checkend 604800 >/dev/null
openssl x509 -in "${tls_dir}/tls.crt" -noout -checkhost "${domain}" >/dev/null

certificate_public_key="$({
  openssl x509 -in "${tls_dir}/tls.crt" -pubkey -noout |
    openssl pkey -pubin -outform DER
} | sha256sum | awk '{print $1}')"
private_public_key="$({
  openssl pkey -in "${tls_dir}/tls.key" -pubout -outform DER
} | sha256sum | awk '{print $1}')"
[[ "${certificate_public_key}" == "${private_public_key}" ]] || {
  printf 'MQTT TLS certificate and private key do not match\n' >&2
  exit 1
}

docker info >/dev/null
docker compose version >/dev/null

printf 'IoT production preflight passed.\n'
