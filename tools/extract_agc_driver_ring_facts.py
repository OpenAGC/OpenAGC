#!/usr/bin/env python3
"""Extract public TF-ring and HS-offchip payload facts for active drivers."""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import subprocess
from pathlib import Path


NIDS = {
    "tf": "XlNp7jzGiPo",
    "hs": "MM4IZSEYytQ",
    "tf_direct": "16IjQxB-Heo",
    "hs_direct": "DPcAnsOlTQs",
}
SYMBOL_RE = re.compile(
    r"^\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+FUNC\s+\S+\s+\S+\s+\S+\s+(\S+)"
)
INSN_RE = re.compile(r"^\s*([0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2}\s+)+\s*(.*?)\s*$")
CONTROL_RE = re.compile(r"^(callq?|j\w+)\s+.*$")
HEX_RE = re.compile(r"(?<![0-9a-fA-F])(0x[0-9a-fA-F]+)")


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


def is_trap(instruction: str) -> bool:
    return instruction.split(None, 1)[0] == "int3"


def command_carrier(instructions: list[tuple[int, str]], command: int) -> list[str]:
    matches = []
    for index, (_, instruction) in enumerate(instructions):
        values = {int(value, 16) for value in HEX_RE.findall(instruction)}
        if command not in values:
            continue
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
        matches.append([text for _, text in instructions[start:end]])
    if len(matches) != 1:
        raise ValueError(f"command 0x{command:08x} has {len(matches)} carriers")
    return matches[0]


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
    return any(expression.search(re.sub(r"\s+", " ", instruction))
               for instruction in body)


def validate_payload(name: str, body: list[str], command: int) -> str:
    required = (
        rf"movq %rsi, -0x20\(%rbp\)",
        rf"movl %edx, -0x18\(%rbp\)",
        rf"\$0x{command:x}",
    )
    missing = [pattern for pattern in required if not has(body, pattern)]
    if missing:
        raise ValueError(f"{name} payload changed; missing {','.join(missing)}")
    return ("explicit-zero" if has(body, r"movl \$0x0, -0x14\(%rbp\)")
            else "unspecified-by-Sony-wrapper")


def validate_permission_stub(name: str, body: list[str]) -> None:
    if not has(body, r"\$0x8a6d0001"):
        raise ValueError(f"{name} direct export is no longer a permission stub")


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
            raise SystemExit(f"missing ring exports for {key}: {','.join(missing)}")
        instructions = disassemble(args.objdump, sprx)
        bodies = {
            "tf_direct": body_for(instructions, *found["tf_direct"]),
            "hs_direct": body_for(instructions, *found["hs_direct"]),
        }
        try:
            bodies["tf"] = command_carrier(instructions, 0x80108128)
            bodies["hs"] = command_carrier(instructions, 0xC010812C)
            tf_reserved = validate_payload("TF-ring", bodies["tf"], 0x80108128)
            hs_reserved = validate_payload("HS-offchip", bodies["hs"], 0xC010812C)
            validate_permission_stub("TF-ring", bodies["tf_direct"])
            validate_permission_stub("HS-offchip", bodies["hs_direct"])
            if not has(bodies["tf"], r"(?:andl|testb) .*\$0x3"):
                raise ValueError("TF-ring size-alignment validation changed")
            if not has(bodies["tf"], r"testb %sil, %sil"):
                raise ValueError("TF-ring address-alignment validation changed")
        except ValueError as error:
            raise SystemExit(f"{key}: {error}") from error
        rows.append((
            key, relative_path, fingerprint(bodies["tf"]), str(len(bodies["tf"])),
            "0x80108128", "u64@0", "u32@8", f"u32@0xc:{tf_reserved}",
            "address-0x100,size-4", fingerprint(bodies["hs"]), str(len(bodies["hs"])),
            "0xc010812c", "u64@0", "u32@8", f"u32@0xc:{hs_reserved}",
            fingerprint(bodies["tf_direct"]), fingerprint(bodies["hs_direct"]),
            "permission-stub-0x8a6d0001", "RE-exact-hardware-pending" if key != "0x0550"
            else "hardware-qualified",
        ))

    header = (
        "# firmware_abi_key\tsprx_relative_path\ttf_carrier_fingerprint\ttf_instructions\t"
        "tf_command\ttf_address_field\ttf_size_field\ttf_reserved_field\t"
        "tf_validation\ths_carrier_fingerprint\ths_instructions\ths_command\t"
        "hs_list_field\ths_count_field\ths_reserved_field\ttf_direct_fingerprint\t"
        "hs_direct_fingerprint\tdirect_export_status\tqualification"
    )
    text = header + "\n" + "\n".join("\t".join(row) for row in rows) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
