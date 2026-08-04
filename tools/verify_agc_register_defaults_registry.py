#!/usr/bin/env python3
"""Verify the direct-profile defaults bounds against the SPRX fact ledger."""

from __future__ import annotations

import csv
import re
from pathlib import Path


ENTRY_RE = re.compile(
    r"\{(0x[0-9a-fA-F]+)u,\s*AGC_REGISTER_DEFAULTS_VERSION_(\d+)\}"
)


def rows_without_comments(path: Path):
    with path.open(encoding="utf-8", newline="") as source:
        yield from (line for line in source if not line.startswith("#"))


def main() -> int:
    facts_path = Path("analysis/agc_register_defaults_facts.tsv")
    registry_path = Path("src/driver_registry.c")
    fields = (
        "key", "path", "versioned_nid", "versioned_vaddr", "versioned_size",
        "versioned_fingerprint", "max_dispatch_version", "runtime_nid",
        "runtime_vaddr", "runtime_size", "runtime_fingerprint", "selector_source",
        "table_stride", "selector_field_offset", "evidence",
        "internal_versioned_nid", "internal_versioned_vaddr",
        "internal_versioned_size", "internal_versioned_fingerprint",
        "internal_max_dispatch_version", "internal_runtime_nid",
        "internal_runtime_vaddr", "internal_runtime_size",
        "internal_runtime_fingerprint",
    )
    expected = {
        int(row["key"], 0): int(row["max_dispatch_version"], 0)
        for row in csv.DictReader(
            rows_without_comments(facts_path), delimiter="\t", fieldnames=fields
        )
    }
    source = registry_path.read_text(encoding="utf-8")
    table_start = source.index("g_direct_defaults_bounds[]")
    table_end = source.index("};", table_start)
    actual = {
        int(key, 0): int(version, 10)
        for key, version in ENTRY_RE.findall(source[table_start:table_end])
    }

    if actual != expected:
        missing = sorted(expected.keys() - actual.keys())
        extra = sorted(actual.keys() - expected.keys())
        mismatched = sorted(
            key for key in expected.keys() & actual.keys()
            if expected[key] != actual[key]
        )
        raise SystemExit(
            "register-default registry differs from SPRX facts: "
            f"missing={missing}, extra={extra}, mismatched={mismatched}"
        )

    print(f"PASS: registry matches all {len(expected)} exact defaults bounds")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
