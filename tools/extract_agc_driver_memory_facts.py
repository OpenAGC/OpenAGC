#!/usr/bin/env python3
"""Extract per-firmware internal-memory allocation facts from AGC drivers."""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fingerprint_agc_driver_command_carriers import (  # noqa: E402
    IMMEDIATE_RE, carrier_bounds, disassemble, normalize,
)


REQUIRED_STANDARD_CONSTANTS = {
    0x4000, 0x3C000, 0xFC000, 0x1E0000, 0xA00000, 0x1000000,
}
TRINITY_NID = "yu17wG8L5FI"


def rows_without_comments(path: Path):
    with path.open(encoding="utf-8", newline="") as source:
        yield from (line for line in source if not line.startswith("#"))


def load_profiles(path: Path, root: Path) -> list[tuple[str, Path]]:
    fields = ("key", "profile", "family", "manifest", "sprx", "status")
    rows = csv.DictReader(rows_without_comments(path), delimiter="\t",
                          fieldnames=fields)
    return [(row["key"], root / row["sprx"]) for row in rows]


def allocation_carrier(objdump: str, sprx: Path) -> tuple[str, int, set[int]]:
    instructions = disassemble(objdump, sprx)
    candidates = []
    for index, (_, instruction) in enumerate(instructions):
        if "$0xa00000" not in instruction.lower():
            continue
        start, end = carrier_bounds(instructions, index)
        body = instructions[start:end]
        values = {
            int(value, 16)
            for _, text in body for value in IMMEDIATE_RE.findall(text)
        }
        if REQUIRED_STANDARD_CONSTANTS <= values:
            candidates.append((body, values))
    if len(candidates) != 1:
        raise SystemExit(
            f"expected one internal-memory carrier in {sprx}, found "
            f"{len(candidates)}"
        )
    body, values = candidates[0]
    normalized = "\n".join(normalize(text) for _, text in body)
    digest = hashlib.sha256(normalized.encode("utf-8")).hexdigest()[:16]
    return digest, len(body), values


def imports_trinity(readelf: str, sprx: Path) -> bool:
    output = subprocess.run(
        (readelf, "--dyn-syms", str(sprx)), check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    ).stdout
    return re.search(rf"\b{re.escape(TRINITY_NID)}#", output) is not None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_root", type=Path)
    parser.add_argument("--profiles", type=Path,
        default=Path("analysis/agc_installed_driver_versions.tsv"))
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    columns = (
        "firmware_abi_key", "allocation_carrier", "instruction_count",
        "gpu_info_standard", "gpu_info_trinity", "trap_code", "trap_data",
        "ddid", "eop_fifo", "shadow_reg", "cwsr_standard", "cwsr_trinity",
        "misc", "cwsr_work_standard", "cwsr_work_trinity",
        "trinity_predicate", "qualification",
    )
    with args.output.open("w", encoding="utf-8", newline="") as output:
        output.write("# " + "\t".join(columns) + "\n")
        writer = csv.DictWriter(output, fieldnames=columns, delimiter="\t",
                                lineterminator="\n")
        for key, sprx in load_profiles(args.profiles, args.firmware_root):
            digest, instruction_count, values = allocation_carrier(
                args.objdump, sprx)
            trinity_constants = 0x1600000 in values
            trinity_import = imports_trinity(args.readelf, sprx)
            if trinity_constants != trinity_import:
                raise SystemExit(
                    f"Trinity constant/import mismatch for {key}: "
                    f"constant={trinity_constants} import={trinity_import}"
                )
            writer.writerow({
                "firmware_abi_key": key,
                "allocation_carrier": digest,
                "instruction_count": instruction_count,
                "gpu_info_standard": "0x100000",
                "gpu_info_trinity": "0x180000" if trinity_import else "-",
                "trap_code": "0x4000",
                "trap_data": "0x4000",
                "ddid": "0xfc000",
                "eop_fifo": "0x3c000",
                "shadow_reg": "0x4000",
                "cwsr_standard": "0x1000000",
                "cwsr_trinity": "0x1600000" if trinity_import else "-",
                "misc": "0x4000",
                "cwsr_work_standard": "0xa00000",
                "cwsr_work_trinity": "0x1000000" if trinity_import else "-",
                "trinity_predicate": TRINITY_NID if trinity_import else "-",
                "qualification": ("hardware-qualified-FW5.50" if key == "0x0550"
                                  else "RE-exact-hardware-pending"),
            })
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
