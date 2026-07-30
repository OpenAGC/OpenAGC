#!/usr/bin/env python3
"""Extract exact linear-registration branches from active VideoOut SPRX files."""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import struct
from dataclasses import dataclass
from pathlib import Path


REGISTER_BUFFERS_NID = "w3BY+tAEiQY"
LINEAR_REJECTION_ERROR = (0x80290007).to_bytes(4, "little")

# The internal registration validator reads two firmware-state fields, with a
# 20-byte guard between them, then conditionally enters the linear rejection.
# Field offsets and the branch displacement evolve, so they are captured rather
# than fixed. The complete branch instruction is retained as runtime evidence.
LINEAR_GUARD_RE = re.compile(
    b"\x48\x8b\x85...."
    b"\x83\xb8(?P<first>....)\x00\x75\x14"
    b"\x48\x8b\x85...."
    b"\x83\xb8(?P<second>....)\x00"
    b"\x0f\x84(?P<relative>....)",
    re.DOTALL,
)


@dataclass(frozen=True)
class LoadSegment:
    file_offset: int
    virtual_address: int
    file_size: int

    def file_to_virtual(self, offset: int) -> int:
        if not self.file_offset <= offset < self.file_offset + self.file_size:
            raise ValueError("file offset is outside executable segment")
        return self.virtual_address + offset - self.file_offset

    def virtual_to_file(self, address: int) -> int:
        if not self.virtual_address <= address < self.virtual_address + self.file_size:
            raise ValueError("virtual address is outside executable segment")
        return self.file_offset + address - self.virtual_address


def rows_without_comments(path: Path):
    with path.open(encoding="utf-8", newline="") as source:
        yield from (line for line in source if not line.startswith("#"))


def load_profiles(path: Path, firmware_root: Path) -> list[tuple[str, Path, str]]:
    fields = ("key", "profile", "family", "manifest", "sprx", "status")
    profiles = []
    for row in csv.DictReader(rows_without_comments(path), delimiter="\t", fieldnames=fields):
        driver_path = Path(row["sprx"])
        videoout_path = driver_path.with_name("libSceVideoOut.sprx")
        profiles.append((
            row["key"], firmware_root / videoout_path, videoout_path.as_posix()
        ))
    return profiles


def executable_segment(image: bytes) -> LoadSegment:
    if image[:4] != b"\x7fELF" or image[4] != 2 or image[5] != 1:
        raise ValueError("not a little-endian ELF64 image")
    program_offset = struct.unpack_from("<Q", image, 0x20)[0]
    program_size = struct.unpack_from("<H", image, 0x36)[0]
    program_count = struct.unpack_from("<H", image, 0x38)[0]
    executable = []
    for index in range(program_count):
        offset = program_offset + index * program_size
        if offset + 56 > len(image):
            raise ValueError("truncated program header table")
        kind, flags, file_offset, virtual_address, _, file_size, _, _ = \
            struct.unpack_from("<IIQQQQQQ", image, offset)
        if kind == 1 and flags & 1 and file_size:
            executable.append(LoadSegment(file_offset, virtual_address, file_size))
    if len(executable) != 1:
        raise ValueError(f"expected one executable LOAD segment, found {len(executable)}")
    return executable[0]


def extract_patch(image: bytes, segment: LoadSegment) -> tuple[int, bytes, int, int]:
    code = image[segment.file_offset:segment.file_offset + segment.file_size]
    candidates = []
    for match in LINEAR_GUARD_RE.finditer(code):
        patch_file_offset = segment.file_offset + match.start() + 30
        patch_address = segment.file_to_virtual(patch_file_offset)
        relative = struct.unpack("<i", match.group("relative"))[0]
        target_address = patch_address + 6 + relative
        try:
            target_file_offset = segment.virtual_to_file(target_address)
        except ValueError:
            continue
        target = image[target_file_offset:target_file_offset + 96]
        stack_mode_compare = (
            len(target) >= 7 and target[:2] == b"\x83\xbd" and target[6] == 2
        )
        register_mode_compare = target[:4] in (
            b"\x41\x83\xfe\x02",  # cmp r14d, 2
            b"\x41\x83\xff\x02",  # cmp r15d, 2
        )
        if not stack_mode_compare and not register_mode_compare:
            continue
        if LINEAR_REJECTION_ERROR not in target:
            continue
        first_field = struct.unpack("<I", match.group("first"))[0]
        second_field = struct.unpack("<I", match.group("second"))[0]
        signature = image[patch_file_offset:patch_file_offset + 6]
        candidates.append((patch_address, signature, first_field, second_field))
    if len(candidates) != 1:
        raise ValueError(f"expected one verified linear guard, found {len(candidates)}")
    return candidates[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_root", type=Path)
    parser.add_argument("--profiles", type=Path,
        default=Path("analysis/agc_firmware_versions.tsv"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    rows = []
    for key, sprx, relative_path in load_profiles(args.profiles, args.firmware_root):
        if not sprx.is_file():
            raise SystemExit(f"missing libSceVideoOut for {key}: {sprx}")
        image = sprx.read_bytes()
        try:
            patch_address, signature, first_field, second_field = extract_patch(
                image, executable_segment(image))
        except ValueError as error:
            raise SystemExit(f"{key}: {error}") from error
        rows.append((
            key,
            relative_path,
            hashlib.sha256(image).hexdigest(),
            REGISTER_BUFFERS_NID,
            f"0x{first_field:x}",
            f"0x{second_field:x}",
            f"0x{patch_address:x}",
            signature.hex(),
            "linear-rejection-0x80290007",
        ))

    header = (
        "# firmware_abi_key\tlibSceVideoOut_relative_path\tsprx_sha256\t"
        "register_buffers_nid\tfirst_guard_field\tsecond_guard_field\t"
        "patch_vaddr\toriginal_bytes\tverification"
    )
    text = header + "\n" + "\n".join("\t".join(row) for row in rows) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
