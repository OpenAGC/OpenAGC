#!/usr/bin/env python3
"""Extract libSceAgc register-default dispatch facts for active firmware."""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import subprocess
from pathlib import Path


VERSIONED_NID = "2JtWUUiYBXs"
RUNTIME_NID = "Wi82ArQtAwg"
INTERNAL_VERSIONED_NID = "wRbq6ZjNop4"
INTERNAL_RUNTIME_NID = "uIwxsqDlHRc"
INIT_NID = "kW3GLb7QfPg"

SYMBOL_RE = re.compile(
    r"^\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+FUNC\s+\S+\s+\S+\s+\S+\s+(\S+)"
)
INSN_RE = re.compile(r"^\s*([0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2}\s+)+\s*(.*?)\s*$")
CONTROL_RE = re.compile(r"^(callq?|j\w+)\s+.*$")
CMP_RE = re.compile(r"^cmp[lq]?\s+\$(0x[0-9a-fA-F]+|\d+),\s+%(?:e?(?:bx|di))$")
FIELD_RE = re.compile(
    r"^movl\s+(0x[0-9a-fA-F]+|\d+)\(%r(?:ax|cx),%r(?:ax|cx)\),\s+%edi$"
)
SHIFT_RE = re.compile(r"^shlq\s+\$(0x[0-9a-fA-F]+|\d+),\s+%rax$")


def rows_without_comments(path: Path):
    with path.open(encoding="utf-8", newline="") as source:
        yield from (line for line in source if not line.startswith("#"))


def load_profiles(path: Path, firmware_root: Path) -> list[tuple[str, Path, str]]:
    fields = ("key", "profile", "family", "manifest", "sprx", "status")
    profiles = []
    for row in csv.DictReader(rows_without_comments(path), delimiter="\t", fieldnames=fields):
        driver_path = Path(row["sprx"])
        agc_path = driver_path.with_name("libSceAgc.sprx")
        profiles.append((row["key"], firmware_root / agc_path, agc_path.as_posix()))
    return profiles


def run(tool: str, *args: str) -> str:
    return subprocess.run(
        (tool, *args), check=True, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    ).stdout


def symbols(readelf: str, sprx: Path) -> dict[str, tuple[int, int]]:
    result: dict[str, tuple[int, int]] = {}
    for line in run(readelf, "-Ws", str(sprx)).splitlines():
        match = SYMBOL_RE.match(line)
        if not match:
            continue
        address, size, nid = match.groups()
        nid = nid.split("#", 1)[0]
        if nid in (VERSIONED_NID, RUNTIME_NID, INTERNAL_VERSIONED_NID,
                   INTERNAL_RUNTIME_NID, INIT_NID) and int(size) > 0:
            result[nid] = (int(address, 16), int(size))
    return result


def instructions(objdump: str, sprx: Path) -> list[tuple[int, str]]:
    result = []
    for line in run(objdump, "-d", str(sprx)).splitlines():
        match = INSN_RE.match(line)
        if match:
            result.append((int(match.group(1), 16), match.group(2).split("#", 1)[0].strip()))
    return result


def body_for(all_instructions: list[tuple[int, str]], address: int, size: int) -> list[str]:
    return [instruction for pc, instruction in all_instructions if address <= pc < address + size]


def normalize(instruction: str) -> str:
    instruction = re.sub(r"[-+]?0x[0-9a-fA-F]+\(%rip\)", "<rip>(%rip)", instruction)
    if CONTROL_RE.match(instruction):
        return f"{instruction.split(None, 1)[0]} <target>"
    return re.sub(r"\s+", " ", instruction)


def fingerprint(body: list[str]) -> str:
    normalized = "\n".join(normalize(instruction) for instruction in body)
    return hashlib.sha256(normalized.encode("utf-8")).hexdigest()[:16]


def max_version(body: list[str]) -> int:
    candidates: list[int] = []
    for index, instruction in enumerate(body):
        match = CMP_RE.match(instruction)
        if not match:
            continue
        following = body[index + 1:index + 4]
        if any(item.startswith("ja\t") or item.startswith("ja ") for item in following):
            value = int(match.group(1), 0)
            if value <= 32:
                candidates.append(value)
    if not candidates:
        raise ValueError("versioned dispatcher has no bounded unsigned version check")
    return max(candidates)


def runtime_selector(body: list[str]) -> tuple[int, int, str]:
    field_offsets = []
    shifts = []
    for instruction in body:
        field_match = FIELD_RE.match(instruction)
        if field_match:
            field_offsets.append(int(field_match.group(1), 0))
        shift_match = SHIFT_RE.match(instruction)
        if shift_match:
            shifts.append(int(shift_match.group(1), 0))
    has_times_five = any("(%rax,%rax,4)" in instruction for instruction in body)
    has_table_index = any(instruction.startswith("movslq\t") and "(%rip)" in instruction
                          for instruction in body)
    if len(field_offsets) != 1 or len(shifts) != 1 or not has_times_five or not has_table_index:
        raise ValueError("runtime wrapper is not the expected indexed hardware-table selector")
    stride = 5 << shifts[0]
    return stride, field_offsets[0], "sceAgcInit-version-argument"


