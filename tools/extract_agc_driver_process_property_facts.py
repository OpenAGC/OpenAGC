#!/usr/bin/env python3
"""Extract the GPU-info process-property call contract from AGC drivers."""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import struct
import subprocess
from pathlib import Path


PROPERTY_NAME = b"Sce.Debug:Gnm\0"
PROPERTY_NID = "-W4xI5aVI8w"
TRINITY_NID = "yu17wG8L5FI"
INSN_RE = re.compile(
    r"^\s*([0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2}\s+)+\s*(.*?)\s*$"
)
CALL_RE = re.compile(r"^callq?\s+0x([0-9a-fA-F]+)")


def rows_without_comments(path: Path):
    with path.open(encoding="utf-8", newline="") as source:
        yield from (line for line in source if not line.startswith("#"))


def load_profiles(path: Path, root: Path) -> list[tuple[str, Path]]:
    fields = ("key", "profile", "family", "manifest", "sprx", "status")
    rows = csv.DictReader(rows_without_comments(path), delimiter="\t",
                          fieldnames=fields)
    return [(row["key"], root / row["sprx"]) for row in rows]


def virtual_address_of_bytes(path: Path, needle: bytes) -> int:
    data = path.read_bytes()
    offsets = []
    cursor = 0
    while True:
        found = data.find(needle, cursor)
        if found < 0:
            break
        offsets.append(found)
        cursor = found + 1
    if len(offsets) != 1:
        raise ValueError(f"expected one {needle!r} string, found {len(offsets)}")

    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("expected little-endian ELF64")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phentsize = struct.unpack_from("<H", data, 54)[0]
    phnum = struct.unpack_from("<H", data, 56)[0]
    file_offset = offsets[0]
    for index in range(phnum):
        header = phoff + index * phentsize
        p_type = struct.unpack_from("<I", data, header)[0]
        p_offset, p_vaddr = struct.unpack_from("<QQ", data, header + 8)
        p_filesz = struct.unpack_from("<Q", data, header + 32)[0]
        if p_type == 1 and p_offset <= file_offset < p_offset + p_filesz:
            return p_vaddr + file_offset - p_offset
    raise ValueError("property string is outside a loadable segment")


def disassemble(tool: str, path: Path) -> list[tuple[int, str]]:
    output = subprocess.run(
        (tool, "-d", str(path)), check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    ).stdout
    instructions = []
    for line in output.splitlines():
        match = INSN_RE.match(line)
        if match:
            instructions.append((int(match.group(1), 16), match.group(2)))
    return instructions


def dynamic_symbols(tool: str, path: Path) -> str:
    return subprocess.run(
        (tool, "--dyn-syms", str(path)), check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    ).stdout


def normalize(instruction: str) -> str:
    instruction = instruction.split("#", 1)[0].strip()
    instruction = re.sub(r"[-+]?0x[0-9a-fA-F]+\(%rip\)",
                         "<rip>(%rip)", instruction)
    if instruction.startswith("call"):
        return "callq <property>"
    return re.sub(r"\s+", " ", instruction)


def property_calls(instructions: list[tuple[int, str]], string_vaddr: int,
                   trinity: bool) -> tuple[list[int], int, str]:
    marker = f"# 0x{string_vaddr:x}"
    calls = []
    targets = set()
    normalized_bodies = []
    for index, (address, instruction) in enumerate(instructions):
        if marker not in instruction or "%rdi" not in instruction or \
                not instruction.lstrip().startswith("leaq"):
            continue
        body = []
        target = None
        for _, candidate in instructions[index:index + 14]:
            body.append(candidate)
            match = CALL_RE.match(candidate)
            if match:
                target = int(match.group(1), 16)
                break
        if target is None:
            raise ValueError(f"property string at 0x{address:x} has no call")
        text = "\n".join(body)
        required = ("%rsi", "xorl\t%ecx, %ecx", "xorl\t%r8d, %r8d")
        if not all(item in text for item in required):
            raise ValueError(f"property argument registers changed at 0x{address:x}")
        if trinity:
            size_forms = (
                ("setne\t%dl", "shll\t$0x13, %edx",
                 "orq\t$0x100000, %rdx"),
                ("setne\t%dl", "orq\t$0x2, %rdx",
                 "shlq\t$0x13, %rdx"),
            )
        else:
            size_forms = (("movl\t$0x100000, %edx",),)
        if not any(all(item in text for item in form) for form in size_forms):
            raise ValueError(f"property span derivation changed at 0x{address:x}")
        calls.append(address)
        targets.add(target)
        normalized_bodies.append("\n".join(normalize(item) for item in body))
    if not calls:
        raise ValueError("no process-property call sites found")
    if len(targets) != 1:
        raise ValueError("process-property call sites use different PLT targets")
    digest = hashlib.sha256("\n---\n".join(normalized_bodies).encode()).hexdigest()[:16]
    return calls, targets.pop(), digest


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
        "firmware_abi_key", "carrier_fingerprint", "call_count",
        "callsites", "plt_target", "property_name", "arg0", "arg1",
        "arg2_standard", "arg2_trinity", "arg3", "arg4",
        "property_nid", "trinity_predicate", "qualification",
    )
    with args.output.open("w", encoding="utf-8", newline="") as output:
        output.write("# " + "\t".join(columns) + "\n")
        writer = csv.DictWriter(output, fieldnames=columns, delimiter="\t",
                                lineterminator="\n")
        for key, sprx in load_profiles(args.profiles, args.firmware_root):
            if not sprx.is_file():
                raise SystemExit(f"missing SPRX for {key}: {sprx}")
            try:
                symbols = dynamic_symbols(args.readelf, sprx)
                if re.search(rf"\s{re.escape(PROPERTY_NID)}#", symbols) is None:
                    raise ValueError("process-property import is missing")
                trinity = re.search(rf"\b{TRINITY_NID}#", symbols) is not None
                string_vaddr = virtual_address_of_bytes(sprx, PROPERTY_NAME)
                calls, target, digest = property_calls(
                    disassemble(args.objdump, sprx), string_vaddr, trinity)
            except ValueError as error:
                raise SystemExit(f"{key}: {error}") from error
            writer.writerow({
                "firmware_abi_key": key,
                "carrier_fingerprint": digest,
                "call_count": len(calls),
                "callsites": ",".join(f"0x{address:x}" for address in calls),
                "plt_target": f"0x{target:x}",
                "property_name": "Sce.Debug:Gnm",
                "arg0": "property_name",
                "arg1": "gpu_info_base",
                "arg2_standard": "0x100000",
                "arg2_trinity": "0x180000" if trinity else "-",
                "arg3": "0",
                "arg4": "0",
                "property_nid": PROPERTY_NID,
                "trinity_predicate": TRINITY_NID if trinity else "-",
                "qualification": "exact-RE-no-direct-call",
            })
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
