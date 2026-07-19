#!/usr/bin/env python3
"""
convert_register_defaults.py — Convert KytyPS5 agcRegisterDefaults.inc
into openagc register_defaults_v{N}.c source files.

The KytyPS5 CompactRegisterDefaults format uses:
  - tbl[0..3]_regs: arrays of {offset, value} pairs (ShaderRegister)
  - tbl[0..3]_pointer_offsets: uint16 indices into the regs arrays
  - types[]: uint32 triplets {hash, packed_index, reserved}
  - count: number of type entries (= number of groups)

The openagc AgcRegisterDefaultsGroup format uses:
  - space: 0=CX(tbl0), 1=SH(tbl1), 2=UC(tbl2 or tbl3)
  - index: pointer index within the table
  - type_hash: from the types array
  - register_count: derived from consecutive pointer offsets
  - registers: pointer to the register array

Mapping:
  - tbl0 → space 0 (CX)
  - tbl1 → space 1 (SH)
  - tbl2 → space 2 (UC), but often null
  - tbl3 → space 2 (UC), used for internal regs

The packed_index in types[] encodes:
  bits 0..1: table space (0=tbl0, 1=tbl1, 2=tbl2, 3=tbl3)
  bits 2..9: pointer index within that table
  bits 10+: flags (we ignore these for the group definition)

Register count per group = ptrs[idx+1] - ptrs[idx] (or regs_count - ptrs[idx] for last)
"""

import re
import sys
import os
from pathlib import Path

# Version mapping from KytyPS5
VERSION_MAP = {
    0: 0, 1: 0, 2: 0, 3: 0,
    4: 4, 5: 5, 6: 5,
    7: 7, 8: 8, 9: 9,
    10: 10, 11: 11, 12: 10,
}
FALLBACK_VERSION = 11
MAX_VERSION = 12

SPDX_HEADER = """\
/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
"""


class CompactRegisterDefaults:
    def __init__(self):
        self.tbl_regs = [None, None, None, None]  # list of [(offset, value), ...]
        self.tbl_reg_counts = [0, 0, 0, 0]
        self.tbl_ptrs = [None, None, None, None]  # list of [uint16, ...]
        self.tbl_ptr_counts = [0, 0, 0, 0]
        self.types = []  # list of (hash, packed_index, reserved)
        self.count = 0


def parse_inc(filepath):
    """Parse agcRegisterDefaults.inc and return dict of version -> (public, internal) CompactRegisterDefaults."""
    with open(filepath, 'r') as f:
        content = f.read()

    # Parse all static arrays
    # Pattern: static ShaderRegister g_agc_{public|internal}_reg_defaults_v{N}_tbl{T}_regs[] = { ... };
    reg_pattern = re.compile(
        r'static\s+ShaderRegister\s+'
        r'(g_agc_(?:public|internal)_reg_defaults_v(\d+)_tbl(\d+)_regs)\[\]\s*=\s*\{(.*?)\};',
        re.DOTALL
    )
    ptr_pattern = re.compile(
        r'static\s+const\s+uint16_t\s+'
        r'(g_agc_(?:public|internal)_reg_defaults_v(\d+)_tbl(\d+)_ptrs)\[\]\s*=\s*\{(.*?)\};',
        re.DOTALL
    )
    types_pattern = re.compile(
        r'static\s+uint32_t\s+'
        r'(g_agc_(?:public|internal)_reg_defaults_v(\d+)_types)\[\]\s*=\s*\{(.*?)\};',
        re.DOTALL
    )
    compact_pattern = re.compile(
        r'static\s+CompactRegisterDefaults\s+'
        r'(g_agc_(public|internal)_reg_defaults_v(\d+))\s*=\s*\{([^}]+)\};',
        re.DOTALL
    )

    # Storage: {(public/internal, version): CompactRegisterDefaults}
    data = {}

    def get_crd(kind, version):
        key = (kind, version)
        if key not in data:
            data[key] = CompactRegisterDefaults()
        return data[key]

    # Parse register arrays
    for m in reg_pattern.finditer(content):
        name, version, tbl = m.group(1), int(m.group(2)), int(m.group(3))
        kind = 'public' if 'public' in name else 'internal'
        regs_text = m.group(4)
        regs = []
        for pair in re.finditer(r'\{0x([0-9a-fA-F]+),\s*0x([0-9a-fA-F]+)\}', regs_text):
            regs.append((int(pair.group(1), 16), int(pair.group(2), 16)))
        crd = get_crd(kind, version)
        crd.tbl_regs[tbl] = regs
        crd.tbl_reg_counts[tbl] = len(regs)

    # Parse pointer arrays
    for m in ptr_pattern.finditer(content):
        name, version, tbl = m.group(1), int(m.group(2)), int(m.group(3))
        kind = 'public' if 'public' in name else 'internal'
        ptrs_text = m.group(4)
        ptrs = []
        for p in re.finditer(r'0x([0-9a-fA-F]+)', ptrs_text):
            ptrs.append(int(p.group(1), 16))
        crd = get_crd(kind, version)
        crd.tbl_ptrs[tbl] = ptrs
        crd.tbl_ptr_counts[tbl] = len(ptrs)

    # Parse types arrays
    for m in types_pattern.finditer(content):
        name, version = m.group(1), int(m.group(2))
        kind = 'public' if 'public' in name else 'internal'
        types_text = m.group(3)
        # Re-parse properly
        vals = []
        for x in re.finditer(r'0x([0-9a-fA-F]+)', types_text):
            vals.append(int(x.group(1), 16))
        # Group into triplets
        types = []
        for i in range(0, len(vals) - 2, 3):
            types.append((vals[i], vals[i+1], vals[i+2]))
        crd = get_crd(kind, version)
        crd.types = types
        crd.count = len(types)

    return data


