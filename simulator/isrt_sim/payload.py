"""Node payloads: the same fields and signature the firmware sends."""

from __future__ import annotations

import hashlib
import hmac
import json

SIGNED_FIELDS = ("dev", "boot", "seq", "up_ms")
MAC_BYTES = 16


def canonical(fields: dict) -> bytes:
    """Bytes that the signature covers: every field except ``mac``, sorted, compact JSON."""
    body = {k: v for k, v in fields.items() if k != "mac"}
    return json.dumps(body, sort_keys=True, separators=(",", ":")).encode()


def sign(fields: dict, key: bytes) -> str:
    """Hex HMAC-SHA256 of the canonical bytes, truncated to MAC_BYTES."""
    return hmac.new(key, canonical(fields), hashlib.sha256).digest()[:MAC_BYTES].hex()


def make(dev: str, boot: str, seq: int, up_ms: int, key: bytes, **readings: float) -> dict:
    """A signed payload with the required fields plus any readings."""
    fields: dict = {"dev": dev, "boot": boot, "seq": seq, "up_ms": up_ms, **readings}
    fields["mac"] = sign(fields, key)
    return fields


def verify(fields: dict, key: bytes) -> bool:
    """True when every signed field is present and the signature matches."""
    if any(k not in fields for k in SIGNED_FIELDS) or "mac" not in fields:
        return False
    return hmac.compare_digest(str(fields["mac"]), sign(fields, key))
