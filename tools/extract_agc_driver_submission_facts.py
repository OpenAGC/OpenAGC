#!/usr/bin/env python3
"""Extract direct submission wrapper and payload facts for active drivers."""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import subprocess
from pathlib import Path


NIDS = {
    "dcb": "UglJIZjGssM",
    "acb": "gSRnr79F8tQ",
    "multi": "6UzEidRZwkg",
}
SUBMIT_COMMAND = 0xC0108102
SYMBOL_RE = re.compile(
    r"^\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+FUNC\s+\S+\s+\S+\s+\S+\s+(\S+)"
)
INSN_RE = re.compile(r"^\s*([0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2}\s+)+\s*(.*?)\s*$")
HEX_RE = re.compile(r"(?<![0-9a-fA-F])(0x[0-9a-fA-F]+)")
CONTROL_RE = re.compile(r"^(callq?|j\w+)\s+.*$")


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
    result = {}
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


def is_frame_prologue(instructions: list[tuple[int, str]], index: int) -> bool:
    if index + 1 >= len(instructions):
        return False
    return (re.sub(r"\s+", " ", instructions[index][1]) == "pushq %rbp" and
            re.sub(r"\s+", " ", instructions[index + 1][1]) == "movq %rsp, %rbp")


def command_carrier(instructions: list[tuple[int, str]], command: int) -> list[str]:
    matches = []
    for index, (_, instruction) in enumerate(instructions):
        if command not in {int(value, 16) for value in HEX_RE.findall(instruction)}:
            continue
        start = index
        while start > 0 and not is_frame_prologue(instructions, start):
            if instructions[start - 1][1].split(None, 1)[0] == "int3":
                break
            start -= 1
        end = index + 1
        while end < len(instructions):
            if (instructions[end][1].split(None, 1)[0] == "int3" or
                    is_frame_prologue(instructions, end)):
                break
            end += 1
        matches.append([text for _, text in instructions[start:end]])
    if len(matches) != 1:
        raise ValueError(f"submit command has {len(matches)} carriers")
    return matches[0]


def normalize(instruction: str) -> str:
    instruction = re.sub(r"[-+]?0x[0-9a-fA-F]+\(%rip\)", "<rip>(%rip)", instruction)
    if CONTROL_RE.match(instruction):
        return f"{instruction.split(None, 1)[0]} <target>"
    return re.sub(r"\s+", " ", instruction)


def fingerprint(body: list[str]) -> str:
    normalized = "\n".join(normalize(instruction) for instruction in body)
    return hashlib.sha256(normalized.encode("utf-8")).hexdigest()[:16]


def require(body: list[str], pattern: str, label: str) -> None:
    expression = re.compile(pattern)
    if not any(expression.search(re.sub(r"\s+", " ", instruction)) for instruction in body):
        raise ValueError(f"submit carrier missing {label}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_root", type=Path)
    parser.add_argument("--profiles", type=Path,
                        default=Path("analysis/agc_installed_driver_versions.tsv"))
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    rows = []
    for key, sprx, relative_path in load_profiles(args.profiles, args.firmware_root):
        if not sprx.is_file():
            raise SystemExit(f"missing SPRX for {key}: {sprx}")
        found = load_symbols(args.readelf, sprx)
        if found.keys() != NIDS.keys():
            missing = NIDS.keys() - found.keys()
            raise SystemExit(f"missing submission exports for {key}: {','.join(missing)}")
        instructions = disassemble(args.objdump, sprx)
        bodies = {name: body_for(instructions, *found[name]) for name in NIDS}
        try:
            carrier = command_carrier(instructions, SUBMIT_COMMAND)
            require(carrier, r"movl %esi, -0x20\(%rbp\)", "u32@0")
            require(carrier, r"movl %edx, -0x1c\(%rbp\)", "u32@4")
            require(carrier, r"movq %rcx, -0x18\(%rbp\)", "u64@8")
        except ValueError as error:
            raise SystemExit(f"{key}: {error}") from error
        rows.append((
            key, relative_path, f"0x{SUBMIT_COMMAND:08x}", fingerprint(carrier),
            str(len(carrier)), "u32@0", "u32@4", "u64@8",
            fingerprint(bodies["dcb"]), str(found["dcb"][1]),
            fingerprint(bodies["acb"]), str(found["acb"][1]),
            fingerprint(bodies["multi"]), str(found["multi"][1]),
            "hardware-qualified" if key == "0x0550" else "RE-exact-hardware-pending",
        ))

    header = (
        "# firmware_abi_key\tsprx_relative_path\tsubmit_command\t"
        "carrier_fingerprint\tcarrier_instructions\tqueue_type_field\t"
        "descriptor_count_field\tdescriptor_pointer_field\tdcb_fingerprint\t"
        "dcb_size_bytes\tacb_fingerprint\tacb_size_bytes\tmulti_fingerprint\t"
        "multi_size_bytes\tqualification"
    )
    text = header + "\n" + "\n".join("\t".join(row) for row in rows) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