def crd_to_groups(crd):
    """Convert CompactRegisterDefaults to list of AgcRegisterDefaultsGroup dicts."""
    groups = []
    for i, (type_hash, packed_index, reserved) in enumerate(crd.types):
        space_idx = packed_index & 0x3
        ptr_idx = (packed_index >> 2) & 0xFF

        # Map space: 0=tbl0(CX), 1=tbl1(SH), 2=tbl2(UC), 3=tbl3(UC)
        if space_idx == 0:
            space = 0  # CX
        elif space_idx == 1:
            space = 1  # SH
        else:
            space = 2  # UC

        # Get register count from pointer offsets
        ptrs = crd.tbl_ptrs[space_idx]
        regs = crd.tbl_regs[space_idx]
        if ptrs is None or regs is None:
            reg_count = 0
            reg_start = 0
        else:
            reg_start = ptrs[ptr_idx] if ptr_idx < len(ptrs) else 0
            if ptr_idx + 1 < len(ptrs):
                reg_count = ptrs[ptr_idx + 1] - reg_start
            else:
                reg_count = len(regs) - reg_start

        groups.append({
            'space': space,
            'space_idx': space_idx,
            'index': ptr_idx,
            'type_hash': type_hash,
            'register_count': reg_count,
            'reg_start': reg_start,
            'regs': regs[reg_start:reg_start + reg_count] if regs else [],
        })

    return groups


def generate_c_file(version, public_groups, internal_groups):
    """Generate a register_defaults_v{version}.c file."""
    lines = []
    lines.append(SPDX_HEADER)
    lines.append(f"/* register_defaults_v{version}.c — Version {version} register defaults")
    lines.append(f" * Auto-generated from reference agcRegisterDefaults.inc")
    lines.append(f" * {len(public_groups)} public groups, {len(internal_groups)} internal groups */")
    lines.append("")
    lines.append('#include "agc_context.h"')
    lines.append("")
    lines.append('#include <stdint.h>')
    lines.append("")

    # Generate register arrays for each group
    def emit_regs(prefix, groups):
        for i, g in enumerate(groups):
            if g['register_count'] == 0:
                continue
            lines.append(f"static const AgcRegisterDefaultValue {prefix}_regs_{i}[] = {{")
            for offset, value in g['regs']:
                lines.append(f"    {{0x{offset:04x}, 0x{value:08x}}},")
            lines.append("};")
            lines.append("")

    # Generate group arrays
    def emit_groups(prefix, groups):
        space_names = {0: 'kAgcRegisterDefaultSpaceCx', 1: 'kAgcRegisterDefaultSpaceSh', 2: 'kAgcRegisterDefaultSpaceUc'}
        lines.append(f"static const AgcRegisterDefaultsGroup {prefix}_defaults[] = {{")
        for i, g in enumerate(groups):
            space_name = space_names[g['space']]
            if g['register_count'] == 0:
                lines.append(f"    {{{space_name}, {g['index']}, 0x{g['type_hash']:08x}u, 0, 0}},")
            else:
                lines.append(f"    {{{space_name}, {g['index']}, 0x{g['type_hash']:08x}u, {g['register_count']}, {prefix}_regs_{i}}},")
        lines.append("};")
        lines.append("")

    # Public
    emit_regs(f"s_regdef_v{version}_primary", public_groups)
    emit_groups(f"s_regdef_v{version}_primary", public_groups)

    # Internal
    emit_regs(f"s_regdef_v{version}_internal", internal_groups)
    emit_groups(f"s_regdef_v{version}_internal", internal_groups)

    # Accessor functions
    lines.append(f"const AgcRegisterDefaultsGroup *agcRegisterDefaultsV{version}GetPrimaryGroups(uint32_t *out_count) {{")
    lines.append(f"    if (out_count)")
    lines.append(f"        *out_count = (uint32_t)(sizeof(s_regdef_v{version}_primary_defaults) /")
    lines.append(f"                               sizeof(s_regdef_v{version}_primary_defaults[0]));")
    lines.append(f"    return s_regdef_v{version}_primary_defaults;")
    lines.append("}")
    lines.append("")
    lines.append(f"const AgcRegisterDefaultsGroup *agcRegisterDefaultsV{version}GetInternalGroups(uint32_t *out_count) {{")
    lines.append(f"    if (out_count)")
    lines.append(f"        *out_count = (uint32_t)(sizeof(s_regdef_v{version}_internal_defaults) /")
    lines.append(f"                               sizeof(s_regdef_v{version}_internal_defaults[0]));")
    lines.append(f"    return s_regdef_v{version}_internal_defaults;")
    lines.append("}")
    lines.append("")

    return '\n'.join(lines)


