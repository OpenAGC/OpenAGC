#!/usr/bin/env python3
"""Group normalized libSceAgcDriver export wrappers across firmware dumps."""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import subprocess
from pathlib import Path


OPERATIONS = (
    "sceAgcDriverSubmitDcb",
    "sceAgcDriverSubmitAcb",
    "sceAgcDriverSubmitMultiDcbs",
    "sceAgcDriverSuspendPointSubmitDirect",
    "sceAgcDriverIsSuspendPointInFlightDirect",
    "sceAgcDriverSetWorkloadsActive",
    "sceAgcDriverSetWorkloadComplete",
    "sceAgcDriverSetTFRing",
    "sceAgcDriverSetTFRingDirect",
    "sceAgcDriverSetHsOffchipParam",
    "sceAgcDriverSetHsOffchipParamDirect",
    "sceAgcDriverNotifyDefaultStates",
    "sceAgcDriverSetupAsyncGraphics",
)

SYMBOL_RE = re.compile(
    r"^\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+FUNC\s+\S+\s+\S+\s+\S+\s+(\S+)"
)
INSN_RE = re.compile(r"^\s*([0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2}\s+)+\s*(.*?)\s*$")
CONTROL_RE = re.compile(r"^(callq?|j\w+)\s+.*$")


def load_names(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    with path.open(encoding="utf-8") as source:
        for line in source:
            nid, separator, name = line.rstrip("\n").partition(" ")
            if separator and name in OPERATIONS:
                result[nid] = name
    return result


def load_profiles(path: Path, firmware_root: Path) -> list[tuple[str, Path]]:
    profiles: list[tuple[str, Path]] = []
    with path.open(encoding="utf-8", newline="") as source:
        rows = csv.DictReader(
            (line for line in source if not line.startswith("#")), delimiter="\t",
            fieldnames=("key", "profile", "family", "manifest", "sprx", "status"),
        )
        for row in rows:
            profiles.append((row["key"], firmware_root / row["sprx"]))
    return profiles


def run(tool: str, *args: str) -> str:
    return subprocess.run(
        (tool, *args), check=True, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    ).stdout


def normalize_instruction(instruction: str) -> str:
    instruction = instruction.split("#", 1)[0].strip()
    instruction = re.sub(r"[-+]?0x[0-9a-fA-F]+\(%rip\)", "<rip>(%rip)", instruction)
    if CONTROL_RE.match(instruction):
        mnemonic = instruction.split(None, 1)[0]
        return f"{mnemonic} <target>"
    return re.sub(r"\s+", " ", instruction)


def fingerprints(
    readelf: str, objdump: str, sprx: Path, names: dict[str, str]
) -> dict[str, tuple[str, int, str]]:
    symbols: list[tuple[int, int, str, str]] = []
    for line in run(readelf, "-Ws", str(sprx)).splitlines():
        match = SYMBOL_RE.match(line)
        if not match:
            continue
        address, size, nid = match.groups()
        nid = nid.split("#", 1)[0]
        if nid in names and int(size) > 0:
            symbols.append((int(address, 16), int(size), nid, names[nid]))

    instructions: list[tuple[int, str]] = []
    for line in run(objdump, "-d", str(sprx)).splitlines():
        match = INSN_RE.match(line)
        if match:
            instructions.append((int(match.group(1), 16), match.group(2)))

    result: dict[str, tuple[str, int, str]] = {}
    for address, size, nid, name in symbols:
        body = "\n".join(
            normalize_instruction(instruction)
            for pc, instruction in instructions
            if address <= pc < address + size
        )
        digest = hashlib.sha256(body.encode("utf-8")).hexdigest()[:16]
        result[name] = (digest, size, nid)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_root", type=Path)
    parser.add_argument("--ledger", type=Path,
        default=Path("analysis/agc_installed_driver_versions.tsv"))
    parser.add_argument("--aerolib", type=Path, required=True)
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--output", type=Path,
        help="write the TSV to this path instead of stdout")
    args = parser.parse_args()

    names = load_names(args.aerolib)
    grouped: dict[tuple[str, str], list[tuple[str, int, str]]] = {}
    missing: list[tuple[str, str]] = []
    for key, sprx in load_profiles(args.ledger, args.firmware_root):
        if not sprx.is_file():
            raise SystemExit(f"missing SPRX for {key}: {sprx}")
        found = fingerprints(args.readelf, args.objdump, sprx, names)
        for operation in OPERATIONS:
            if operation not in found:
                missing.append((operation, key))
                continue
            digest, size, nid = found[operation]
            grouped.setdefault((operation, digest), []).append((key, size, nid))

    output = ["# operation\tfingerprint\tsize_bytes\tfirmware_abi_keys\tnids"]
    for operation in OPERATIONS:
        for (group_operation, digest), members in sorted(grouped.items()):
            if group_operation != operation:
                continue
            sizes = sorted({size for _, size, _ in members})
            keys = ",".join(key for key, _, _ in members)
            nids = ",".join(sorted({nid for _, _, nid in members}))
            size_text = ",".join(str(size) for size in sizes)
            output.append(f"{operation}\t{digest}\t{size_text}\t{keys}\t{nids}")
    for operation, key in missing:
        output.append(f"{operation}\tMISSING\t0\t{key}\t-")
    text = "\n".join(output) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
