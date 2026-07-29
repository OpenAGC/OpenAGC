#!/usr/bin/env python3
"""Build the conservative per-firmware AGC driver operation ledger."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


FINGERPRINT_COLUMNS = {
    "sceAgcDriverSubmitDcb": "submit_dcb_group",
    "sceAgcDriverSubmitAcb": "submit_acb_group",
    "sceAgcDriverSuspendPointSubmitDirect": "suspend_direct_group",
    "sceAgcDriverIsSuspendPointInFlightDirect": "suspend_query_direct_group",
    "sceAgcDriverSetWorkloadsActive": "workload_active_group",
    "sceAgcDriverSetWorkloadComplete": "workload_complete_group",
    "sceAgcDriverSetTFRing": "tf_public_group",
    "sceAgcDriverSetTFRingDirect": "tf_direct_group",
    "sceAgcDriverSetHsOffchipParam": "hs_public_group",
    "sceAgcDriverSetHsOffchipParamDirect": "hs_direct_group",
    "sceAgcDriverNotifyDefaultStates": "defaults_group",
    "sceAgcDriverSetupAsyncGraphics": "async_group",
}


def rows_without_comments(path: Path):
    with path.open(encoding="utf-8", newline="") as source:
        yield from (line for line in source if not line.startswith("#"))


def load_profiles(path: Path) -> list[dict[str, str]]:
    fields = ("key", "profile", "family", "manifest", "sprx", "status")
    return list(csv.DictReader(rows_without_comments(path), delimiter="\t",
                               fieldnames=fields))


def load_fingerprints(path: Path) -> dict[tuple[str, str], str]:
    result: dict[tuple[str, str], str] = {}
    fields = ("operation", "fingerprint", "size", "keys", "nids")
    for row in csv.DictReader(rows_without_comments(path), delimiter="\t",
                              fieldnames=fields):
        for key in row["keys"].split(","):
            result[(key, row["operation"])] = row["fingerprint"]
    return result


def evidence_for(key: str) -> dict[str, str]:
    evidence = {
        "enabled_direct_ops": "submit16,tf-ring,hs-offchip,async",
        "queue": "disabled-pending-layout",
        "suspend_submit": "disabled-pending-layout",
        "suspend_query": "disabled-permission-export",
        "workload": "disabled-not-adapted",
        "tf_ring": "RE-exact-public-0x80108128-hardware-pending",
        "hs_offchip": "RE-exact-0xc010812c-hardware-pending",
        "memory": "disabled-pending-layout",
        "defaults": "disabled-version-unknown",
        "async_graphics": "RE-exact-0x80048126-hardware-pending",
        "qualification": "RE-operation-carriers-hardware-pending",
    }
    if key == "0x0550":
        evidence.update({
            "enabled_direct_ops": (
                "submit16,memory,queue,suspend-primary,suspend-final,"
                "workload-extension,tf-ring,hs-offchip,defaults-v8,async"
            ),
            "queue": "hardware-qualified",
            "suspend_submit": "hardware-qualified-primary-final",
            "workload": "hardware-qualified-openagc-extension",
            "tf_ring": "hardware-qualified-public-0x80108128",
            "hs_offchip": "hardware-qualified-0xc010812c",
            "memory": "hardware-qualified-standard",
            "defaults": "hardware-qualified-v8",
            "async_graphics": "hardware-qualified-0x80048126",
            "qualification": "hardware-qualified-FW5.50",
        })
    elif key == "0x1160":
        evidence.update({
            "enabled_direct_ops": (
                "submit16,memory,queue,suspend-primary,suspend-final,"
                "tf-ring,hs-offchip,async"
            ),
            "queue": "RE-exact-authenticated-0xc0408121-0xc00c810e",
            "suspend_submit": "RE-exact-0xc010811c-0xc0108139",
            "workload": "disabled-nine-dword-Sony-ABI",
            "tf_ring": "RE-exact-public-0x80108128",
            "hs_offchip": "RE-exact-0xc010812c",
            "memory": "RE-exact-standard-Trinity",
            "async_graphics": "RE-exact-0x80048126",
            "qualification": "RE-exact-hardware-pending",
        })
    return evidence


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profiles", type=Path,
        default=Path("analysis/agc_installed_driver_versions.tsv"))
    parser.add_argument("--fingerprints", type=Path,
        default=Path("analysis/agc_driver_wrapper_fingerprints.tsv"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    profiles = load_profiles(args.profiles)
    fingerprints = load_fingerprints(args.fingerprints)
    evidence_columns = (
        "enabled_direct_ops", "queue", "suspend_submit", "suspend_query",
        "workload", "tf_ring", "hs_offchip", "memory", "defaults",
        "async_graphics", "qualification",
    )
    columns = ("firmware_abi_key", "abi_family", *evidence_columns,
               *FINGERPRINT_COLUMNS.values())

    with args.output.open("w", encoding="utf-8", newline="") as output:
        output.write("# " + "\t".join(columns) + "\n")
        writer = csv.DictWriter(output, fieldnames=columns, delimiter="\t",
                                lineterminator="\n")
        for profile in profiles:
            key = profile["key"]
            row = {
                "firmware_abi_key": key,
                "abi_family": profile["family"],
                **evidence_for(key),
            }
            for operation, column in FINGERPRINT_COLUMNS.items():
                row[column] = fingerprints.get((key, operation), "MISSING")
            writer.writerow(row)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
