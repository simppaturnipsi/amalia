#!/usr/bin/env python3
"""Capture/compare existing-service HTTP behavior before and after Amalia install."""

from __future__ import annotations

import argparse
import hashlib
import json
import ssl
import urllib.request
from pathlib import Path


def capture(urls: list[str]) -> dict:
    result = {}
    context = ssl.create_default_context()
    for url in urls:
        request = urllib.request.Request(url, headers={"User-Agent": "AmaliaRegressionCheck/1.0"})
        try:
            with urllib.request.urlopen(request, timeout=15, context=context) as response:
                body = response.read(10 * 1024 * 1024)
                result[url] = {"status": response.status, "content_type": response.headers.get_content_type(),
                               "sha256": hashlib.sha256(body).hexdigest(), "length": len(body)}
        except Exception as error:
            result[url] = {"error": type(error).__name__}
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", action="append", default=[])
    parser.add_argument("--write", type=Path)
    parser.add_argument("--compare", type=Path)
    args = parser.parse_args()
    current = capture(args.url)
    if args.write:
        args.write.write_text(json.dumps(current, indent=2) + "\n")
    if args.compare:
        baseline = json.loads(args.compare.read_text())
        if current != baseline:
            print(json.dumps({"baseline": baseline, "current": current}, indent=2))
            return 1
    print(json.dumps(current, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
