#!/usr/bin/env python3
"""Extract Sony workload packet-builder facts for active AGC drivers."""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import subprocess
from pathlib import Path


NIDS = {
    "active_size": "gyVTZWyySpM",
    "complete_size": "WNyjOWq8-Vk",
    "active": "UM9b9NunSrE",
    "complete": "i6bfTi13ApA",
}

SYMBOL_RE = re.compile(
    r"^\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+FUNC\s+\S+\s+\S+\s+\S+\s+(\S+)"
)
INSN_RE = re.compile(r"^\s*([0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2}\s+)+\s*(.*?)\s*$")
HEX_RE = re.compile(r"(?<![0-9a-fA-F])(-?0x[0-9a-fA-F]+)")
CALL_RE = re.compile(r"^callq?\s+0x([0-9a-fA-F]+)")
CONTROL_RE = re.compile(r"^(callq?|j\w+)\s+.*$")

PACKET_HEADER = 0xC0071E00
PACKET_DWORDS = ((PACKET_HEADER >> 16) & 0x3FFF) + 2


def rows_without_comments(path: Path):
    with path.open(encoding="utf-8", newline="") as source:
        yield from (line for line in source if not line.startswith("#"))


def load_profiles(path: Path, root: Path) -> list[tuple[str, Path, str]]:
    fields = ("key", "profile", "family", "manifest", "sprx", "status")
    rows = csv.DictReader(rows_without_comments(path), delimiter="\t", fieldnames=fields)
    return [(row["key"], root / row["sprx"], row["sprx"]) for row in rows]


def run(tool: str, *args: str) -> str:
    return subprocess.run(
        (tool, *args), check=True, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    ).stdout


def load_symbols(readelf: str, sprx: Path) -> dict[str, tuple[int, int]]:
    by_nid = {nid: operation for operation, nid in NIDS.items()}
    result: dict[str, tuple[int, int]] = {}
    for line in run(readelf, "-Ws", str(sprx)).splitlines():
        match = SYMBOL_RE.match(line)
        if not match:
            continue
        address, size, nid = match.groups()
        nid = nid.split("#", 1)[0]
        if nid in by_nid and int(size) > 0:
            result[by_nid[nid]] = (int(address, 16), int(size))
    return result


def disassemble(objdump: str, sprx: Path) -> list[tuple[int, str]]:
    result = []
    for line in run(objdump, "-d", str(sprx)).splitlines():
        match = INSN_RE.match(line)
        if match:
            result.append((int(match.group(1), 16), match.group(2).split("#", 1)[0].strip()))
    return result


def body_for(instructions: list[tuple[int, str]], address: int, size: int) -> list[str]:
    return [text for pc, text in instructions if address <= pc < address + size]


def normalize(instruction: str) -> str:
    instruction = re.sub(r"[-+]?0x[0-9a-fA-F]+\(%rip\)", "<rip>(%rip)", instruction)
    if CONTROL_RE.match(instruction):
        return f"{instruction.split(None, 1)[0]} <target>"
    return re.sub(r"\s+", " ", instruction)


def fingerprint(body: list[str]) -> str:
    normalized = "\n".join(normalize(instruction) for instruction in body)
    return hashlib.sha256(normalized.encode("utf-8")).hexdigest()[:16]


def immediates(body: list[str]) -> set[int]:
    return {int(value, 0) & 0xFFFFFFFF for instruction in body
            for value in HEX_RE.findall(instruction)}


def sole_call_target(body: list[str]) -> int:
    targets = []
    for instruction in body:
        match = CALL_RE.match(instruction)
        if match:
            targets.append(int(match.group(1), 16))
    if len(targets) != 1:
        raise ValueError("packet-size wrapper does not have one helper call")
    return targets[0]


def helper_body(instructions: list[tuple[int, str]], address: int) -> list[str]:
    body = []
    started = False
    for pc, instruction in instructions:
        if pc == address:
            started = True
        if not started:
            continue
        if instruction.split(None, 1)[0] == "int3":
            break
        body.append(instruction)
    return body


def validate_size_helper(body: list[str]) -> None:
    normalized = [re.sub(r"\s+", " ", instruction) for instruction in body]
    required = {
        "testl %edi, %edi", "movl %edi, %eax", "addq $0x3, %rax",
        "shrq $0x2, %rax", "leal 0x7(%rax), %ecx",
        "movl $0x4, %eax", "cmovnel %ecx, %eax", "movl $0x3, %eax",
    }
    has_one_compare = any(re.match(r"^cmp[ql] \$0x1, %[re]ax$", item)
                          for item in normalized)
    if (not required.issubset(normalized) or not has_one_compare or
            normalized.count("retq") != 2):
        raise ValueError("packet-size helper formula changed")


