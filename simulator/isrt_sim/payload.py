"""Node payloads: the same fields and signature the firmware sends.

The signature covers the identity fields only, as one fixed string
``dev|boot|seq|up_ms``. Both sides build that string the same way without a
JSON library, so the firmware and the engine agree byte for byte; readings
are not signed.
"""

from __future__ import annotations

import hashlib
import hmac

SIGNED_FIELDS = ("dev", "boot", "seq", "up_ms")
MAC_BYTES = 16


def signed_text(fields: dict) -> bytes:
    """The bytes the signature covers: the identity fields joined by ``|``."""
    return "|".join(str(fields[k]) for k in SIGNED_FIELDS).encode()


def sign(fields: dict, key: bytes) -> str:
    """Hex HMAC-SHA256 of the signed text, truncated to MAC_BYTES."""
    return hmac.new(key, signed_text(fields), hashlib.sha256).digest()[:MAC_BYTES].hex()


def make(dev: str, boot: str, seq: int, up_ms: int, key: bytes, **readings: float) -> dict:
    """A signed payload with the identity fields plus any readings."""
    fields: dict = {"dev": dev, "boot": boot, "seq": seq, "up_ms": up_ms, **readings}
    fields["mac"] = sign(fields, key)
    return fields


def verify(fields: dict, key: bytes) -> bool:
    """True when every identity field is present and the signature matches."""
    if any(k not in fields for k in SIGNED_FIELDS) or "mac" not in fields:
        return False
    return hmac.compare_digest(str(fields["mac"]), sign(fields, key))
