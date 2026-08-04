#!/usr/bin/env python3
"""Keep hardware qualification labels conservative across active profiles."""

from __future__ import annotations

import csv
from pathlib import Path


ENDPOINTS = {"0x0550", "0x1160"}
INTERMEDIATE_LABEL = "SPRX-qualified-hardware-unverified"


def rows_without_comments(path: Path):
    with path.open(encoding="utf-8", newline="") as source:
        yield from (line for line in source if not line.startswith("#"))


def main() -> int:
    ledger = Path("analysis/agc_driver_operation_facts.tsv")
    header = ledger.read_text(encoding="utf-8").splitlines()[0][2:].split("\t")
    rows = list(csv.DictReader(
        rows_without_comments(ledger), delimiter="\t", fieldnames=header
    ))
    if len(rows) != 39:
        raise SystemExit(f"expected 39 active profiles, found {len(rows)}")

    found_endpoints = set()
    for row in rows:
        key = row["firmware_abi_key"]
        qualification = row["qualification"]
        if key in ENDPOINTS:
            if not qualification.startswith("hardware-qualified"):
                raise SystemExit(f"{key} lost endpoint hardware qualification")
            found_endpoints.add(key)
        elif qualification != INTERMEDIATE_LABEL:
            raise SystemExit(
                f"{key} must remain {INTERMEDIATE_LABEL}, got {qualification}"
            )
    if found_endpoints != ENDPOINTS:
        raise SystemExit("hardware endpoint set is incomplete")

    print("PASS: 37 intermediate profiles are SPRX-qualified/hardware-unverified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
