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
        "<6I", 1, 32, 0x01020304, 25, 0, 0)
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

    submission = struct.pack("<QIIIIIIQ", 42, 1, 1, 1, 1, 0, 0, 5) + \
        struct.pack("<Q", 1) + struct.pack("<QII", 6, 1, 0) + \
        struct.pack("<QII", 7, 2, 0)
    records.append(record(7, 5, submission))

    validation = struct.pack("<QIIII", 0, 4, 2, 0x80890003, 3) + \
        fixed("agcEndCommandBuffer", 48) + fixed("fixture-command", 64) + \
        fixed("command buffer must be Recording before end", 192)
    records.append(record(9, 6, validation))
    resource = struct.pack("<QIIQII", 2, 2, 1, 4096, 0x30, 2)
    records.append(record(13, 7, resource))
    shader = struct.pack("<Q6I4Q", 3, 6, 0, 0, 24, 0, 1, 128,
                         0x1122334455667788, 0, 0)
    records.append(record(14, 8, shader))
    records.append(record(15, 9, struct.pack("<QII4s", 3, 0, 4, b"SB\0\1")))
    pipeline = struct.pack("<QIIQ6I", 4, 8, 2, 3, 64, 1, 1, 0, 0, 0)
    records.append(record(16, 10, pipeline))
    graphics_pipeline = struct.pack(
        "<QII3Q14I", 8, 7, 2, 0, 9, 10,
        0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0)
    records.append(record(16, 11, graphics_pipeline))
    transition = struct.pack("<3Q8I3Q", 1, 2, 0, 0, 0, 0, 0, 2, 1,
                             0, 0, 128, 256, 0)
    records.append(record(17, 12, transition))
    image_transition = struct.pack(
        "<3Q8I6I", 1, 11, 0, 1, 0, 0, 0, 1, 2, 0, 0,
        1, 1, 2, 3, 4, 0)
    records.append(record(17, 13, image_transition))
    readback = struct.pack("<QIIQQQ", 2, 2, 1, 128, 256,
                           0xCBF29CE484222325)
    records.append(record(10, 14, readback))
    final_size = len(header) + sum(map(len, records)) + 40
    end = struct.pack("<IIQQ", 0, 0, 15, final_size)
    records.append(record(11, 15, end))
    return header + b"".join(records)


def main() -> int:
    decoder = load_decoder()
    fixture = build_fixture()
    first = decoder.decode_capture(fixture)
    second = decoder.decode_capture(fixture)
    assert first == second
    assert "OpenAGC capture v1 runtime_api=25" in first
    assert "profile=fixture-profile firmware=0x11600005 abi=0x1160" in first
    assert "COMMAND_STREAM" in first
    assert "SET_CONTEXT_REG dwords=3" in first
    assert "CB_TARGET_MASK=0x0000000f" in first
    assert "WRITE_DATA dwords=5" in first
    assert "address=<redacted>" in first
    assert "submission=42 queue=compute commands=[1] fence=5" in first
    assert "wait label=6 value=1" in first
    assert "signal label=7 value=2" in first
    assert "function=agcEndCommandBuffer" in first
    assert "command buffer must be Recording before end" in first
    assert "type=buffer v1 size=4096 usage=0x30 flags=0x2" in first
    assert "record_version=24 code_size=128" in first
    assert "shader=3 half=primary bytes=4" in first
    assert "type=compute v2 shader=3 local_size=64x1x1" in first
    assert "type=graphics v2 shaders={hull:0, primitive:9, pixel:10}" in first
    assert "undefined/host -> copy-destination/graphics" in first
    assert "bytes=[128,384)" in first
    assert "resource=11 type=image undefined/host -> copy-source/compute" in first
    assert "aspect=0x1 mips=[1,3) layers=[3,7)" in first
    assert "algorithm=fnv1a64 hash=0xcbf29ce484222325" in first
    assert "records=15" in first
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
