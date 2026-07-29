#!/usr/bin/env python3
"""Fingerprint private ioctl command-carrier functions in AGC driver SPRXs."""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import subprocess
from pathlib import Path


COMMANDS = {
    0xC0108102: "submit16",
    0xC00C810E: "queue_destroy",
    0xC010811C: "suspend_primary",
    0xC0108120: "tf_privileged",
    0xC0408121: "queue_create",
    0x80048126: "async_graphics",
    0x80048127: "queue_status_internal",
    0x80108128: "tf_public",
    0xC010812C: "hs_offchip",
    0xC0108139: "suspend_final",
}

INSN_RE = re.compile(
    r"^\s*([0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2}\s+)+\s*(.*?)\s*$"
)
IMMEDIATE_RE = re.compile(r"\$0x([0-9a-fA-F]+)")
CONTROL_RE = re.compile(r"^(callq?|j\w+)\s+.*$")


def rows_without_comments(path: Path):
    with path.open(encoding="utf-8", newline="") as source:
        yield from (line for line in source if not line.startswith("#"))


def load_profiles(path: Path, root: Path) -> list[tuple[str, Path]]:
    fields = ("key", "profile", "family", "manifest", "sprx", "status")
    rows = csv.DictReader(rows_without_comments(path), delimiter="\t",
                          fieldnames=fields)
    return [(row["key"], root / row["sprx"]) for row in rows]


def disassemble(objdump: str, sprx: Path) -> list[tuple[int, str]]:
    output = subprocess.run(
        (objdump, "-d", str(sprx)), check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    ).stdout
    instructions = []
    for line in output.splitlines():
        match = INSN_RE.match(line)
        if match:
            instructions.append((int(match.group(1), 16), match.group(2)))
    return instructions


def is_trap(instruction: str) -> bool:
    return instruction.split(None, 1)[0] == "int3"


def is_frame_prologue(instructions: list[tuple[int, str]], index: int) -> bool:
    if index + 1 >= len(instructions):
        return False
    first = re.sub(r"\s+", " ", instructions[index][1].strip())
    second = re.sub(r"\s+", " ", instructions[index + 1][1].strip())
    return first == "pushq %rbp" and second == "movq %rsp, %rbp"


def carrier_bounds(instructions: list[tuple[int, str]], index: int) -> tuple[int, int]:
    start = index
    while start > 0 and not is_frame_prologue(instructions, start):
        if is_trap(instructions[start - 1][1]):
            break
        start -= 1
    end = index + 1
    while end < len(instructions):
        if is_trap(instructions[end][1]) or is_frame_prologue(instructions, end):
            break
        end += 1
    return start, end


def normalize(instruction: str) -> str:
    instruction = instruction.split("#", 1)[0].strip()
    instruction = re.sub(r"[-+]?0x[0-9a-fA-F]+\(%rip\)",
                         "<rip>(%rip)", instruction)
    if CONTROL_RE.match(instruction):
        return instruction.split(None, 1)[0] + " <target>"
    return re.sub(r"\s+", " ", instruction)


def fingerprints(objdump: str, sprx: Path) -> dict[int, list[tuple[str, int, int]]]:
    instructions = disassemble(objdump, sprx)
    result: dict[int, list[tuple[str, int, int]]] = {}
    seen: set[tuple[int, int, int]] = set()
    for index, (_, instruction) in enumerate(instructions):
        immediates = {int(value, 16) for value in IMMEDIATE_RE.findall(instruction)}
        for command in immediates & COMMANDS.keys():
            start, end = carrier_bounds(instructions, index)
            identity = (command, start, end)
            if identity in seen:
                continue
            seen.add(identity)
            body = "\n".join(normalize(text)
                             for _, text in instructions[start:end])
            digest = hashlib.sha256(body.encode("utf-8")).hexdigest()[:16]
            result.setdefault(command, []).append(
                (digest, len(instructions[start:end]), instructions[start][0])
            )
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_root", type=Path)
    parser.add_argument("--profiles", type=Path,
        default=Path("analysis/agc_installed_driver_versions.tsv"))
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    grouped: dict[tuple[int, str, int], list[tuple[str, int]]] = {}
    missing: list[tuple[int, str]] = []
    for key, sprx in load_profiles(args.profiles, args.firmware_root):
        if not sprx.is_file():
            raise SystemExit(f"missing SPRX for {key}: {sprx}")
        found = fingerprints(args.objdump, sprx)
        for command in COMMANDS:
            carriers = found.get(command, [])
            if not carriers:
                missing.append((command, key))
                continue
            for digest, instruction_count, address in carriers:
                grouped.setdefault((command, digest, instruction_count), []).append(
                    (key, address)
                )

    with args.output.open("w", encoding="utf-8", newline="") as output:
        output.write("# operation\tcommand\tcarrier_fingerprint\t"
                     "instruction_count\tfirmware_abi_keys\taddresses\n")
        for command, operation in COMMANDS.items():
            members = sorted(
                (identity, values) for identity, values in grouped.items()
                if identity[0] == command
            )
            for (_, digest, instruction_count), values in members:
                keys = ",".join(key for key, _ in values)
                addresses = ",".join(f"{key}@0x{address:x}"
                                     for key, address in values)
                output.write(
                    f"{operation}\t0x{command:08x}\t{digest}\t"
                    f"{instruction_count}\t{keys}\t{addresses}\n"
                )
            absent = [key for missing_command, key in missing
                      if missing_command == command]
            if absent:
                output.write(
                    f"{operation}\t0x{command:08x}\tMISSING\t0\t"
                    f"{','.join(absent)}\t-\n"
                )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
