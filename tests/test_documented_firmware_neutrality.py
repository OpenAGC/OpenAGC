#!/usr/bin/env python3
"""Keep ordinary documented application code firmware-neutral."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
EXAMPLES = (
    ROOT / "examples/first_compute.c",
    ROOT / "examples/first_triangle.c",
)
BANNED = (
    "firmware_version",
    "firmware_abi_key",
    "profile_name",
    "OPENAGC_PROSPERO",
    "__PROSPERO__",
    "sceAgc",
    "agcPm4",
    "/dev/gc",
)


def main() -> int:
    failures: list[str] = []
    for path in sorted(DOCS.glob("*.md")):
        text = path.read_text(encoding="utf-8")
        for block_index, block in enumerate(
            re.findall(r"```(?:c|C)\s*\n(.*?)```", text, re.DOTALL), 1
        ):
            for token in BANNED:
                if token in block:
                    failures.append(
                        f"{path.relative_to(ROOT)} C block {block_index}: {token}"
                    )
    for path in EXAMPLES:
        text = path.read_text(encoding="utf-8")
        for token in BANNED:
            if token in text:
                failures.append(f"{path.relative_to(ROOT)}: {token}")
    if failures:
        print("firmware-specific ordinary application code:")
        print("\n".join(f"  {failure}" for failure in failures))
        return 1
    print("documented application code is firmware-neutral")
    return 0


if __name__ == "__main__":
    sys.exit(main())
