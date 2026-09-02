#!/usr/bin/env bash
set -Eeuo pipefail

release_sha="${1:?full Git SHA is required}"
root="${SOLAR_IOT_ROOT:-/opt/solar-iot}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
payload_dir="$(cd -- "${script_dir}/../.." && pwd)"

[[ "${release_sha}" =~ ^[0-9a-f]{40}$ ]] || {
  printf 'release SHA must contain exactly 40 lowercase hex characters\n' >&2
  exit 2
}

host_env="${root}/config/host.env"
runtime_env="${root}/secrets/runtime.env"
image_lock="${payload_dir}/deploy/production/image-lock.env"

read_env() {
  local key="$1"
  local file="$2"
  sed -n "s/^${key}=//p" "${file}" | tail -n 1 | tr -d '\r'
}

mosquitto_image="$(read_env MOSQUITTO_IMAGE "${image_lock}")"
[[ "${mosquitto_image}" =~ ^[^[:space:]@]+@sha256:[0-9a-f]{64}$ ]] || {
  printf 'MOSQUITTO_IMAGE must be an immutable image reference\n' >&2
  exit 1
}

"${script_dir}/preflight-production.sh"

cosign_public_key="${SOLAR_COSIGN_PUBLIC_KEY:-/opt/solar-platform/config/cosign.pub}"
cosign verify --key "${cosign_public_key}" "${mosquitto_image}" >/dev/null

umask 027
release_dir="${root}/releases/${release_sha}"
mkdir -p "${root}/releases"
if [[ -e "${release_dir}" ]]; then
  cmp "${image_lock}" "${release_dir}/deploy/production/image-lock.env" || {
    printf 'immutable IoT release exists with a different image lock\n' >&2
    exit 1
  }
else
  mkdir -p "${release_dir}"
  cp -a "${payload_dir}/infra" "${release_dir}/infra"
  cp -a "${payload_dir}/deploy" "${release_dir}/deploy"
fi

auth_dir="$(read_env MQTT_AUTH_DIR "${host_env}")"
mqtt_user="$(read_env Mqtt__Username "${runtime_env}")"
mqtt_password="$(read_env Mqtt__Password "${runtime_env}")"

# BatteryService may populate device credentials before the broker is deployed.
# Always (re)install the bridge account while preserving those managed entries;
# checking only for a non-empty file can otherwise lock the backend out.
docker run --rm \
  --user 0:0 \
  --entrypoint /bin/sh \
  -e "MQTT_BOOTSTRAP_USER=${mqtt_user}" \
  -e "MQTT_BOOTSTRAP_PASSWORD=${mqtt_password}" \
  -v "${auth_dir}:/work" \
  "${mosquitto_image}" \
  -ec '
    set -eu
    umask 027

    password_file=/work/passwd
    bridge_file="/work/.bridge-passwd.$$"
    merged_file="/work/.passwd.new.$$"
    trap '\''rm -f "${bridge_file}" "${merged_file}"'\'' EXIT HUP INT TERM

    mosquitto_passwd -c -b "${bridge_file}" \
      "${MQTT_BOOTSTRAP_USER}" "${MQTT_BOOTSTRAP_PASSWORD}"

    {
      cat "${bridge_file}"
      if [ -s "${password_file}" ]; then
        awk -F: -v user="${MQTT_BOOTSTRAP_USER}" '\''$1 != user'\'' \
          "${password_file}"
      fi
    } > "${merged_file}"

    chown 10001:10001 "${merged_file}"
    chmod 0640 "${merged_file}"
    mv -f "${merged_file}" "${password_file}"
  '

compose() {
  docker compose \
    --project-name solar-iot \
    --env-file "${host_env}" \
    --env-file "${runtime_env}" \
    --env-file "${release_dir}/deploy/production/image-lock.env" \
    -f "${release_dir}/infra/docker-compose.prod.yml" \
    "$@"
}

previous_release=""
if [[ -L "${root}/current" ]]; then
  previous_release="$(readlink -f "${root}/current")"
fi

rollback_on_failure() {
  status=$?
  if (( status == 0 )); then
    return
  fi
  printf 'IoT deployment failed; attempting rollback\n' >&2
  if [[ -n "${previous_release}" && -d "${previous_release}" ]]; then
    docker compose \
      --project-name solar-iot \
      --env-file "${host_env}" \
      --env-file "${runtime_env}" \
      --env-file "${previous_release}/deploy/production/image-lock.env" \
      -f "${previous_release}/infra/docker-compose.prod.yml" \
      up -d --remove-orphans --wait --wait-timeout 180 || true
  else
    compose down --remove-orphans || true
  fi
  exit "${status}"
}

compose config --quiet
compose pull
trap rollback_on_failure EXIT
compose up -d --remove-orphans --wait --wait-timeout 180

domain="$(read_env IOT_PUBLIC_DOMAIN "${host_env}")"
port="$(read_env MQTT_TLS_PORT "${host_env}")"
tls_attempts=0
until openssl s_client \
  -connect "${domain}:${port}" \
  -servername "${domain}" \
  -verify_hostname "${domain}" \
  -verify_return_error </dev/null 2>/dev/null | grep -q 'Verify return code: 0'; do
  tls_attempts=$((tls_attempts + 1))
  if (( tls_attempts >= 18 )); then
    docker logs --tail 200 solar-iot-mosquitto >&2 || true
    printf 'public MQTT TLS smoke check failed\n' >&2
    exit 1
  fi
  sleep 5
done

if [[ -n "${previous_release}" && "${previous_release}" != "${release_dir}" ]]; then
  ln -sfn "${previous_release}" "${root}/previous"
fi
ln -sfn "${release_dir}" "${root}/current"

trap - EXIT
printf 'IoT production deployed: sha=%s image=%s\n' "${release_sha}" "${mosquitto_image}"
