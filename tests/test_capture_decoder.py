#!/usr/bin/env python3
"""Deterministic host-decoder fixture for OpenAGC capture v1."""

from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path


def load_decoder():
    source = Path(__file__).resolve().parents[1] / "tools" / \
        "decode_openagc_capture.py"
    spec = importlib.util.spec_from_file_location("openagc_capture_decoder", source)
    if spec is None or spec.loader is None:
        raise RuntimeError("unable to load capture decoder")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fixed(text: str, size: int) -> bytes:
    encoded = text.encode("utf-8")
    if len(encoded) >= size:
        encoded = encoded[:size - 1]
    return encoded + bytes(size - len(encoded))


def record(record_type: int, sequence: int, payload: bytes) -> bytes:
    size = 16 + len(payload)
    return struct.pack("<HHIQ", record_type, 1, size, sequence) + payload


def build_fixture() -> bytes:
    header = b"OAGCCAP\0" + struct.pack(
        "<6I", 1, 32, 0x01020304, 24, 0, 0)
    records: list[bytes] = []
    runtime = struct.pack("<IHHIQ", 0x11600005, 0x1160, 1, 12,
                          0xFF) + bytes([3] * 8) + fixed("fixture-profile", 48)
    records.append(record(1, 1, runtime))
    records.append(record(2, 2, struct.pack("<QIIII", 1, 9, 0, 1, 64)))
    records.append(record(3, 3, struct.pack("<QI", 1, 9) +
                          fixed("fixture-command", 64)))

    set_context = (0xC0016900, 0x08E, 0x0000000F)
    write_data = (0xC0033700, 0, 0x12345000, 0x0000000F, 0xA5A5A5A5)
    words = set_context + write_data
    command = struct.pack("<QII", 1, 0, len(words)) + struct.pack(
        f"<{len(words)}I", *words)
    records.append(record(12, 4, command))

    validation = struct.pack("<QIIII", 0, 4, 2, 0x80890003, 3) + \
        fixed("agcEndCommandBuffer", 48) + fixed("fixture-command", 64) + \
        fixed("command buffer must be Recording before end", 192)
    records.append(record(9, 5, validation))
    final_size = len(header) + sum(map(len, records)) + 40
    end = struct.pack("<IIQQ", 0, 0, 6, final_size)
    records.append(record(11, 6, end))
    return header + b"".join(records)


def main() -> int:
    decoder = load_decoder()
    fixture = build_fixture()
    first = decoder.decode_capture(fixture)
    second = decoder.decode_capture(fixture)
    assert first == second
    assert "OpenAGC capture v1 runtime_api=24" in first
    assert "profile=fixture-profile firmware=0x11600005 abi=0x1160" in first
    assert "COMMAND_STREAM" in first
    assert "SET_CONTEXT_REG dwords=3" in first
    assert "CB_TARGET_MASK=0x0000000f" in first
    assert "WRITE_DATA dwords=5" in first
    assert "address=<redacted>" in first
    assert "function=agcEndCommandBuffer" in first
    assert "command buffer must be Recording before end" in first
    assert "records=6" in first
    shown = decoder.decode_capture(fixture, show_addresses=True)
    assert "address=0x0000000f12345000" in shown
    try:
        decoder.decode_capture(b"not a capture")
    except decoder.CaptureError:
        pass
    else:
        raise AssertionError("malformed capture was accepted")
    print("capture decoder fixture: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
