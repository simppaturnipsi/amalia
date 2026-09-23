#!/usr/bin/env python3
from __future__ import annotations

import argparse
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path

from update_protocol import publish


def main() -> int:
    parser = argparse.ArgumentParser(description="Publish a signed Amalia update")
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--channel", choices=["stable", "test"], required=True)
    parser.add_argument("--platform", choices=["windows", "ubuntu"], required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--minimum-supported-version", default="")
    parser.add_argument("--mandatory", action="store_true")
    parser.add_argument("--log-file", type=Path, default=Path("/var/log/amalia/update-publisher.log"))
    args = parser.parse_args()
    logger = logging.getLogger("amalia.update-publisher")
    try:
        args.log_file.parent.mkdir(parents=True, exist_ok=True)
        handler = RotatingFileHandler(args.log_file, maxBytes=5_000_000, backupCount=5, encoding="utf-8")
        handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
        logger.addHandler(handler)
        logger.setLevel(logging.INFO)
        metadata = publish(args.package, args.repository, args.channel, args.platform, args.version,
                           args.private_key, args.minimum_supported_version, args.mandatory)
    except Exception as error:
        logger.error("Update publish failed version=%s channel=%s platform=%s type=%s",
                     args.version, args.channel, args.platform, type(error).__name__)
        raise
    logger.info("Update published version=%s channel=%s platform=%s sha256=%s",
                metadata.version, metadata.channel, metadata.platform, metadata.sha256)
    print(f"Published {metadata.version} ({metadata.channel}/{metadata.platform}) SHA-256={metadata.sha256}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
