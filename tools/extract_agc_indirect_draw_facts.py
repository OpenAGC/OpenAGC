#!/usr/bin/env python3
"""Extract indirect-draw ABI facts from every active libSceAgc profile."""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import subprocess
from pathlib import Path


EXPORTS = {
    "draw": "1q1titRBL6o",
    "draw_get_size": "cxPZ4Wgvdj8",
    "draw_multi": "kUlvghKs-mA",
    "draw_multi_get_size": "pYoKs3lPy88",
    "draw_index": "t1vNu082-jM",
    "draw_index_get_size": "mStuvI0zOtc",
    "draw_index_multi": "ypVBz4uPKcQ",
    "draw_index_multi_get_size": "r98I08t+LOg",
}
USER_DATA_GET_SIZE_NID = "VhLnEiTuuWo"
USER_DATA_IMMEDIATE_PACKET_NID = "tLTma0k0U3E"
SYMBOL_RE = re.compile(
    r"^\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+FUNC\s+\S+\s+\S+\s+\S+\s+(\S+)"
)
INSN_RE = re.compile(r"^\s*([0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2}\s+)+\s*(.*?)\s*$")
CONTROL_RE = re.compile(r"^(callq?|j\w+)\s+.*$")


def rows_without_comments(path: Path):
    with path.open(encoding="utf-8", newline="") as source:
        yield from (line for line in source if not line.startswith("#"))


def load_profiles(path: Path, root: Path) -> list[tuple[str, Path, Path, str]]:
    fields = ("key", "profile", "family", "manifest", "sprx", "status")
    rows = csv.DictReader(rows_without_comments(path), delimiter="\t", fieldnames=fields)
    result = []
    for row in rows:
        driver_relative = Path(row["sprx"])
        agc_relative = driver_relative.with_name("libSceAgc.sprx")
        result.append((row["key"], root / agc_relative,
                       root / driver_relative, str(agc_relative)))
    return result


def run(tool: str, *args: str) -> str:
    return subprocess.run(
        (tool, *args), check=True, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    ).stdout


def load_symbols(readelf: str, sprx: Path, nids: dict[str, str]) -> dict[str, tuple[int, int]]:
    by_nid = {nid: name for name, nid in nids.items()}
    result = {}
    for line in run(readelf, "-Ws", str(sprx)).splitlines():
        match = SYMBOL_RE.match(line)
        if not match:
            continue
        address, size, symbol = match.groups()
        nid = symbol.split("#", 1)[0]
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


def has(body: list[str], pattern: str) -> bool:
    expression = re.compile(pattern)
    return any(expression.search(re.sub(r"\s+", " ", instruction)) for instruction in body)


def require(name: str, body: list[str], patterns: tuple[str, ...]) -> None:
    missing = [pattern for pattern in patterns if not has(body, pattern)]
    if missing:
        raise ValueError(f"{name} semantic pattern changed: {','.join(missing)}")


def validate_single(name: str, body: list[str], header: int, indexed: bool) -> None:
    patterns = (
        rf"\$0x{header:x}",
        r"\$0x509",
        r"\$0x513",
        r"\$0x280",
        r"\$0x20",
        r"(?:\$)?0x14",
    )
    if indexed:
        patterns += (r"\$0x50e",)
    require(name, body, patterns)


def validate_multi(name: str, body: list[str], header: int, pre_tag: int,
                   indexed: bool) -> None:
    patterns = (
        rf"\$0x{header:x}",
        rf"\$0x{pre_tag:x}",
        r"\$0xc6000000",
        r"\$0x509",
        r"\$0x513",
        r"\$0x518",
        r"(?:\$)?0x28",
        r"\$-0x4",
        r"0x10\(%rbp\)",
    )
    if indexed:
        patterns += (r"\$0x50e",)
    require(name, body, patterns)


def validate_get_sizes(bodies: dict[str, list[str]]) -> None:
    require("draw_get_size", bodies["draw_get_size"], (r"\$0x14",))
    require("draw_index_get_size", bodies["draw_index_get_size"], (r"\$0x14",))
    for name in ("draw_multi_get_size", "draw_index_multi_get_size"):
        require(name, bodies[name], (r"(?:\$)?0xa", r"\$0x2"))


