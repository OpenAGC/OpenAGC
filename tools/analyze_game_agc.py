#!/usr/bin/env python3
"""Extract AGC imports from a decrypted PS5 ELF without section headers."""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import struct
import sys
from pathlib import Path


PT_LOAD = 1
PT_DYNAMIC = 2
PT_SCE_DYNLIBDATA = 0x61000000
DT_NULL = 0
DT_STRTAB = 5
DT_SYMTAB = 6
DT_SYMENT = 11
DT_SCE_IMPORT_LIB = 0x61000049
DT_SCE_STRTAB = 0x61000035
DT_SCE_SYMTAB = 0x61000039
DT_SCE_SYMTABSZ = 0x6100003F
ELF64_SYM_SIZE = 24
NID_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-"
VERSIONED_NAME = re.compile(r"_[0-9]{4}$")
PUBLIC_DECL = re.compile(r"\b(sceAgc(?:Driver)?[A-Za-z0-9_]+)\s*\(")


class ElfError(ValueError):
    pass


def read_c_string(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        raise ElfError(f"string offset 0x{offset:x} is outside the file")
    end = data.find(b"\0", offset)
    if end < 0:
        raise ElfError(f"unterminated string at file offset 0x{offset:x}")
    return data[offset:end].decode("ascii", errors="replace")


def decode_sce_id(text: str) -> int:
    value = 0
    for char in text:
        try:
            digit = NID_ALPHABET.index(char)
        except ValueError as exc:
            raise ElfError(f"invalid SCE identifier character {char!r}") from exc
        value = (value << 6) | digit
    return value


def load_known_nids(path: Path) -> dict[str, tuple[str, str]]:
    result: dict[str, tuple[str, str]] = {}
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.reader(stream, delimiter="\t"):
            if len(row) >= 3 and row[0].startswith("libSceAgc"):
                result[row[2]] = (row[0], row[1])
    return result


def load_variant_nids(path: Path) -> dict[str, tuple[str, str]]:
    result: dict[str, tuple[str, str]] = {}
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.reader(stream, delimiter="\t"):
            if len(row) >= 4 and row[1].startswith("libSceAgc"):
                result[row[3]] = (row[1], row[2])
    return result


def load_public_declarations(include_dir: Path) -> set[str]:
    declarations: set[str] = set()
    for header in include_dir.glob("*.h"):
        declarations.update(PUBLIC_DECL.findall(header.read_text(encoding="utf-8")))
    return declarations


def classify(name: str, declarations: set[str]) -> str:
    if "Unknown_" in name:
        return "unresolved"
    if name not in declarations:
        return "unresolved"
    if VERSIONED_NAME.search(name):
        return "forwarding-wrapper"
    return "implemented"


def parse_imports(data: bytes, known: dict[str, tuple[str, str]],
                  declarations: set[str]) -> list[tuple[str, str, str, str, str]]:
    if len(data) < 64 or data[:4] != b"\x7fELF":
        raise ElfError("not an ELF file")
    if data[4] != 2 or data[5] != 1:
        raise ElfError("only little-endian ELF64 images are supported")

    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data, 0)
    phoff, phentsize, phnum = header[5], header[9], header[10]
    if phentsize < 56 or phoff + phentsize * phnum > len(data):
        raise ElfError("invalid program-header table")

    segments = [
        struct.unpack_from("<IIQQQQQQ", data, phoff + index * phentsize)
        for index in range(phnum)
    ]

    def virtual_to_file(value: int) -> int:
        for p_type, _flags, p_offset, p_vaddr, _paddr, p_filesz, _memsz, _align in segments:
            if p_type == PT_LOAD and p_vaddr <= value < p_vaddr + p_filesz:
                return p_offset + value - p_vaddr
        dynlib = next((segment for segment in segments
                       if segment[0] == PT_SCE_DYNLIBDATA), None)
        if dynlib is not None and value < dynlib[5]:
            return dynlib[2] + value
        raise ElfError(f"virtual address 0x{value:x} is not file-backed")

    dynamic = next((segment for segment in segments if segment[0] == PT_DYNAMIC), None)
    if dynamic is None:
        raise ElfError("ELF has no PT_DYNAMIC segment")

    entries: list[tuple[int, int]] = []
    for offset in range(dynamic[2], dynamic[2] + dynamic[5], 16):
        tag, value = struct.unpack_from("<qQ", data, offset)
        entries.append((tag, value))
        if tag == DT_NULL:
            break
    tags = dict(entries)

    str_value = tags.get(DT_STRTAB, tags.get(DT_SCE_STRTAB))
    sym_value = tags.get(DT_SYMTAB, tags.get(DT_SCE_SYMTAB))
    sym_size = tags.get(DT_SCE_SYMTABSZ)
    sym_ent = tags.get(DT_SYMENT, ELF64_SYM_SIZE)
    if str_value is None or sym_value is None or sym_size is None:
        raise ElfError("missing dynamic string/symbol table metadata")
    if sym_ent < ELF64_SYM_SIZE or sym_size % sym_ent != 0:
        raise ElfError("invalid dynamic symbol table size")

    strtab = virtual_to_file(str_value)
    symtab = virtual_to_file(sym_value)
    libraries: dict[int, str] = {}
    for tag, value in entries:
        if tag == DT_SCE_IMPORT_LIB:
            libraries[(value >> 48) & 0xFFFF] = read_c_string(
                data, strtab + (value & 0xFFFFFFFF))

    imports: set[tuple[str, str, str, str, str]] = set()
    for offset in range(symtab, symtab + sym_size, sym_ent):
        name_offset, _info, _other, section, _value, _size = struct.unpack_from(
            "<IBBHQQ", data, offset)
        if section != 0 or name_offset == 0:
            continue
        raw_name = read_c_string(data, strtab + name_offset)
        parts = raw_name.split("#")
        if len(parts) < 2:
            continue
        nid = parts[0]
        library = libraries.get(decode_sce_id(parts[1]), "")
        if library not in ("libSceAgc", "libSceAgcDriver"):
            continue
        mapped_library, name = known.get(
            nid, (library, f"{library.removeprefix('libSce')}Unknown_{nid}"))
        imports.add((mapped_library, name, nid, classify(name, declarations), raw_name))
    return sorted(imports)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--known", type=Path,
                        default=Path(__file__).resolve().parents[1] /
                        "analysis/agc_known_nids.tsv")
    parser.add_argument("--variants", type=Path,
                        default=Path(__file__).resolve().parents[1] /
                        "analysis/agc_nids_version_variants.tsv")
    parser.add_argument("--include-dir", type=Path,
                        default=Path(__file__).resolve().parents[1] / "include")
    parser.add_argument("--require-covered", action="store_true")
    args = parser.parse_args()

    data = args.binary.read_bytes()
    known = load_known_nids(args.known)
    for nid, mapped in load_variant_nids(args.variants).items():
        known.setdefault(nid, mapped)
    imports = parse_imports(data, known,
                            load_public_declarations(args.include_dir))
    writer = csv.writer(sys.stdout, delimiter="\t", lineterminator="\n")
    writer.writerow(("library", "function", "nid", "classification", "raw_symbol"))
    writer.writerows(imports)

    unresolved = sum(row[3] == "unresolved" for row in imports)
    digest = hashlib.sha256(data).hexdigest()
    print(f"{args.binary}: {len(imports)} AGC imports, {unresolved} unresolved, "
          f"sha256={digest}", file=sys.stderr)
    return 2 if args.require_covered and unresolved else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ElfError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
