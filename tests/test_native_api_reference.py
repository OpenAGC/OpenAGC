#!/usr/bin/env python3
"""Require the installed native API reference to index every public symbol."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
HEADERS = (
    ROOT / "include/openagc/runtime.h",
    ROOT / "include/openagc/capture.h",
    ROOT / "include/openagc/shader_reflection.h",
)
FUNCTION_HEADERS = HEADERS + (ROOT / "include/agc_error.h",)
REFERENCE = ROOT / "docs/api_reference.md"


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//.*", "", text)


def public_types() -> set[str]:
    names: set[str] = set()
    for header in HEADERS:
        text = strip_comments(header.read_text(encoding="utf-8"))
        names.update(re.findall(r"}\s*(Agc[A-Za-z0-9_]+)\s*;", text))
        names.update(re.findall(
            r"typedef\s+struct\s+[A-Za-z0-9_]+\s*\*\s*"
            r"(Agc[A-Za-z0-9_]+)\s*;",
            text,
        ))
        names.update(re.findall(
            r"typedef\s+u?int(?:32|64)_t\s+(Agc[A-Za-z0-9_]+)\s*;",
            text,
        ))
        names.update(re.findall(
            r"\(\s*PS5_SYSV_ABI\s*\*\s*(Agc[A-Za-z0-9_]+)\s*\)",
            text,
        ))
    # Shared agc_types.h enums used directly by the native contract.
    names.update(("AgcIndexSize", "AgcQueueType"))
    return names


def public_functions() -> set[str]:
    names: set[str] = set()
    for header in FUNCTION_HEADERS:
        text = strip_comments(header.read_text(encoding="utf-8"))
        names.update(re.findall(r"\b(agc[A-Z][A-Za-z0-9_]+)\s*\(", text))
    return names


def main() -> int:
    reference = REFERENCE.read_text(encoding="utf-8")
    missing_types = sorted(
        name for name in public_types() if f"`{name}`" not in reference
    )
    missing_functions = sorted(
        name for name in public_functions() if f"`{name}`" not in reference
    )
    required_sections = (
        "## Ownership and lifetime",
        "## Thread safety",
        "## Return values",
        "## Type index",
        "## Function reference",
        "## Examples",
    )
    missing_sections = [
        section for section in required_sections if section not in reference
    ]
    if missing_types or missing_functions or missing_sections:
        if missing_sections:
            print("missing sections:", ", ".join(missing_sections))
        if missing_types:
            print("missing types:", ", ".join(missing_types))
        if missing_functions:
            print("missing functions:", ", ".join(missing_functions))
        return 1
    print(
        f"native API reference: {len(public_types())} types, "
        f"{len(public_functions())} functions"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