def main():
    inc_path = sys.argv[1] if len(sys.argv) > 1 else \
        '/Users/bizkut/Downloads/PS5/homebrew/KytyPS5/src/libs/agcRegisterDefaults.inc'
    out_dir = sys.argv[2] if len(sys.argv) > 2 else \
        '/Users/bizkut/Downloads/PS5/homebrew/ps4-freegnm/OpenAGC/src'

    data = parse_inc(inc_path)

    # Get unique versions
    versions = sorted(set(v for (_, v) in data.keys()))
    print(f"Found versions: {versions}")

    for version in versions:
        public_crd = data.get(('public', version))
        internal_crd = data.get(('internal', version))
        if public_crd is None or internal_crd is None:
            print(f"  Skipping v{version}: missing data")
            continue

        public_groups = crd_to_groups(public_crd)
        internal_groups = crd_to_groups(internal_crd)

        c_content = generate_c_file(version, public_groups, internal_groups)
        out_path = os.path.join(out_dir, f'register_defaults_v{version}.c')
        with open(out_path, 'w') as f:
            f.write(c_content)
        print(f"  Generated {out_path}: {len(public_groups)} public, {len(internal_groups)} internal groups")

    # Generate the version selection header/source
    generate_version_selector(out_dir, versions)


def generate_version_selector(out_dir, versions):
    """Generate register_defaults_versions.c with version selection logic."""
    lines = []
    lines.append(SPDX_HEADER)
    lines.append("/* register_defaults_versions.c — Version selection for register defaults")
    lines.append(" * Auto-generated from reference agcRegisterDefaults.inc */")
    lines.append("")
    lines.append('#include "agc_context.h"')
    lines.append("")
    lines.append('#include <stdint.h>')
    lines.append("")

    # Forward declarations for all version accessors
    for v in versions:
        lines.append(f"const AgcRegisterDefaultsGroup *agcRegisterDefaultsV{v}GetPrimaryGroups(uint32_t *out_count);")
        lines.append(f"const AgcRegisterDefaultsGroup *agcRegisterDefaultsV{v}GetInternalGroups(uint32_t *out_count);")
    lines.append("")

    # Version mapping table
    lines.append(f"#define AGC_REGISTER_DEFAULTS_MAX_VERSION   {MAX_VERSION}")
    lines.append(f"#define AGC_REGISTER_DEFAULTS_FALLBACK_VERSION  {FALLBACK_VERSION}")
    lines.append("")

    # Generate function pointer arrays
    lines.append("typedef const AgcRegisterDefaultsGroup *(*GetGroupsFunc)(uint32_t *);")
    lines.append("")
    lines.append("static GetGroupsFunc s_primary_getters[] = {")
    for i in range(MAX_VERSION + 1):
        mapped = VERSION_MAP.get(i, FALLBACK_VERSION)
        lines.append(f"    agcRegisterDefaultsV{mapped}GetPrimaryGroups,  /* version {i} → v{mapped} */")
    lines.append("};")
    lines.append("")
    lines.append("static GetGroupsFunc s_internal_getters[] = {")
    for i in range(MAX_VERSION + 1):
        mapped = VERSION_MAP.get(i, FALLBACK_VERSION)
        lines.append(f"    agcRegisterDefaultsV{mapped}GetInternalGroups,  /* version {i} → v{mapped} */")
    lines.append("};")
    lines.append("")

    # Version selection function
    lines.append("const AgcRegisterDefaultsGroup *agcRegisterDefaultsGetPrimaryGroupsForVersion(")
    lines.append("    uint32_t version, uint32_t *out_count) {")
    lines.append(f"    if (version > AGC_REGISTER_DEFAULTS_MAX_VERSION)")
    lines.append(f"        version = AGC_REGISTER_DEFAULTS_FALLBACK_VERSION;")
    lines.append("    return s_primary_getters[version](out_count);")
    lines.append("}")
    lines.append("")
    lines.append("const AgcRegisterDefaultsGroup *agcRegisterDefaultsGetInternalGroupsForVersion(")
    lines.append("    uint32_t version, uint32_t *out_count) {")
    lines.append(f"    if (version > AGC_REGISTER_DEFAULTS_MAX_VERSION)")
    lines.append(f"        version = AGC_REGISTER_DEFAULTS_FALLBACK_VERSION;")
    lines.append("    return s_internal_getters[version](out_count);")
    lines.append("}")
    lines.append("")

    out_path = os.path.join(out_dir, 'register_defaults_versions.c')
    with open(out_path, 'w') as f:
        f.write('\n'.join(lines))
    print(f"  Generated {out_path}")


if __name__ == '__main__':
    main()
