"""Command line: list scenarios or print one as replay events."""

from __future__ import annotations

import argparse
import sys

from isrt_sim.scenarios import SCENARIOS


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="python -m isrt_sim")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("list", help="list the scenarios")
    events = sub.add_parser("events", help="print a scenario as replay events")
    events.add_argument("scenario", choices=sorted(SCENARIOS))
    args = parser.parse_args(argv)

    if args.command == "list":
        for name, s in SCENARIOS.items():
            print(f"{name:10} {s.description}")
        return 0
    for line in SCENARIOS[args.scenario].lines():
        sys.stdout.write(line + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
