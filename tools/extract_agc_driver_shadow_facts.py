#!/usr/bin/env python3
"""Extract register-shadow constructor facts from AGC driver SPRXs."""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fingerprint_agc_driver_command_carriers import (  # noqa: E402
    IMMEDIATE_RE, carrier_bounds, disassemble, normalize,
)


ADDRESS_HINT = 0xFE0000000
APERTURE_SIZE = 0x200000
MEMORY_TYPE = 0x0C
STANDARD_PROT = 0x33
SHADOW_LAYOUT = (0x8000, 0x19000, 0x21000, 0x19000)
SHADOW_WORDS = (0, 0x3BF, 0x2000, 0x2281, 0x2400, 0x2843)
SHADOW_FINAL_IMMEDIATE = 0x284300002400
TRINITY_NID = "yu17wG8L5FI"
PROPERTY_NAMES = (
    "Sce.Debug:Gn2", "Sce.Debug:Gn3", "Sce.Debug:Gn4",
)
BASE_RANGE_NAMES = (
    "SceAgcRegShadow", "SceAgcGprDumpArea", "SceAgcDdid",
)
GN4_RANGE_NAMES = ("SceAgcRegShadowCopy", "SceAgcRegShadowInfo")


def rows_without_comments(path: Path):
    with path.open(encoding="utf-8", newline="") as source:
        yield from (line for line in source if not line.startswith("#"))


def load_profiles(path: Path, root: Path) -> list[tuple[str, Path]]:
    fields = ("key", "profile", "family", "manifest", "sprx", "status")
    rows = csv.DictReader(rows_without_comments(path), delimiter="\t",
                          fieldnames=fields)
    return [(row["key"], root / row["sprx"]) for row in rows]


def load_segments(data: bytes) -> list[tuple[int, int, int]]:
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("expected little-endian ELF64")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phentsize = struct.unpack_from("<H", data, 54)[0]
    phnum = struct.unpack_from("<H", data, 56)[0]
    segments = []
    for index in range(phnum):
        header = phoff + index * phentsize
        p_type = struct.unpack_from("<I", data, header)[0]
        if p_type != 1:
            continue
        p_offset, p_vaddr = struct.unpack_from("<QQ", data, header + 8)
        p_filesz = struct.unpack_from("<Q", data, header + 32)[0]
        segments.append((p_offset, p_vaddr, p_filesz))
    return segments


def file_offset_to_vaddr(segments: list[tuple[int, int, int]],
                         file_offset: int) -> int:
    for p_offset, p_vaddr, p_filesz in segments:
        if p_offset <= file_offset < p_offset + p_filesz:
            return p_vaddr + file_offset - p_offset
    raise ValueError(f"file offset 0x{file_offset:x} is outside a LOAD segment")


def unique_offset(data: bytes, needle: bytes, label: str) -> int:
    first = data.find(needle)
    if first < 0:
        raise ValueError(f"missing {label}")
    if data.find(needle, first + 1) >= 0:
        raise ValueError(f"multiple {label} values")
    return first


def unique_vaddr(data: bytes, segments: list[tuple[int, int, int]],
                 needle: bytes, label: str) -> tuple[int, int]:
    offset = unique_offset(data, needle, label)
    return offset, file_offset_to_vaddr(segments, offset)


def dynamic_symbols(tool: str, path: Path) -> str:
    return subprocess.run(
        (tool, "--dyn-syms", str(path)), check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    ).stdout


def carrier_for_immediate(instructions: list[tuple[int, str]],
                          immediate: int) -> tuple[int, list[tuple[int, str]]]:
    matches = []
    for index, (_, instruction) in enumerate(instructions):
        values = {int(value, 16) for value in IMMEDIATE_RE.findall(instruction)}
        if immediate in values:
            matches.append(index)
    if len(matches) != 1:
        raise ValueError(
            f"expected one immediate 0x{immediate:x}, found {len(matches)}"
        )
    start, end = carrier_bounds(instructions, matches[0])
    return instructions[start][0], instructions[start:end]


def digest_body(body: list[tuple[int, str]]) -> str:
    normalized = "\n".join(normalize(text) for _, text in body)
    return hashlib.sha256(normalized.encode("utf-8")).hexdigest()[:16]


