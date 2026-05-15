from __future__ import annotations

import json
import logging
import urllib.error
import urllib.request
from datetime import timezone
from typing import Any
from uuid import uuid4

from gateway.models import ApiResult, GatewayConfig, Heartbeat, SensorReading, to_iso_utc, utc_now


logger = logging.getLogger(__name__)


class ApiClientError(RuntimeError):
    pass


class GatewayApiClient:
    def __init__(self, config: GatewayConfig) -> None:
        self._config = config

    def build_sensor_batch_payload(self, readings: list[SensorReading]) -> dict[str, Any]:
        if self._config.ingest_mode == "legacy":
            return {"items": [reading.to_legacy_dict() for reading in readings]}

        return {
            "deviceTimestamp": to_iso_utc(utc_now().astimezone(timezone.utc)),
            "readings": [reading.to_production_dict() for reading in readings],
        }

    def send_sensor_batch_payload(self, payload: dict[str, Any], idempotency_key: str) -> ApiResult:
        return self._request(
            "POST",
            "/api/sensor-readings/batch",
            payload,
            extra_headers={"Idempotency-Key": idempotency_key},
        )

    def send_sensor_batch(self, readings: list[SensorReading], idempotency_key: str | None = None) -> ApiResult:
        key = idempotency_key or str(uuid4())
        return self.send_sensor_batch_payload(self.build_sensor_batch_payload(readings), key)

    def provision(self) -> ApiResult:
        return self._request(
            "POST",
            "/api/v1/iot-devices/provision",
            {
                "deviceCode": self._config.device_code,
                "macAddress": self._config.mac_address,
                "model": self._config.model,
                "firmwareVersion": self._config.firmware_version,
            },
        )

    def send_heartbeat(self, heartbeat: Heartbeat) -> ApiResult:
        return self._request("POST", "/api/v1/iot-devices/heartbeat", heartbeat.to_dict())

    def firmware_check(self) -> ApiResult:
        return self._request("GET", "/api/v1/iot-devices/firmware-check", None)

    def update_firmware_log(self, log_id: str, status: str, error_message: str | None = None) -> ApiResult:
        payload: dict[str, Any] = {"status": status}
        if error_message:
            payload["errorMessage"] = error_message
        return self._request("PUT", f"/api/v1/iot-devices/firmware-update-log/{log_id}", payload)

    def _request(
        self,
        method: str,
        path: str,
        payload: dict[str, Any] | None,
        *,
        extra_headers: dict[str, str] | None = None,
    ) -> ApiResult:
        url = f"{self._config.normalized_backend_url}{path}"
        headers = {
            "Accept": "application/json",
            "X-Api-Key": self._config.api_key,
            "X-Device-Code": self._config.device_code,
        }
        if payload is not None:
            headers["Content-Type"] = "application/json"
        if extra_headers:
            headers.update(extra_headers)

        body = None if payload is None else json.dumps(payload, separators=(",", ":")).encode("utf-8")
        if self._config.dry_run:
            logger.info("DRY RUN %s %s payload=%s headers=%s", method, url, payload, _safe_headers(headers))
            return ApiResult(status_code=200, body='{"dryRun":true}')

        request = urllib.request.Request(url, data=body, headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=self._config.request_timeout_sec) as response:
                response_body = response.read().decode("utf-8", errors="replace")
                return ApiResult(status_code=response.status, body=response_body)
        except urllib.error.HTTPError as exc:
            response_body = exc.read().decode("utf-8", errors="replace")
            return ApiResult(status_code=exc.code, body=response_body)
        except urllib.error.URLError as exc:
            raise ApiClientError(str(exc.reason)) from exc
        except TimeoutError as exc:
            raise ApiClientError("request timed out") from exc


def _safe_headers(headers: dict[str, str]) -> dict[str, str]:
    safe = dict(headers)
    if "X-Api-Key" in safe:
        safe["X-Api-Key"] = "***"
    return safe

