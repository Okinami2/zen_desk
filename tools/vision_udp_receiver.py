#!/usr/bin/env python3

import argparse
import json
import socket
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Receive vision_service JSON telemetry over UDP."
    )
    parser.add_argument("--bind", default="0.0.0.0", help="local IPv4 address")
    parser.add_argument("--port", type=int, default=9100, help="local UDP port")
    parser.add_argument("--output", type=Path, help="append JSON records here")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not 1 <= args.port <= 65535:
        print("port must be in 1..65535", file=sys.stderr)
        return 2

    output = args.output.open("a", encoding="utf-8") if args.output else None
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.bind, args.port))
    print(f"listening on udp://{args.bind}:{args.port}")

    try:
        while True:
            payload, peer = sock.recvfrom(65535)
            try:
                record = json.loads(payload.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                print(
                    f"invalid packet from {peer[0]}:{peer[1]}: {exc}",
                    file=sys.stderr,
                )
                continue

            line = json.dumps(record, ensure_ascii=False, separators=(",", ":"))
            print(line, flush=True)
            if output:
                output.write(line + "\n")
                output.flush()
    except KeyboardInterrupt:
        return 0
    finally:
        sock.close()
        if output:
            output.close()


if __name__ == "__main__":
    raise SystemExit(main())