def verify_init_version_flow(init_body: list[str], all_instructions: list[tuple[int, str]],
                             field_offset: int) -> None:
    if not any(instruction == "movl\t%edi, %esi" or
               instruction == "movl %edi, %esi" for instruction in init_body):
        raise ValueError("sceAgcInit does not forward its version argument")
    targets = []
    for instruction in init_body:
        match = re.match(r"^jmpq?\s+(0x[0-9a-fA-F]+)", instruction)
        if match:
            targets.append(int(match.group(1), 16))
    if not targets:
        raise ValueError("sceAgcInit has no common-initializer tail call")
    target = targets[-1]
    initializer = [instruction for pc, instruction in all_instructions
                   if target <= pc < target + 0x600]
    field = f"0x{field_offset:x}("
    saved_registers = {
        match.group(1)
        for instruction in initializer
        if (match := re.match(
            r"^movl\s+%esi,\s*%(ebx|r12d|r13d|r14d|r15d)$", instruction))
    }
    stored = any(
        re.match(rf"^movl\s+%{register},", instruction) and
        field in instruction
        for register in saved_registers for instruction in initializer
    )
    if not saved_registers or not stored:
        raise ValueError("initializer does not store the version in the runtime record")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_root", type=Path)
    parser.add_argument("--profiles", type=Path,
        default=Path("analysis/agc_firmware_versions.tsv"))
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    rows = []
    for key, sprx, relative_path in load_profiles(args.profiles, args.firmware_root):
        if not sprx.is_file():
            raise SystemExit(f"missing libSceAgc for {key}: {sprx}")
        found = symbols(args.readelf, sprx)
        missing = {VERSIONED_NID, RUNTIME_NID, INTERNAL_VERSIONED_NID,
                   INTERNAL_RUNTIME_NID, INIT_NID} - found.keys()
        if missing:
            raise SystemExit(f"missing defaults exports for {key}: {','.join(sorted(missing))}")
        all_instructions = instructions(args.objdump, sprx)
        versioned_address, versioned_size = found[VERSIONED_NID]
        runtime_address, runtime_size = found[RUNTIME_NID]
        internal_versioned_address, internal_versioned_size = found[INTERNAL_VERSIONED_NID]
        internal_runtime_address, internal_runtime_size = found[INTERNAL_RUNTIME_NID]
        init_address, init_size = found[INIT_NID]
        versioned_body = body_for(all_instructions, versioned_address, versioned_size)
        runtime_body = body_for(all_instructions, runtime_address, runtime_size)
        internal_versioned_body = body_for(
            all_instructions, internal_versioned_address, internal_versioned_size)
        internal_runtime_body = body_for(
            all_instructions, internal_runtime_address, internal_runtime_size)
        init_body = body_for(all_instructions, init_address, init_size)
        try:
            maximum = max_version(versioned_body)
            stride, field_offset, selector = runtime_selector(runtime_body)
            internal_maximum = max_version(internal_versioned_body)
            internal_stride, internal_field_offset, internal_selector = \
                runtime_selector(internal_runtime_body)
            verify_init_version_flow(init_body, all_instructions, field_offset)
        except ValueError as error:
            raise SystemExit(f"{key}: {error}") from error
        if (internal_maximum != maximum or internal_stride != stride or
                internal_field_offset != field_offset or
                internal_selector != selector):
            raise SystemExit(f"{key}: primary/internal defaults selectors disagree")
        rows.append((
            key, relative_path, VERSIONED_NID, f"0x{versioned_address:x}",
            str(versioned_size), fingerprint(versioned_body), str(maximum),
            RUNTIME_NID, f"0x{runtime_address:x}", str(runtime_size),
            fingerprint(runtime_body), selector, f"0x{stride:x}",
            f"0x{field_offset:x}", "caller-argument-flow-proven",
            INTERNAL_VERSIONED_NID, f"0x{internal_versioned_address:x}",
            str(internal_versioned_size), fingerprint(internal_versioned_body),
            str(internal_maximum), INTERNAL_RUNTIME_NID,
            f"0x{internal_runtime_address:x}", str(internal_runtime_size),
            fingerprint(internal_runtime_body),
        ))

    header = (
        "# firmware_abi_key\tlibSceAgc_relative_path\tversioned_nid\tversioned_vaddr\t"
        "versioned_size\tversioned_fingerprint\tmax_dispatch_version\truntime_nid\t"
        "runtime_vaddr\truntime_size\truntime_fingerprint\tselector_source\t"
        "table_stride\tselector_field_offset\tselection_evidence\t"
        "internal_versioned_nid\tinternal_versioned_vaddr\tinternal_versioned_size\t"
        "internal_versioned_fingerprint\tinternal_max_dispatch_version\t"
        "internal_runtime_nid\tinternal_runtime_vaddr\tinternal_runtime_size\t"
        "internal_runtime_fingerprint"
    )
    text = header + "\n" + "\n".join("\t".join(row) for row in rows) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