def callsites_for_string(instructions: list[tuple[int, str]],
                         string_vaddr: int) -> list[tuple[int, int, list[str]]]:
    marker = f"# 0x{string_vaddr:x}"
    candidates = []
    for index, (address, instruction) in enumerate(instructions):
        if marker not in instruction or "%rdi" not in instruction or \
                not instruction.lstrip().startswith("leaq"):
            continue
        body = []
        target = None
        for _, text in instructions[index:index + 16]:
            body.append(text)
            match = re.match(r"^callq?\s+0x([0-9a-fA-F]+)", text)
            if match:
                target = int(match.group(1), 16)
                break
        if target is not None:
            candidates.append((address, target, body))
    if not candidates:
        raise ValueError(
            f"expected a call for string vaddr 0x{string_vaddr:x}"
        )
    return candidates


def inspect_driver(objdump: str, readelf: str, path: Path) -> dict[str, object]:
    data = path.read_bytes()
    segments = load_segments(data)
    instructions = disassemble(objdump, path)
    symbols = dynamic_symbols(readelf, path)

    allocation_address, allocation_body = carrier_for_immediate(
        instructions, ADDRESS_HINT)
    allocation_values = {
        int(value, 16)
        for _, text in allocation_body for value in IMMEDIATE_RE.findall(text)
    }
    required_allocation = {
        ADDRESS_HINT, APERTURE_SIZE, MEMORY_TYPE, STANDARD_PROT,
    }
    if not required_allocation <= allocation_values:
        missing = required_allocation - allocation_values
        raise ValueError(
            "driver-memory carrier missing " +
            ",".join(f"0x{value:x}" for value in sorted(missing))
        )

    layout_bytes = struct.pack("<4I", *SHADOW_LAYOUT)
    layout_offset, layout_vaddr = unique_vaddr(
        data, segments, layout_bytes, "shadow layout")

    leading_words = struct.pack("<4I", *SHADOW_WORDS[:4])
    words_offset, words_vaddr = unique_vaddr(
        data, segments, leading_words, "shadow descriptor leading words")
    marker = f"# 0x{words_vaddr:x}"
    word_loads = [index for index, (_, text) in enumerate(instructions)
                  if marker in text]
    constructor_candidates = {}
    for index in word_loads:
        start, end = carrier_bounds(instructions, index)
        body = instructions[start:end]
        values = {
            int(value, 16)
            for _, text in body for value in IMMEDIATE_RE.findall(text)
        }
        if SHADOW_FINAL_IMMEDIATE in values:
            constructor_candidates[(start, end)] = body
    if len(constructor_candidates) != 1:
        raise ValueError(
            "expected one constructor carrying all shadow words at "
            f"0x{words_vaddr:x}, found {len(constructor_candidates)}"
        )
    constructor_body = next(iter(constructor_candidates.values()))

    property_calls = {}
    property_bodies = []
    for name in PROPERTY_NAMES:
        needle = name.encode("ascii") + b"\0"
        count = data.count(needle)
        if count == 0:
            property_calls[name] = None
            continue
        _, name_vaddr = unique_vaddr(data, segments, needle, name)
        name_calls = callsites_for_string(instructions, name_vaddr)
        property_calls[name] = name_calls
        for _, _, body in name_calls:
            property_bodies.extend(normalize(text) for text in body)

    for name in BASE_RANGE_NAMES:
        if data.count(name.encode("ascii") + b"\0") != 1:
            raise ValueError(f"expected one {name} range name")
    has_gn4 = property_calls["Sce.Debug:Gn4"] is not None
    for name in GN4_RANGE_NAMES:
        expected = 1 if has_gn4 else 0
        if data.count(name.encode("ascii") + b"\0") != expected:
            raise ValueError(
                f"expected {expected} {name} range names for Gn4={has_gn4}"
            )

    present_targets = {
        target for calls in property_calls.values() if calls is not None
        for _, target, _ in calls
    }
    if len(present_targets) != 1:
        raise ValueError("shadow properties use different PLT targets")

    return {
        "allocation_address": allocation_address,
        "allocation_digest": digest_body(allocation_body),
        "allocation_count": len(allocation_body),
        "layout_offset": layout_offset,
        "layout_vaddr": layout_vaddr,
        "words_offset": words_offset,
        "words_vaddr": words_vaddr,
        "constructor_address": constructor_body[0][0],
        "constructor_digest": digest_body(constructor_body),
        "constructor_count": len(constructor_body),
        "property_calls": property_calls,
        "property_target": present_targets.pop(),
        "property_digest": hashlib.sha256(
            "\n".join(property_bodies).encode("utf-8")
        ).hexdigest()[:16],
        "trinity": re.search(rf"\b{re.escape(TRINITY_NID)}#", symbols)
                   is not None,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_root", type=Path)
    parser.add_argument("--profiles", type=Path,
        default=Path("analysis/agc_firmware_versions.tsv"))
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    columns = (
        "firmware_abi_key", "allocation_carrier", "allocation_insns",
        "allocation_vaddr", "address_hint", "aperture_size", "alignment",
        "memory_type", "standard_prot", "layout_file_offset", "layout_vaddr",
        "first_offset", "first_size", "second_offset", "second_size",
        "words_file_offset", "words_vaddr", "descriptor_words",
        "constructor_carrier", "constructor_insns", "constructor_vaddr",
        "property_carrier", "property_plt", "gn2_call", "gn3_call",
        "gn4_call", "trinity_predicate", "qualification",
    )
    with args.output.open("w", encoding="utf-8", newline="") as output:
        output.write("# " + "\t".join(columns) + "\n")
        writer = csv.DictWriter(output, fieldnames=columns, delimiter="\t",
                                lineterminator="\n")
        for key, sprx in load_profiles(args.profiles, args.firmware_root):
            if not sprx.is_file():
                raise SystemExit(f"missing SPRX for {key}: {sprx}")
            try:
                facts = inspect_driver(args.objdump, args.readelf, sprx)
            except ValueError as error:
                raise SystemExit(f"{key}: {error}") from error
            calls = facts["property_calls"]
            writer.writerow({
                "firmware_abi_key": key,
                "allocation_carrier": facts["allocation_digest"],
                "allocation_insns": facts["allocation_count"],
                "allocation_vaddr": f"0x{facts['allocation_address']:x}",
                "address_hint": f"0x{ADDRESS_HINT:x}",
                "aperture_size": f"0x{APERTURE_SIZE:x}",
                "alignment": f"0x{APERTURE_SIZE:x}",
                "memory_type": f"0x{MEMORY_TYPE:x}",
                "standard_prot": f"0x{STANDARD_PROT:x}",
                "layout_file_offset": f"0x{facts['layout_offset']:x}",
                "layout_vaddr": f"0x{facts['layout_vaddr']:x}",
                "first_offset": f"0x{SHADOW_LAYOUT[0]:x}",
                "first_size": f"0x{SHADOW_LAYOUT[1]:x}",
                "second_offset": f"0x{SHADOW_LAYOUT[2]:x}",
                "second_size": f"0x{SHADOW_LAYOUT[3]:x}",
                "words_file_offset": f"0x{facts['words_offset']:x}",
                "words_vaddr": f"0x{facts['words_vaddr']:x}",
                "descriptor_words": ",".join(
                    f"0x{value:x}" for value in SHADOW_WORDS),
                "constructor_carrier": facts["constructor_digest"],
                "constructor_insns": facts["constructor_count"],
                "constructor_vaddr": f"0x{facts['constructor_address']:x}",
                "property_carrier": facts["property_digest"],
                "property_plt": f"0x{facts['property_target']:x}",
                "gn2_call": (",".join(f"0x{call[0]:x}"
                                      for call in calls["Sce.Debug:Gn2"])
                             if calls["Sce.Debug:Gn2"] else "-"),
                "gn3_call": (",".join(f"0x{call[0]:x}"
                                      for call in calls["Sce.Debug:Gn3"])
                             if calls["Sce.Debug:Gn3"] else "-"),
                "gn4_call": (",".join(f"0x{call[0]:x}"
                                      for call in calls["Sce.Debug:Gn4"])
                             if calls["Sce.Debug:Gn4"] else "-"),
                "trinity_predicate": TRINITY_NID if facts["trinity"] else "-",
                "qualification": ("FW5.50-workload-qualified-without-replay"
                                  if key == "0x0550"
                                  else "exact-RE-hardware-pending"),
            })
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
