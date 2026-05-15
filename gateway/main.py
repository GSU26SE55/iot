from __future__ import annotations

import argparse
import logging
from dataclasses import replace
from pathlib import Path

from gateway.config import ConfigError, load_config
from gateway.logging_config import configure_logging
from gateway.runner import GatewayRunner


def main() -> int:
    parser = argparse.ArgumentParser(description="Solar battery IoT gateway")
    parser.add_argument("--config", default="config/config.example.json", help="Path to gateway JSON config")
    parser.add_argument("--once", action="store_true", help="Read sensors, enqueue, flush, send heartbeat once")
    parser.add_argument("--loop", action="store_true", help="Run forever")
    parser.add_argument("--flush-only", action="store_true", help="Only flush local queue")
    parser.add_argument("--heartbeat-only", action="store_true", help="Only send one heartbeat")
    parser.add_argument("--provision", action="store_true", help="Run device provisioning request")
    parser.add_argument("--firmware-check", action="store_true", help="Run one firmware check request")
    parser.add_argument("--dry-run", action="store_true", help="Print requests instead of sending to backend")
    parser.add_argument("--scenario", help="Override mock scenario: normal, overheat, low_soc, soh_degradation, mixed")
    parser.add_argument("--log-level", help="Override log level")
    args = parser.parse_args()

    try:
        config = load_config(Path(args.config))
    except ConfigError as exc:
        print(f"Config error: {exc}")
        return 2

    if args.dry_run:
        config = replace(config, dry_run=True)
    if args.scenario:
        mock = dict(config.mock)
        mock["scenario"] = args.scenario
        config = replace(config, adapter="mock", mock=mock)
    if args.log_level:
        config = replace(config, log_level=args.log_level.upper())

    configure_logging(config.log_level)
    runner = GatewayRunner(config)
    try:
        if args.provision:
            runner.provision()
            return 0
        if args.firmware_check:
            runner.firmware_check()
            return 0
        if args.flush_only:
            runner.flush_queue()
            return 0
        if args.heartbeat_only:
            runner.send_heartbeat()
            return 0
        if args.loop:
            runner.run_forever()
            return 0

        # Default is one-shot so a fresh checkout can be tested safely.
        runner.run_once()
        return 0
    except KeyboardInterrupt:
        logging.getLogger(__name__).info("Gateway stopped by user")
        return 130
    finally:
        runner.close()


if __name__ == "__main__":
    raise SystemExit(main())