def validate_user_data_get_size(body: list[str]) -> None:
    require("user_data_get_size", body,
            (r"(?:\$)?0x3", r"(?:\$)?0x4", r"(?:\$)?0x7"))


def validate_user_data_immediate_packet(body: list[str]) -> None:
    require("user_data_immediate_packet", body, (r"xorl %eax, %eax", r"retq"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_root", type=Path)
    parser.add_argument("--profiles", type=Path,
                        default=Path("analysis/agc_firmware_versions.tsv"))
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    facts = []
    for key, agc_sprx, driver_sprx, relative_path in load_profiles(
            args.profiles, args.firmware_root):
        if not agc_sprx.is_file():
            raise SystemExit(f"missing libSceAgc SPRX for {key}: {agc_sprx}")
        if not driver_sprx.is_file():
            raise SystemExit(f"missing libSceAgcDriver SPRX for {key}: {driver_sprx}")

        symbols = load_symbols(args.readelf, agc_sprx, EXPORTS)
        if symbols.keys() != EXPORTS.keys():
            missing = EXPORTS.keys() - symbols.keys()
            raise SystemExit(f"missing indirect-draw exports for {key}: {','.join(missing)}")
        instructions = disassemble(args.objdump, agc_sprx)
        bodies = {name: body_for(instructions, *symbols[name]) for name in EXPORTS}

        try:
            validate_single("draw", bodies["draw"], 0xC0032400, False)
            validate_single("draw_index", bodies["draw_index"], 0xC0032500, True)
            validate_multi("draw_multi", bodies["draw_multi"],
                           0xC0082C00, 0xC6000008, False)
            validate_multi("draw_index_multi", bodies["draw_index_multi"],
                           0xC0083800, 0xC6000005, True)
            validate_get_sizes(bodies)
        except ValueError as error:
            raise SystemExit(f"{key}: {error}") from error

        driver_exports = {
            "user_data_get_size": USER_DATA_GET_SIZE_NID,
            "user_data_immediate_packet": USER_DATA_IMMEDIATE_PACKET_NID,
        }
        driver_symbols = load_symbols(args.readelf, driver_sprx, driver_exports)
        if driver_symbols.keys() != driver_exports.keys():
            missing = driver_exports.keys() - driver_symbols.keys()
            raise SystemExit(f"missing user-data exports for {key}: {','.join(missing)}")
        driver_instructions = disassemble(args.objdump, driver_sprx)
        user_data_body = body_for(driver_instructions, *driver_symbols["user_data_get_size"])
        immediate_packet_body = body_for(
            driver_instructions, *driver_symbols["user_data_immediate_packet"])
        try:
            validate_user_data_get_size(user_data_body)
            validate_user_data_immediate_packet(immediate_packet_body)
        except ValueError as error:
            raise SystemExit(f"{key}: {error}") from error

        facts.append((
            key, relative_path,
            fingerprint(bodies["draw"]), fingerprint(bodies["draw_index"]),
            fingerprint(bodies["draw_multi"]), fingerprint(bodies["draw_index_multi"]),
            "cb,u32,u64", "5", "20",
            "cb,u32,u32,u32,ptr,u32,u64", "10", "64",
            "pre=tag:8/5,post=tag:0,driver-stub=0+0",
            "count_addr=align4,split32;stride=dword8",
            ("legacy-bits5:7" if has(bodies["draw"], r"\$0xe0000020")
             else "standard"),
        ))

    output = [
        "# firmware_abi_key\tsprx_relative_path\tdraw_fingerprint\t"
        "draw_index_fingerprint\tdraw_multi_fingerprint\t"
        "draw_index_multi_fingerprint\tsingle_signature\tsingle_core_dwords\t"
        "single_get_size_bytes\tmulti_signature\tmulti_core_dwords\t"
        "multi_get_size_bytes\tuser_data_wrappers\tmulti_count_fields"
        "\tinitiator_modifier_family"
    ]
    output.extend("\t".join(row) for row in facts)
    text = "\n".join(output) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
