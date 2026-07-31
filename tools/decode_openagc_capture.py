#!/usr/bin/env python3
"""Decode OpenAGC native-runtime capture v1 without replaying it."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

MAGIC = b"OAGCCAP\0"
FILE_HEADER_SIZE = 32
RECORD_HEADER_SIZE = 16
ENDIAN_TAG = 0x01020304

RECORD_NAMES = {
    1: "RUNTIME_INFO",
    2: "OBJECT_CREATE",
    3: "OBJECT_NAME",
    4: "OBJECT_DESTROY",
    5: "COMMAND_BEGIN",
    6: "COMMAND_END",
    7: "SUBMISSION",
    8: "FENCE_RESULT",
    9: "VALIDATION_MESSAGE",
    10: "READBACK_HASH",
    11: "END",
    12: "COMMAND_STREAM",
}

OBJECT_NAMES = {
    0: "device",
    1: "queue",
    2: "buffer",
    3: "image",
    4: "image-view",
    5: "sampler",
    6: "shader",
    7: "graphics-pipeline",
    8: "compute-pipeline",
    9: "command-buffer",
    10: "fence",
    11: "gpu-label",
    12: "present-chain",
}

QUEUE_NAMES = {0: "graphics", 1: "compute"}

OPCODE_NAMES = {
    0x10: "NOP",
    0x11: "SET_BASE",
    0x13: "INDEX_BUFFER_SIZE",
    0x15: "DISPATCH_DIRECT",
    0x16: "DISPATCH_INDIRECT",
    0x24: "DRAW_INDIRECT",
    0x25: "DRAW_INDEX_INDIRECT",
    0x26: "INDEX_BASE",
    0x27: "DRAW_INDEX_2",
    0x28: "CONTEXT_CONTROL",
    0x2A: "INDEX_TYPE",
    0x2D: "DRAW_INDEX_AUTO",
    0x2F: "NUM_INSTANCES",
    0x37: "WRITE_DATA",
    0x3C: "WAIT_REG_MEM",
    0x40: "COPY_DATA",
    0x46: "EVENT_WRITE",
    0x47: "EVENT_WRITE_EOP",
    0x49: "RELEASE_MEM",
    0x50: "DMA_DATA",
    0x58: "ACQUIRE_MEM",
    0x68: "SET_CONFIG_REG",
    0x69: "SET_CONTEXT_REG",
    0x76: "SET_SH_REG",
    0x77: "SET_SH_REG_OFFSET",
    0x79: "SET_UCONFIG_REG",
}

CONTEXT_REGISTERS = {
    0x000: "DB_RENDER_CONTROL",
    0x002: "DB_DEPTH_VIEW",
    0x007: "DB_DEPTH_SIZE_XY",
    0x00F: "DB_DEPTH_INFO",
    0x010: "DB_Z_INFO",
    0x011: "DB_STENCIL_INFO",
    0x080: "PA_SC_WINDOW_OFFSET",
    0x081: "PA_SC_WINDOW_SCISSOR_TL",
    0x082: "PA_SC_WINDOW_SCISSOR_BR",
    0x00C: "PA_SC_SCREEN_SCISSOR_TL",
    0x00D: "PA_SC_SCREEN_SCISSOR_BR",
    0x08E: "CB_TARGET_MASK",
    0x08F: "CB_SHADER_MASK",
    0x201: "DB_EQAA",
}

SH_REGISTERS = {
    0x207: "COMPUTE_NUM_THREAD_X",
    0x208: "COMPUTE_NUM_THREAD_Y",
    0x209: "COMPUTE_NUM_THREAD_Z",
    0x20C: "COMPUTE_PGM_LO",
    0x20D: "COMPUTE_PGM_HI",
    0x212: "COMPUTE_PGM_RSRC1",
    0x213: "COMPUTE_PGM_RSRC2",
    0x215: "COMPUTE_RESOURCE_LIMITS",
    0x228: "COMPUTE_PGM_RSRC3",
    0x240: "COMPUTE_USER_DATA_0",
}

UCONFIG_REGISTERS = {
    0x242: "VGT_PRIMITIVE_TYPE",
}


class CaptureError(ValueError):
    """Malformed or unsupported capture."""


def c_string(data: bytes) -> str:
    return data.split(b"\0", 1)[0].decode("utf-8", "replace")


def unpack_from(fmt: str, data: bytes, offset: int = 0) -> tuple[int, ...]:
    size = struct.calcsize(fmt)
    if offset < 0 or size > len(data) - offset:
        raise CaptureError("truncated record payload")
    return struct.unpack_from(fmt, data, offset)


def format_value(register: str | None, value: int, show_addresses: bool) -> str:
    if show_addresses:
        return f"0x{value:08x}"
    if register is None or any(token in register for token in
            ("BASE", "ADDR", "PGM_LO", "PGM_HI", "USER_DATA")):
        return "<redacted>"
    return f"0x{value:08x}"


def decode_pm4(words: tuple[int, ...], show_addresses: bool) -> list[str]:
    lines: list[str] = []
    cursor = 0
    packet_index = 0
    while cursor < len(words):
        header = words[cursor]
        packet_type = header >> 30
        if packet_type != 3:
            lines.append(
                f"    packet[{packet_index}] INVALID_TYPE type={packet_type}")
            break
        length = ((header >> 16) & 0x3FFF) + 2
        opcode = (header >> 8) & 0xFF
        name = OPCODE_NAMES.get(opcode, f"OP_0x{opcode:02x}")
        if length < 2 or length > len(words) - cursor:
            lines.append(
                f"    packet[{packet_index}] {name} malformed-length={length}")
            break
        payload = words[cursor + 1:cursor + length]
        lines.append(
            f"    packet[{packet_index}] {name} dwords={length}")
        if opcode in (0x69, 0x76, 0x79) and payload:
            register_maps = {
                0x69: CONTEXT_REGISTERS,
                0x76: SH_REGISTERS,
                0x79: UCONFIG_REGISTERS,
            }
            space = {0x69: "CX", 0x76: "SH", 0x79: "UC"}[opcode]
            first = payload[0] & 0xFFFF
            for index, value in enumerate(payload[1:]):
                offset = first + index
                register = register_maps[opcode].get(offset)
                register_name = register or f"UNKNOWN_0x{offset:03x}"
                shown = format_value(register, value, show_addresses)
                lines.append(
                    f"      {space}[0x{offset:03x}] {register_name}={shown}")
        elif opcode == 0x15 and len(payload) >= 4:
            lines.append(
                f"      groups={payload[0]}x{payload[1]}x{payload[2]} "
                f"initiator=0x{payload[3]:08x}")
        elif opcode == 0x3C and len(payload) >= 6:
            address = (payload[2] << 32) | payload[1]
            shown_address = f"0x{address:016x}" if show_addresses else "<redacted>"
            lines.append(
                f"      control=0x{payload[0]:08x} address={shown_address} "
                f"reference=0x{payload[3]:08x} mask=0x{payload[4]:08x}")
        elif opcode == 0x37 and len(payload) >= 3:
            address = (payload[2] << 32) | payload[1]
            shown_address = f"0x{address:016x}" if show_addresses else "<redacted>"
            lines.append(
                f"      control=0x{payload[0]:08x} address={shown_address}")
        cursor += length
        packet_index += 1
    if cursor != len(words):
        lines.append(f"    warning: {len(words) - cursor} undecoded dwords")
    return lines


def decode_record(record_type: int, payload: bytes,
                  show_addresses: bool) -> list[str]:
    lines: list[str] = []
    if record_type == 1:
        firmware, abi, family, agc = unpack_from("<IHHI", payload)
        capabilities, = unpack_from("<Q", payload, 12)
        profile = c_string(payload[28:76])
        lines.append(
            f"  profile={profile} firmware=0x{firmware:08x} "
            f"abi=0x{abi:04x} family={family} agc={agc} "
            f"capabilities=0x{capabilities:016x}")
    elif record_type == 2:
        object_id, object_type, _, detail0, detail1 = unpack_from(
            "<QIIII", payload)
        lines.append(
            f"  id={object_id} type={OBJECT_NAMES.get(object_type, object_type)} "
            f"detail0={detail0} detail1={detail1}")
    elif record_type == 3:
        object_id, object_type = unpack_from("<QI", payload)
        lines.append(
            f"  id={object_id} type={OBJECT_NAMES.get(object_type, object_type)} "
            f"name={c_string(payload[12:76])!r}")
    elif record_type == 4:
        object_id, object_type, _ = unpack_from("<QII", payload)
        lines.append(
            f"  id={object_id} type={OBJECT_NAMES.get(object_type, object_type)}")
    elif record_type == 5:
        object_id, queue_type, capacity = unpack_from("<QII", payload)
        lines.append(
            f"  command={object_id} queue={QUEUE_NAMES.get(queue_type, queue_type)} "
            f"capacity_dwords={capacity}")
    elif record_type in (6, 12):
        object_id, queue_type, count = unpack_from("<QII", payload)
        expected = 16 + count * 4
        if len(payload) != expected:
            raise CaptureError("command record dword count does not match size")
        words = struct.unpack_from(f"<{count}I", payload, 16) if count else ()
        lines.append(
            f"  command={object_id} queue={QUEUE_NAMES.get(queue_type, queue_type)} "
            f"dwords={count}")
        lines.extend(decode_pm4(words, show_addresses))
    elif record_type == 7:
        submission, queue_type, command_count, waits, signals, result, _, fence = \
            unpack_from("<QIIIIIIQ", payload)
        cursor = 40
        commands = []
        for _ in range(command_count):
            command, = unpack_from("<Q", payload, cursor)
            commands.append(command)
            cursor += 8
        lines.append(
            f"  submission={submission} queue={QUEUE_NAMES.get(queue_type, queue_type)} "
            f"commands={commands} fence={fence} result=0x{result:08x}")
        for kind, count in (("wait", waits), ("signal", signals)):
            for _ in range(count):
                label, value, _reserved = unpack_from("<QII", payload, cursor)
                cursor += 16
                lines.append(f"    {kind} label={label} value={value}")
        if cursor != len(payload):
            raise CaptureError("submission record has trailing bytes")
    elif record_type == 8:
        fence, submission, result, operation, timeout = unpack_from(
            "<QQIIQ", payload)
        lines.append(
            f"  fence={fence} submission={submission} operation={operation} "
            f"result=0x{result:08x} timeout_ns={timeout}")
    elif record_type == 9:
        debug_sequence, severity, category, result, object_type = unpack_from(
            "<QIIII", payload)
        function = c_string(payload[24:72])
        object_name = c_string(payload[72:136])
        message = c_string(payload[136:328])
        lines.append(
            f"  warning severity=0x{severity:x} category=0x{category:x} "
            f"result=0x{result:08x} function={function} "
            f"object_type={object_type} object_name={object_name!r}")
        lines.append(f"    {message} debug_sequence={debug_sequence}")
    elif record_type == 11:
        status, _, records, byte_count = unpack_from("<IIQQ", payload)
        lines.append(
            f"  status=0x{status:08x} records={records} bytes={byte_count}")
    else:
        lines.append(f"  payload_bytes={len(payload)}")
    return lines


def decode_capture(data: bytes, show_addresses: bool = False) -> str:
    if len(data) < FILE_HEADER_SIZE:
        raise CaptureError("capture is smaller than the file header")
    if data[:8] != MAGIC:
        raise CaptureError("invalid OpenAGC capture magic")
    version, header_size, endian_tag, runtime_api, flags, reserved = \
        struct.unpack_from("<6I", data, 8)
    if version != 1 or header_size != FILE_HEADER_SIZE:
        raise CaptureError(
            f"unsupported capture header version={version} size={header_size}")
    if endian_tag != ENDIAN_TAG or reserved != 0:
        raise CaptureError("invalid endian tag or reserved header field")
    lines = [
        f"OpenAGC capture v{version} runtime_api={runtime_api} "
        f"flags=0x{flags:08x} addresses={'shown' if show_addresses else 'redacted'}"
    ]
    offset = header_size
    expected_sequence = 1
    while offset < len(data):
        if len(data) - offset < RECORD_HEADER_SIZE:
            raise CaptureError("truncated record header")
        record_type, record_version, record_size, sequence = \
            struct.unpack_from("<HHIQ", data, offset)
        if record_version != 1:
            raise CaptureError(f"unsupported record version {record_version}")
        if record_size < RECORD_HEADER_SIZE or record_size > len(data) - offset:
            raise CaptureError("invalid record size")
        if sequence != expected_sequence:
            raise CaptureError(
                f"non-contiguous sequence {sequence}, expected {expected_sequence}")
        name = RECORD_NAMES.get(record_type, f"UNKNOWN_{record_type}")
        lines.append(f"[{sequence}] {name} size={record_size}")
        payload = data[offset + RECORD_HEADER_SIZE:offset + record_size]
        lines.extend(decode_record(record_type, payload, show_addresses))
        expected_sequence += 1
        offset += record_size
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--show-addresses", action="store_true",
                        help="show process-specific addresses instead of redacting")
    args = parser.parse_args()
    try:
        output = decode_capture(args.capture.read_bytes(), args.show_addresses)
    except (OSError, CaptureError) as error:
        print(f"decode_openagc_capture: {error}", file=sys.stderr)
        return 1
    sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
