from isrt_sim import payload

KEY = b"test-key-not-secret"


def test_signed_payload_verifies():
    p = payload.make("esp01", "5f3a91c2", 7, 700, KEY, t=23.5)
    assert payload.verify(p, KEY)


def test_changed_field_fails():
    p = payload.make("esp01", "5f3a91c2", 7, 700, KEY, t=23.5)
    p["seq"] = 8
    assert not payload.verify(p, KEY)


def test_other_key_fails():
    p = payload.make("esp01", "5f3a91c2", 7, 700, KEY)
    assert not payload.verify(p, b"another-key")


def test_missing_field_fails():
    p = payload.make("esp01", "5f3a91c2", 7, 700, KEY)
    del p["boot"]
    assert not payload.verify(p, KEY)


def test_signature_is_truncated_hex():
    p = payload.make("esp01", "5f3a91c2", 7, 700, KEY)
    assert len(p["mac"]) == payload.MAC_BYTES * 2
    int(p["mac"], 16)
