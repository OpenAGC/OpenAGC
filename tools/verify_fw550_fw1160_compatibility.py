#!/usr/bin/env python3
"""Lock the FW 5.50/FW 11.60 direct-driver compatibility boundary."""

from __future__ import annotations

import csv
from pathlib import Path


ENDPOINTS = ("0x0550", "0x1160")

# Provenance addresses, normalized instruction fingerprints, and qualification
# labels may differ without changing the /dev/gc request ABI. Everything else
# is either required to be identical or listed explicitly as a policy/layout
# difference below.
IGNORED = {
    "sprx_relative_path",
    "libSceAgc_relative_path",
    "qualification",
}

EXPECTED_DIFFERENCES = {
    "analysis/agc_driver_operation_facts.tsv": {
        "enabled_direct_ops", "queue", "workload", "hs_offchip", "memory",
        "defaults", "submit_acb_group", "workload_active_group",
        "workload_complete_group", "tf_public_group", "hs_public_group",
        "defaults_group", "async_group",
    },
    "analysis/agc_driver_submission_facts.tsv": {
        "acb_fingerprint", "acb_size_bytes",
    },
    "analysis/agc_driver_queue_facts.tsv": {
        "setup_carrier", "instruction_count",
    },
    "analysis/agc_driver_memory_facts.tsv": {
        "allocation_carrier", "instruction_count", "gpu_info_trinity",
        "cwsr_trinity", "cwsr_work_trinity", "trinity_predicate",
    },
    "analysis/agc_driver_suspend_facts.tsv": {
        "primary_status", "final_carriers", "final_status",
        "query_internal_carrier",
    },
    "analysis/agc_driver_workload_facts.tsv": {
        "size_helper_fingerprint", "active_builder_fingerprint",
        "active_builder_bytes", "complete_builder_fingerprint",
        "complete_builder_bytes", "openagc_status",
    },
    "analysis/agc_driver_ring_facts.tsv": {
        "tf_carrier_fingerprint", "tf_instructions",
    },
    "analysis/agc_register_defaults_facts.tsv": {
        "versioned_vaddr", "versioned_size", "versioned_fingerprint",
        "max_dispatch_version", "runtime_vaddr",
        "internal_versioned_vaddr", "internal_versioned_size",
        "internal_versioned_fingerprint", "internal_max_dispatch_version",
        "internal_runtime_vaddr",
    },
    "analysis/agc_driver_shadow_facts.tsv": {
        "allocation_carrier", "allocation_insns", "allocation_vaddr",
        "layout_file_offset", "layout_vaddr", "words_file_offset",
        "words_vaddr", "constructor_carrier", "constructor_insns",
        "constructor_vaddr", "property_carrier", "property_plt",
        "gn2_call", "gn3_call", "gn4_call", "trinity_predicate",
    },
}

COMMON_BASELINE_OPS = {
    "submit16", "memory", "queue", "suspend-primary", "suspend-final",
    "tf-ring", "hs-offchip", "defaults-caller-selected", "async",
}


def read_endpoints(path: Path) -> tuple[list[str], dict[str, str], dict[str, str]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    header = lines[0].removeprefix("# ").split("\t")
    rows = {
        row[header[0]]: row
        for row in csv.DictReader(lines[1:], delimiter="\t", fieldnames=header)
    }
    missing = set(ENDPOINTS) - rows.keys()
    if missing:
        raise SystemExit(f"{path}: missing endpoint rows {sorted(missing)}")
    return header, rows[ENDPOINTS[0]], rows[ENDPOINTS[1]]


def main() -> int:
    for filename, expected_differences in EXPECTED_DIFFERENCES.items():
        path = Path(filename)
        header, fw550, fw1160 = read_endpoints(path)
        actual_differences = {
            field for field in header
            if field not in IGNORED and field != header[0]
            and fw550[field] != fw1160[field]
        }
        if actual_differences != expected_differences:
            raise SystemExit(
                f"{path}: endpoint difference set changed: "
                f"missing={sorted(expected_differences - actual_differences)}, "
                f"unexpected={sorted(actual_differences - expected_differences)}"
            )

    _, fw550_ops, fw1160_ops = read_endpoints(
        Path("analysis/agc_driver_operation_facts.tsv")
    )
    for key, row in zip(ENDPOINTS, (fw550_ops, fw1160_ops)):
        enabled = set(row["enabled_direct_ops"].split(","))
        missing = COMMON_BASELINE_OPS - enabled
        if missing:
            raise SystemExit(f"{key}: missing baseline operations {sorted(missing)}")

    fw550_only = set(fw550_ops["enabled_direct_ops"].split(",")) - set(
        fw1160_ops["enabled_direct_ops"].split(",")
    )
    fw1160_only = set(fw1160_ops["enabled_direct_ops"].split(",")) - set(
        fw550_ops["enabled_direct_ops"].split(",")
    )
    if fw550_only != {"workload-extension", "eop-flip"} or fw1160_only:
        raise SystemExit(
            "endpoint optional-operation boundary changed: "
            f"FW5.50-only={sorted(fw550_only)}, FW11.60-only={sorted(fw1160_only)}"
        )

    print(
        "PASS: FW 5.50 and FW 11.60 share the baseline /dev/gc ABI; "
        "all mechanical differences remain explicitly classified"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