def prefix_dwords(payload_bytes: int) -> int:
    if payload_bytes == 0:
        return 3
    rounded_dwords = (payload_bytes + 3) >> 2
    return 4 if rounded_dwords == 1 else rounded_dwords + 7


def validate_builders(active: list[str], complete: list[str]) -> None:
    active_required = {
        PACKET_HEADER, 0xCC000000, 0x267, 0x3F, 0x8A6C0033,
        0x8A6C0034, 0x8A6C003A, 0x8A6C003B,
    }
    complete_required = {
        PACKET_HEADER, 0xCD000000, 0x275, 0x3F, 0x8A6C0033,
        0x8A6C0034, 0x8A6C003A, 0x8A6C003B,
    }
    active_missing = active_required - immediates(active)
    complete_missing = complete_required - immediates(complete)
    if active_missing:
        raise ValueError("active builder constants changed; missing " +
                         ",".join(f"0x{value:x}" for value in sorted(active_missing)))
    if complete_missing:
        raise ValueError("complete builder constants changed; missing " +
                         ",".join(f"0x{value:x}" for value in sorted(complete_missing)))
    for name, body in (("active", active), ("complete", complete)):
        values = immediates(body)
        if 0x1E not in values and not {0x20, 0xFFFFFFE1}.issubset(values):
            raise ValueError(f"{name} stream-range validation changed")
    if sum(PACKET_HEADER in immediates([instruction]) for instruction in active) != 1:
        raise ValueError("active builder packet-header count changed")
    if sum(PACKET_HEADER in immediates([instruction]) for instruction in complete) != 1:
        raise ValueError("complete builder packet-header count changed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_root", type=Path)
    parser.add_argument("--profiles", type=Path,
        default=Path("analysis/agc_firmware_versions.tsv"))
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    if PACKET_DWORDS != 9:
        raise SystemExit("unexpected Sony workload packet length")
    rows = []
    for key, sprx, relative_path in load_profiles(args.profiles, args.firmware_root):
        if not sprx.is_file():
            raise SystemExit(f"missing SPRX for {key}: {sprx}")
        found = load_symbols(args.readelf, sprx)
        if found.keys() != NIDS.keys():
            missing = NIDS.keys() - found.keys()
            raise SystemExit(f"missing workload exports for {key}: {','.join(missing)}")
        instructions = disassemble(args.objdump, sprx)
        bodies = {name: body_for(instructions, *found[name]) for name in NIDS}
        try:
            active_target = sole_call_target(bodies["active_size"])
            complete_target = sole_call_target(bodies["complete_size"])
            if active_target != complete_target:
                raise ValueError("size exports call different helpers")
            size_helper = helper_body(instructions, active_target)
            validate_size_helper(size_helper)
            validate_builders(bodies["active"], bodies["complete"])
            if not {0x8, 0x9}.issubset(immediates(bodies["active_size"])):
                raise ValueError("active maximum-size formula inputs changed")
            if 0x9 not in immediates(bodies["complete_size"]):
                raise ValueError("complete maximum-size packet addition changed")
        except ValueError as error:
            raise SystemExit(f"{key}: {error}") from error

        status = ("hardware-qualified-openagc-extension-only" if key == "0x0550"
                  else "sony-abi-not-adapted")
        active_max = prefix_dwords(8) + PACKET_DWORDS
        complete_max = prefix_dwords(0) + PACKET_DWORDS
        if active_max != 18 or complete_max != 12:
            raise SystemExit(f"{key}: workload maximum-size derivation changed")
        rows.append((
            key, relative_path,
            fingerprint(bodies["active_size"]), str(found["active_size"][1]),
            fingerprint(bodies["complete_size"]), str(found["complete_size"][1]),
            fingerprint(size_helper), str(active_max), str(complete_max),
            fingerprint(bodies["active"]), str(found["active"][1]),
            fingerprint(bodies["complete"]), str(found["complete"][1]),
            f"0x{PACKET_HEADER:08x}", str(PACKET_DWORDS), "1", "31", "63",
            "1", "63", "0xcc000000", "0xcd000000", "0x267", "0x275", status,
        ))

    header = (
        "# firmware_abi_key\tsprx_relative_path\tactive_size_fingerprint\t"
        "active_size_bytes\tcomplete_size_fingerprint\tcomplete_size_bytes\t"
        "size_helper_fingerprint\tactive_max_dwords\tcomplete_max_dwords\t"
        "active_builder_fingerprint\tactive_builder_bytes\t"
        "complete_builder_fingerprint\tcomplete_builder_bytes\tpacket_header\t"
        "packet_dwords\tstream_min\tstream_max\tworkload_id_max\t"
        "active_count_min\tactive_count_max\tactive_prefix_control\t"
        "complete_prefix_control\tactive_packet_control\tcomplete_packet_control\t"
        "openagc_status"
    )
    text = header + "\n" + "\n".join("\t".join(row) for row in rows) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
