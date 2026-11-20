"""Each scenario through the engine's replay: the alerts it must raise.

Needs the Linux engine build (``./dev build``) at ``build/engine/isrt``;
without it the tests are skipped locally and fail under ISRT_REQUIRE_ENGINE=1.
"""

from __future__ import annotations

import json
import os
import subprocess
from collections import Counter
from pathlib import Path

import pytest

from isrt_sim.scenarios import SCENARIOS, S

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "engine" / "isrt"

if not BIN.exists():
    if os.environ.get("ISRT_REQUIRE_ENGINE") == "1":
        raise RuntimeError(f"engine binary not found: {BIN}")
    pytest.skip(f"engine binary not built: {BIN}", allow_module_level=True)


def replay(name: str) -> list[dict]:
    text = "".join(line + "\n" for line in SCENARIOS[name].lines())
    proc = subprocess.run(  # fixed in-tree binary, argument list, no shell
        [BIN, "replay"], input=text, capture_output=True, text=True, check=False
    )
    assert proc.returncode == 0, proc.stderr
    return [json.loads(line) for line in proc.stdout.splitlines()]


def alerts(out: list[dict]) -> Counter:
    return Counter((o["node"], o["kind"]) for o in out if "kind" in o)


def health(out: list[dict]) -> dict[str, int]:
    return {o["node"]: o["health"] for o in out if "health" in o}


def test_normal_raises_nothing():
    out = replay("normal")
    assert alerts(out) == Counter()
    assert all(h >= 60 for h in health(out).values())


def test_offline_node_goes_missing_on_time_and_recovers():
    out = replay("offline")
    assert alerts(out) == Counter({("esp01", "missing"): 1, ("esp01", "recovered"): 1})
    missing = next(o for o in out if o.get("kind") == "missing")
    # five 100 ms periods after the last message before 20 s, within jitter
    assert 20.4e9 <= missing["t"] <= 20.6e9
    assert missing["severity"] == "critical"


def test_slow_node_degrades():
    out = replay("degrading")
    a = alerts(out)
    assert a[("esp02", "degrading")] == 1
    assert a[("esp02", "missing")] == 0
    assert set(n for n, _ in a) == {"esp02"}
    assert health(out)["esp02"] <= 70


def test_flood_is_a_storm_and_ends():
    out = replay("storm")
    a = alerts(out)
    assert a[("esp01", "storm")] == 1
    recovered = [o for o in out if o.get("kind") == "recovered"]
    assert [o["cleared"] for o in recovered] == ["storm"]
    assert set(n for n, _ in a) == {"esp01"}


def test_duplicates_are_reported():
    a = alerts(replay("duplicate"))
    assert a[("esp03", "duplicate")] >= 4
    assert set(n for n, _ in a) == {"esp03"}


def test_reboot_is_reported_once():
    assert alerts(replay("reboot")) == Counter({("esp02", "rebooted"): 1})


def test_old_boot_replay_does_not_flip_the_boot():
    out = replay("replay")
    a = alerts(out)
    assert a[("esp01", "rebooted")] == 1  # the real restart only
    assert a[("esp01", "reordered")] >= 1  # the replays are stale


def test_old_boot_replay_does_not_keep_a_silent_node_alive():
    out = replay("replay")
    missing = [o for o in out if o.get("node") == "esp01" and o.get("kind") == "missing"]
    assert len(missing) == 1
    assert 20 * S < missing[0]["t"] < 21 * S
    stale_after = [
        int(w[1])
        for w in (line.split() for line in SCENARIOS["replay"].lines())
        if w[0] == "msg" and w[2] == "esp01" and w[3] == "1" and int(w[1]) > missing[0]["t"]
    ]
    assert stale_after, "replays must still be arriving when missing fires"
    recovered = [o for o in out if o.get("node") == "esp01" and o.get("kind") == "recovered"]
    assert [o["cleared"] for o in recovered] == ["missing"]
    assert recovered[0]["t"] >= 24 * S
