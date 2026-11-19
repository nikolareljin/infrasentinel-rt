"""Scenarios: node streams with injected faults, written as replay events.

Every scenario is deterministic (seeded jitter), so tests can assert the exact
alerts the engine raises. The event format is the engine's replay input:

    node <name> <period_ms>
    msg  <t_ns> <name> <boot_hex> <seq>
    end  <t_ns>
"""

from __future__ import annotations

import random
from collections.abc import Iterator
from dataclasses import dataclass, field

MS = 1_000_000
S = 1_000_000_000


@dataclass
class Node:
    name: str
    period_ms: int
    boot: str = "1"  # boot counter, hex; raised on each restart
    jitter: float = 0.1  # fraction of the period, uniform +/-


@dataclass
class Fault:
    """Changes one node's stream from `start` to `stop` (seconds)."""

    kind: str  # offline, slow, flood, duplicate, reboot, replay
    node: str
    start: float
    stop: float = 0.0
    factor: float = 3.0  # slow: interval multiplier
    rate: int = 2000  # flood: messages per second


@dataclass
class Scenario:
    name: str
    description: str
    duration: float
    nodes: list[Node]
    faults: list[Fault] = field(default_factory=list)
    seed: int = 7

    def events(self) -> list[tuple[int, str]]:
        """(time_ns, line) pairs, time-sorted; node lines first."""
        rng = random.Random(self.seed)
        out: list[tuple[int, str]] = [(-1, f"node {n.name} {n.period_ms}") for n in self.nodes]
        for node in self.nodes:
            out.extend(self._stream(node, rng))
        out.sort(key=lambda e: e[0])
        out.append((int(self.duration * S), f"end {int(self.duration * S)}"))
        return out

    def lines(self) -> Iterator[str]:
        for _, line in self.events():
            yield line

    def _faults(self, node: str, kind: str) -> list[Fault]:
        return [f for f in self.faults if f.node == node and f.kind == kind]

    def _active(self, node: str, kind: str, t: float) -> Fault | None:
        for f in self._faults(node, kind):
            if f.start <= t and (f.stop == 0.0 or t < f.stop):
                return f
        return None

    def _stream(self, node: Node, rng: random.Random) -> list[tuple[int, str]]:
        events: list[tuple[int, str]] = []
        boot, seq, t = node.boot, 0, 0.0
        period = node.period_ms / 1000
        rebooted: set[int] = set()

        def emit(at: float, b: str, s: int) -> None:
            events.append((int(at * S), f"msg {int(at * S)} {node.name} {b} {s}"))

        floods = self._faults(node.name, "flood")
        flood_next = [f.start for f in floods]

        def flood_until(until: float) -> None:
            # The node itself floods (a firmware fault): its own boot, rising seq.
            nonlocal seq
            for i, f in enumerate(floods):
                while flood_next[i] < min(until, f.stop):
                    seq += 1
                    emit(flood_next[i], boot, seq)
                    flood_next[i] += 1 / f.rate

        while True:
            step = period * (1 + rng.uniform(-node.jitter, node.jitter))
            slow = self._active(node.name, "slow", t)
            if slow:
                step *= slow.factor
            t += step
            if t >= self.duration:
                break
            flood_until(t)
            if self._active(node.name, "offline", t):
                continue
            for i, f in enumerate(self._faults(node.name, "reboot")):
                if i not in rebooted and t >= f.start:
                    rebooted.add(i)
                    boot, seq = f"{i + 2:x}", 0
            seq += 1
            emit(t, boot, seq)
            if self._active(node.name, "duplicate", t):
                emit(t + 0.001, boot, seq)
            if self._active(node.name, "replay", t):
                emit(t + 0.002, node.boot, 1)  # an old message from the first boot
        return events


def _three() -> list[Node]:
    return [Node("esp01", 100), Node("esp02", 500), Node("esp03", 1000)]


SCENARIOS: dict[str, Scenario] = {
    s.name: s
    for s in [
        Scenario("normal", "Three nodes, normal jitter: no alerts.", 60, _three()),
        Scenario(
            "offline",
            "esp01 loses power at 20 s and comes back at 40 s.",
            60,
            _three(),
            [Fault("offline", "esp01", 20, 40)],
        ),
        Scenario(
            "degrading",
            "esp02 slows to three times its period from 30 s.",
            60,
            _three(),
            [Fault("slow", "esp02", 30)],
        ),
        Scenario(
            "storm",
            "esp01 floods at 2000 messages per second for 2 s from 20 s.",
            40,
            _three(),
            [Fault("flood", "esp01", 20, 22)],
        ),
        Scenario(
            "duplicate",
            "esp03 sends every message twice from 20 s to 25 s.",
            40,
            _three(),
            [Fault("duplicate", "esp03", 20, 25)],
        ),
        Scenario(
            "reboot",
            "esp02 restarts at 20 s.",
            40,
            _three(),
            [Fault("reboot", "esp02", 20)],
        ),
        Scenario(
            "replay",
            "Old esp01 messages from its first boot are replayed after it restarts.",
            40,
            _three(),
            [Fault("reboot", "esp01", 10), Fault("replay", "esp01", 20, 22)],
        ),
    ]
}
