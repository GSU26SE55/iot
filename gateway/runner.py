from __future__ import annotations

import logging
import time
from collections.abc import Iterable
from dataclasses import replace
from uuid import uuid4

from gateway.api.client import ApiClientError, GatewayApiClient
from gateway.bms.base import BmsAdapter, BmsAdapterError
from gateway.bms.factory import create_bms_adapter
from gateway.calibration import apply_calibrations
from gateway.models import GatewayConfig, SensorReading
from gateway.queue.local_store import LocalQueue, QueueMessage
from gateway.telemetry.heartbeat import collect_heartbeat
from gateway.validation import ReadingValidationError, validate_reading


logger = logging.getLogger(__name__)


class GatewayRunner:
    def __init__(self, config: GatewayConfig) -> None:
        self.config = config
        self.client = GatewayApiClient(config)
        self.queue = LocalQueue(config.queue_db_path)
        self.adapter: BmsAdapter = create_bms_adapter(config)

    def provision(self) -> None:
        result = self.client.provision()
        if not result.is_success:
            raise RuntimeError(f"Provision failed: {result.status_code} {result.body}")
        logger.info("Provision success: %s", result.body)

    def firmware_check(self) -> None:
        result = self.client.firmware_check()
        if not result.is_success:
            raise RuntimeError(f"Firmware check failed: {result.status_code} {result.body}")
        logger.info("Firmware check response: %s", result.body)

    def run_once(self, *, send_heartbeat: bool = True, flush_first: bool = True) -> None:
        if flush_first:
            self.flush_queue()

        readings = self._read_valid_readings()
        if readings:
            self.enqueue_readings(readings)

        self.flush_queue()

        if send_heartbeat:
            self.send_heartbeat()

    def flush_queue(self, *, limit: int = 50) -> None:
        for message in self.queue.due_messages(limit=limit):
            self._send_queue_message(message)

    def enqueue_readings(self, readings: list[SensorReading]) -> None:
        for batch in _chunks(readings, self.config.batch_size):
            payload = self.client.build_sensor_batch_payload(batch)
            message_id = self.queue.enqueue(
                kind="sensor_batch",
                payload=payload,
                idempotency_key=str(uuid4()),
            )
            logger.info("Queued sensor batch id=%s readings=%s", message_id, len(batch))

    def send_heartbeat(self) -> None:
        heartbeat = collect_heartbeat(
            connected_sensor_count=self.adapter.connected_sensor_count,
            local_queue_depth=self.queue.depth(),
        )
        try:
            result = self.client.send_heartbeat(heartbeat)
        except ApiClientError as exc:
            logger.warning("Heartbeat failed: %s", exc)
            return

        if result.is_success:
            logger.info("Heartbeat sent queueDepth=%s", heartbeat.local_queue_depth)
        else:
            logger.warning("Heartbeat rejected: status=%s body=%s", result.status_code, result.body)

    def run_forever(self) -> None:
        logger.info(
            "Gateway started device=%s adapter=%s ingestMode=%s",
            self.config.device_code,
            self.config.adapter,
            self.config.ingest_mode,
        )
        next_read_at = 0.0
        next_heartbeat_at = 0.0
        while True:
            now = time.monotonic()
            if now >= next_read_at:
                self.run_once(send_heartbeat=False)
                next_read_at = now + self.config.polling_interval_sec

            if now >= next_heartbeat_at:
                self.send_heartbeat()
                next_heartbeat_at = now + self.config.heartbeat_interval_sec

            self.flush_queue(limit=20)
            time.sleep(1)

    def close(self) -> None:
        self.adapter.close()

    def _read_valid_readings(self) -> list[SensorReading]:
        try:
            raw_readings = self.adapter.read()
        except BmsAdapterError as exc:
            logger.warning("BMS read failed: %s", exc)
            return []

        valid: list[SensorReading] = []
        for reading in raw_readings:
            calibrated = apply_calibrations(reading, self.config.calibration_rules)
            try:
                validate_reading(calibrated, max_future_skew_sec=self.config.clock_skew_max_sec)
            except ReadingValidationError as exc:
                logger.warning(
                    "Dropped invalid reading serial=%s source=%s reason=%s",
                    reading.battery_asset_serial,
                    reading.sensor_source_code,
                    exc,
                )
                continue
            valid.append(calibrated)

        logger.info("Read %s valid sensor readings", len(valid))
        return valid

    def _send_queue_message(self, message: QueueMessage) -> None:
        try:
            if message.kind != "sensor_batch":
                self.queue.mark_failed(message.id, f"unsupported message kind: {message.kind}")
                return

            result = self.client.send_sensor_batch_payload(message.payload, message.idempotency_key)
        except ApiClientError as exc:
            self.queue.mark_failed(message.id, str(exc))
            logger.warning("Queued message failed id=%s error=%s", message.id, exc)
            return

        if result.is_success:
            self.queue.mark_sent(message.id)
            logger.info("Sent queued message id=%s status=%s", message.id, result.status_code)
        else:
            error = f"HTTP {result.status_code}: {result.body}"
            self.queue.mark_failed(message.id, error)
            logger.warning("Backend rejected queued message id=%s %s", message.id, error)


def _chunks(items: list[SensorReading], size: int) -> Iterable[list[SensorReading]]:
    for index in range(0, len(items), size):
        yield items[index : index + size]


def with_dry_run(config: GatewayConfig) -> GatewayConfig:
    return replace(config, dry_run=True)

