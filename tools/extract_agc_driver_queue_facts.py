#!/usr/bin/env python3
"""Extract per-firmware authenticated special-queue setup facts."""

from __future__ import annotations

import argparse
import csv
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fingerprint_agc_driver_command_carriers import (  # noqa: E402
    carrier_bounds, disassemble, normalize,
)


REQUIRED_TEXT = (
    "39000", "1c8000", "1cc000", "af1e80b7", "8b4cdd90",
    "99f68d6c", "e5fcc174",
)


def rows_without_comments(path: Path):
    with path.open(encoding="utf-8", newline="") as source:
        yield from (line for line in source if not line.startswith("#"))


def load_profiles(path: Path, root: Path) -> list[tuple[str, Path]]:
    fields = ("key", "profile", "family", "manifest", "sprx", "status")
    rows = csv.DictReader(rows_without_comments(path), delimiter="\t",
                          fieldnames=fields)
    return [(row["key"], root / row["sprx"]) for row in rows]


def queue_setup_carrier(objdump: str, sprx: Path) -> tuple[str, int]:
    instructions = disassemble(objdump, sprx)
    candidates = []
    for index, (_, instruction) in enumerate(instructions):
        if "1cc000" not in instruction.lower():
            continue
        start, end = carrier_bounds(instructions, index)
        body = instructions[start:end]
        raw = "\n".join(text.lower() for _, text in body)
        if all(value in raw for value in REQUIRED_TEXT):
            candidates.append(body)
    if len(candidates) != 1:
        raise SystemExit(
            f"expected one authenticated queue setup carrier in {sprx}, "
            f"found {len(candidates)}"
        )
    body = candidates[0]
    normalized = "\n".join(normalize(text) for _, text in body)
    digest = hashlib.sha256(normalized.encode("utf-8")).hexdigest()[:16]
    return digest, len(body)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_root", type=Path)
    parser.add_argument("--profiles", type=Path,
        default=Path("analysis/agc_installed_driver_versions.tsv"))
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    columns = (
        "firmware_abi_key", "setup_carrier", "instruction_count",
        "create_command", "destroy_command", "magic1", "magic2", "magic3",
        "token", "eop_ring_offset", "read_pointer_offset",
        "metadata_offset", "pipe_id", "ring_size", "qualification",
    )
    with args.output.open("w", encoding="utf-8", newline="") as output:
        output.write("# " + "\t".join(columns) + "\n")
        writer = csv.DictWriter(output, fieldnames=columns, delimiter="\t",
                                lineterminator="\n")
        for key, sprx in load_profiles(args.profiles, args.firmware_root):
            digest, instruction_count = queue_setup_carrier(args.objdump, sprx)
            writer.writerow({
                "firmware_abi_key": key,
                "setup_carrier": digest,
                "instruction_count": instruction_count,
                "create_command": "0xc0408121",
                "destroy_command": "0xc00c810e",
                "magic1": "0xaf1e80b7",
                "magic2": "0x8b4cdd90",
                "magic3": "0x99f68d6c",
                "token": "0xe5fcc174",
                "eop_ring_offset": "0x39000",
                "read_pointer_offset": "0x1c8000",
                "metadata_offset": "0x1cc000",
                "pipe_id": "0xc",
                "ring_size": "0x1000",
                "qualification": ("hardware-qualified-FW5.50" if key == "0x0550"
                                  else "RE-exact-hardware-pending"),
            })
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
