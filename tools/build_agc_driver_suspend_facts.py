#!/usr/bin/env python3
"""Build the per-firmware suspend submission/query fact ledger."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def rows_without_comments(path: Path):
    with path.open(encoding="utf-8", newline="") as source:
        yield from (line for line in source if not line.startswith("#"))


def load_keys(path: Path) -> list[str]:
    fields = ("key", "profile", "family", "manifest", "sprx", "status")
    rows = csv.DictReader(rows_without_comments(path), delimiter="\t",
                          fieldnames=fields)
    return [row["key"] for row in rows]


def load_carriers(path: Path) -> dict[tuple[str, str], set[str]]:
    fields = ("operation", "command", "fingerprint", "count", "keys", "addresses")
    result: dict[tuple[str, str], set[str]] = {}
    rows = csv.DictReader(rows_without_comments(path), delimiter="\t",
                          fieldnames=fields)
    for row in rows:
        for key in row["keys"].split(","):
            result.setdefault((key, row["operation"]), set()).add(row["fingerprint"])
    return result


def groups(carriers: dict[tuple[str, str], set[str]], key: str,
           operation: str) -> str:
    values = carriers.get((key, operation), set())
    return ",".join(sorted(values)) if values else "MISSING"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profiles", type=Path,
        default=Path("analysis/agc_installed_driver_versions.tsv"))
    parser.add_argument("--carriers", type=Path,
        default=Path("analysis/agc_driver_command_carriers.tsv"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    carriers = load_carriers(args.carriers)
    columns = (
        "firmware_abi_key", "primary_command", "primary_carrier",
        "primary_payload", "primary_status", "final_command", "final_carriers",
        "final_status", "query_internal_command", "query_internal_carrier",
        "public_direct_exports", "query_status", "qualification",
    )
    with args.output.open("w", encoding="utf-8", newline="") as output:
        output.write("# " + "\t".join(columns) + "\n")
        writer = csv.DictWriter(output, fieldnames=columns, delimiter="\t",
                                lineterminator="\n")
        for key in load_keys(args.profiles):
            final_status = "disabled-final-wrapper-variants-pending"
            qualification = "RE-primary-hardware-pending"
            if key == "0x0550":
                final_status = "hardware-qualified"
                qualification = "hardware-qualified-FW5.50"
            elif key == "0x1160":
                final_status = "RE-exact-hardware-pending"
                qualification = "RE-primary-final-hardware-pending"
            writer.writerow({
                "firmware_abi_key": key,
                "primary_command": "0xc010811c",
                "primary_carrier": groups(carriers, key, "suspend_primary"),
                "primary_payload": "u32@0,u32@4,u32@8,u32@0xc",
                "primary_status": ("hardware-qualified" if key == "0x0550"
                                   else "RE-exact-hardware-pending"),
                "final_command": "0xc0108139",
                "final_carriers": groups(carriers, key, "suspend_final"),
                "final_status": final_status,
                "query_internal_command": "0x80048127",
                "query_internal_carrier": groups(
                    carriers, key, "queue_status_internal"),
                "public_direct_exports": "permission-stub-0x8a6d0001",
                "query_status": "disabled-semantics-not-exposed",
                "qualification": qualification,
            })
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
