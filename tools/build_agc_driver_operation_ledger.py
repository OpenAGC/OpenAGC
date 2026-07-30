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

TRINITY_KEYS = {
    "0x0900", "0x0905", "0x0920", "0x0940", "0x0960",
    "0x1001", "0x1020", "0x1040", "0x1060",
    "0x1100", "0x1120", "0x1140", "0x1160",
    "0x1200", "0x1202", "0x1220", "0x1240", "0x1260", "0x1270",
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


def load_defaults(path: Path) -> dict[str, tuple[str, str]]:
    result: dict[str, tuple[str, str]] = {}
    fields = (
        "key", "path", "versioned_nid", "versioned_vaddr", "versioned_size",
        "versioned_fingerprint", "max_dispatch_version", "runtime_nid",
        "runtime_vaddr", "runtime_size", "runtime_fingerprint", "selector_source",
        "table_stride", "selector_field_offset", "selected_version", "evidence",
    )
    for row in csv.DictReader(rows_without_comments(path), delimiter="\t",
                              fieldnames=fields):
        result[row["key"]] = (row["selected_version"], row["evidence"])
    return result


def evidence_for(key: str, defaults: dict[str, tuple[str, str]]) -> dict[str, str]:
    evidence = {
        "enabled_direct_ops": (
            "submit16,memory,queue,suspend-primary,tf-ring,hs-offchip,async"
        ),
        "queue": "RE-exact-authenticated-0xc0408121-0xc00c810e-hardware-pending",
        "suspend_submit": "RE-exact-primary-hardware-pending-final-disabled",
        "suspend_query": "disabled-permission-export",
        "workload": "disabled-not-adapted",
        "tf_ring": "RE-exact-public-0x80108128-hardware-pending",
        "hs_offchip": "RE-exact-0xc010812c-hardware-pending",
        "memory": "RE-exact-standard-Trinity-hardware-pending",
        "defaults": "disabled-caller-selected-version-unknown",
        "async_graphics": "RE-exact-0x80048126-hardware-pending",
        "qualification": "RE-operation-carriers-hardware-pending",
    }
    if key not in TRINITY_KEYS:
        evidence["memory"] = "RE-exact-standard-hardware-pending"
    selected_version, selection_evidence = defaults[key]
    if selected_version != "unknown":
        evidence["enabled_direct_ops"] += f",defaults-v{selected_version}"
        evidence["defaults"] = (
            f"SPRX-qualified-v{selected_version}-{selection_evidence}"
        )
    if key == "0x0550":
        evidence.update({
            "enabled_direct_ops": (
                "submit16,memory,queue,suspend-primary,suspend-final,"
                "workload-extension,tf-ring,hs-offchip,defaults-v8,async,"
                "eop-flip"
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
                "tf-ring,hs-offchip,defaults-v12,async"
            ),
            "queue": "hardware-qualified-authenticated-0xc0408121-0xc00c810e",
            "suspend_submit": "hardware-qualified-primary-final",
            "workload": "disabled-nine-dword-Sony-ABI",
            "tf_ring": "hardware-qualified-public-0x80108128",
            "hs_offchip": "hardware-qualified-zero-entry-carrier-0xc010812c",
            "memory": "hardware-qualified-standard-RE-exact-Trinity",
            "defaults": "hardware-qualified-v12",
            "async_graphics": "hardware-qualified-0x80048126",
            "qualification": "hardware-qualified-standard-FW11.60",
        })
    return evidence


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profiles", type=Path,
        default=Path("analysis/agc_firmware_versions.tsv"))
    parser.add_argument("--fingerprints", type=Path,
        default=Path("analysis/agc_driver_wrapper_fingerprints.tsv"))
    parser.add_argument("--defaults", type=Path,
        default=Path("analysis/agc_register_defaults_facts.tsv"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    profiles = load_profiles(args.profiles)
    fingerprints = load_fingerprints(args.fingerprints)
    defaults = load_defaults(args.defaults)
    profile_keys = {profile["key"] for profile in profiles}
    if defaults.keys() != profile_keys:
        raise SystemExit("register-default facts do not cover every active profile")
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
                **evidence_for(key, defaults),
            }
            for operation, column in FINGERPRINT_COLUMNS.items():
                row[column] = fingerprints.get((key, operation), "MISSING")
            writer.writerow(row)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
