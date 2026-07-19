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

/* register_defaults_v8.c — Version 8 register defaults
 * Auto-generated from reference agcRegisterDefaults.inc
 * 127 public groups, 22 internal groups */

#include "agc_context.h"

#include <stdint.h>

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_0[] = {
    {0x0202, 0x00cc0010},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_1[] = {
    {0x0109, 0x00000008},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_2[] = {
    {0x0104, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_3[] = {
    {0x008f, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_4[] = {
    {0x008e, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_5[] = {
    {0x02dc, 0x0000aa00},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_6[] = {
    {0x0001, 0x11000100},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_7[] = {
    {0x0200, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_8[] = {
    {0x0201, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_9[] = {
    {0x0000, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_10[] = {
    {0x0006, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_11[] = {
    {0x001f, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_12[] = {
    {0x0203, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_13[] = {
    {0x02b0, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_14[] = {
    {0x02b1, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_15[] = {
    {0x010c, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_16[] = {
    {0x010d, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_17[] = {
    {0x010b, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_18[] = {
    {0x01ff, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_19[] = {
    {0x0204, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_20[] = {
    {0x020d, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_21[] = {
    {0x0206, 0x0000043f},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_22[] = {
    {0x02f8, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_23[] = {
    {0x0083, 0x0000ffff},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_24[] = {
    {0x0313, 0x00006000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_25[] = {
    {0x00f0, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_26[] = {
    {0x00ea, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_27[] = {
    {0x00e9, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_28[] = {
    {0x0292, 0x00000002},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_29[] = {
    {0x0293, 0x06020000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_30[] = {
    {0x00e8, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_31[] = {
    {0x0080, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_32[] = {
    {0x0211, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_33[] = {
    {0x0210, 0x04000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_34[] = {
    {0x008d, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_35[] = {
    {0x0282, 0x00000008},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_36[] = {
    {0x0281, 0xffff0000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_37[] = {
    {0x0280, 0x00080008},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_38[] = {
    {0x02df, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_39[] = {
    {0x02de, 0x000001e9},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_40[] = {
    {0x0205, 0x00000240},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_41[] = {
    {0x020c, 0x00000021},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_42[] = {
    {0x02f9, 0x0000002d},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_43[] = {
    {0x01ba, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_44[] = {
    {0x02a6, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_45[] = {
    {0x02ce, 0x00000400},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_46[] = {
    {0x029b, 0x00000002},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_47[] = {
    {0x02d6, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_48[] = {
    {0x0103, 0xffffffff},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_49[] = {
    {0x02a1, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_50[] = {
    {0x02ad, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_51[] = {
    {0x02d5, 0x00002000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_52[] = {
    {0x02d4, 0x88101000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_53[] = {
    {0x02db, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_54[] = {
    {0x02f5, 0x00000000},
    {0x02f6, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_55[] = {
    {0x02fe, 0x00000000},
    {0x02ff, 0x00000000},
    {0x0300, 0x00000000},
    {0x0301, 0x00000000},
    {0x0302, 0x00000000},
    {0x0303, 0x00000000},
    {0x0304, 0x00000000},
    {0x0305, 0x00000000},
    {0x0306, 0x00000000},
    {0x0307, 0x00000000},
    {0x0308, 0x00000000},
    {0x0309, 0x00000000},
    {0x030a, 0x00000000},
    {0x030b, 0x00000000},
    {0x030c, 0x00000000},
    {0x030d, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_56[] = {
    {0x030e, 0xffffffff},
    {0x030f, 0xffffffff},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_57[] = {
    {0x0311, 0x01fd2002},
    {0x0312, 0x03ff0080},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_58[] = {
    {0x0105, 0x00000000},
    {0x0106, 0x00000000},
    {0x0107, 0x00000000},
    {0x0108, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_59[] = {
    {0x01e0, 0x20010001},
    {0x01e1, 0x20010001},
    {0x01e2, 0x20010001},
    {0x01e3, 0x20010001},
    {0x01e4, 0x20010001},
    {0x01e5, 0x20010001},
    {0x01e6, 0x20010001},
    {0x01e7, 0x20010001},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_60[] = {
    {0x0020, 0x00000000},
    {0x0021, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_61[] = {
    {0x0084, 0x00000000},
    {0x0085, 0x20002000},
    {0x0086, 0x00000000},
    {0x0087, 0x20002000},
    {0x0088, 0x00000000},
    {0x0089, 0x20002000},
    {0x008a, 0x00000000},
    {0x008b, 0x20002000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_62[] = {
    {0x00db, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_63[] = {
    {0x0008, 0x00000000},
    {0x0009, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_64[] = {
    {0x0010, 0x80000180},
    {0x0011, 0x20000180},
    {0x0012, 0x00000000},
    {0x0013, 0x00000000},
    {0x0014, 0x00000000},
    {0x0015, 0x00000000},
    {0x001a, 0x00000000},
    {0x001b, 0x00000000},
    {0x001c, 0x00000000},
    {0x001d, 0x00000000},
    {0x001e, 0x00000000},
    {0x0002, 0x00000000},
    {0x0005, 0x00000000},
    {0x0007, 0x00000000},
    {0x000b, 0x00000000},
    {0x000a, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_65[] = {
    {0x00eb, 0xff00ff00},
    {0x00ec, 0x00010000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_66[] = {
    {0x00f1, 0x00000000},
    {0x00f2, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_67[] = {
    {0x0090, 0x80000000},
    {0x0091, 0x40004000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_68[] = {
    {0x02fa, 0x3f800000},
    {0x02fb, 0x3f800000},
    {0x02fc, 0x3f800000},
    {0x02fd, 0x3f800000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_69[] = {
    {0x02e2, 0x00000000},
    {0x02e3, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_70[] = {
    {0x02e0, 0x00000000},
    {0x02e1, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_71[] = {
    {0x0003, 0x00000000},
    {0x0004, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_72[] = {
    {0x0318, 0x00000000},
    {0x031b, 0x00000000},
    {0x031c, 0x00000000},
    {0x031d, 0x00000000},
    {0x031e, 0x00000048},
    {0x031f, 0x00000000},
    {0x0321, 0x00000000},
    {0x0323, 0x00000000},
    {0x0324, 0x00000000},
    {0x0325, 0x00000000},
    {0x0390, 0x00000000},
    {0x0398, 0x00000000},
    {0x03a0, 0x00000000},
    {0x03a8, 0x00000000},
    {0x03b0, 0x00000000},
    {0x03b8, 0x08c6c000},
    {0x0327, 0x00000000},
    {0x032a, 0x00000000},
    {0x032b, 0x00000000},
    {0x032c, 0x00000000},
    {0x032d, 0x00000048},
    {0x032e, 0x00000000},
    {0x0330, 0x00000000},
    {0x0332, 0x00000000},
    {0x0333, 0x00000000},
    {0x0334, 0x00000000},
    {0x0391, 0x00000000},
    {0x0399, 0x00000000},
    {0x03a1, 0x00000000},
    {0x03a9, 0x00000000},
    {0x03b1, 0x00000000},
    {0x03b9, 0x08c6c000},
    {0x0336, 0x00000000},
    {0x0339, 0x00000000},
    {0x033a, 0x00000000},
    {0x033b, 0x00000000},
    {0x033c, 0x00000048},
    {0x033d, 0x00000000},
    {0x033f, 0x00000000},
    {0x0341, 0x00000000},
    {0x0342, 0x00000000},
    {0x0343, 0x00000000},
    {0x0392, 0x00000000},
    {0x039a, 0x00000000},
    {0x03a2, 0x00000000},
    {0x03aa, 0x00000000},
    {0x03b2, 0x00000000},
    {0x03ba, 0x08c6c000},
    {0x0345, 0x00000000},
    {0x0348, 0x00000000},
    {0x0349, 0x00000000},
    {0x034a, 0x00000000},
    {0x034b, 0x00000048},
    {0x034c, 0x00000000},
    {0x034e, 0x00000000},
    {0x0350, 0x00000000},
    {0x0351, 0x00000000},
    {0x0352, 0x00000000},
    {0x0393, 0x00000000},
    {0x039b, 0x00000000},
    {0x03a3, 0x00000000},
    {0x03ab, 0x00000000},
    {0x03b3, 0x00000000},
    {0x03bb, 0x08c6c000},
    {0x0354, 0x00000000},
    {0x0357, 0x00000000},
    {0x0358, 0x00000000},
    {0x0359, 0x00000000},
    {0x035a, 0x00000048},
    {0x035b, 0x00000000},
    {0x035d, 0x00000000},
    {0x035f, 0x00000000},
    {0x0360, 0x00000000},
    {0x0361, 0x00000000},
    {0x0394, 0x00000000},
    {0x039c, 0x00000000},
    {0x03a4, 0x00000000},
    {0x03ac, 0x00000000},
    {0x03b4, 0x00000000},
    {0x03bc, 0x08c6c000},
    {0x0363, 0x00000000},
    {0x0366, 0x00000000},
    {0x0367, 0x00000000},
    {0x0368, 0x00000000},
    {0x0369, 0x00000048},
    {0x036a, 0x00000000},
    {0x036c, 0x00000000},
    {0x036e, 0x00000000},
    {0x036f, 0x00000000},
    {0x0370, 0x00000000},
    {0x0395, 0x00000000},
    {0x039d, 0x00000000},
    {0x03a5, 0x00000000},
    {0x03ad, 0x00000000},
    {0x03b5, 0x00000000},
    {0x03bd, 0x08c6c000},
    {0x0372, 0x00000000},
    {0x0375, 0x00000000},
    {0x0376, 0x00000000},
    {0x0377, 0x00000000},
    {0x0378, 0x00000048},
    {0x0379, 0x00000000},
    {0x037b, 0x00000000},
    {0x037d, 0x00000000},
    {0x037e, 0x00000000},
    {0x037f, 0x00000000},
    {0x0396, 0x00000000},
    {0x039e, 0x00000000},
    {0x03a6, 0x00000000},
    {0x03ae, 0x00000000},
    {0x03b6, 0x00000000},
    {0x03be, 0x08c6c000},
    {0x0381, 0x00000000},
    {0x0384, 0x00000000},
    {0x0385, 0x00000000},
    {0x0386, 0x00000000},
    {0x0387, 0x00000048},
    {0x0388, 0x00000000},
    {0x038a, 0x00000000},
    {0x038c, 0x00000000},
    {0x038d, 0x00000000},
    {0x038e, 0x00000000},
    {0x0397, 0x00000000},
    {0x039f, 0x00000000},
    {0x03a7, 0x00000000},
    {0x03af, 0x00000000},
    {0x03b7, 0x00000000},
    {0x03bf, 0x08c6c000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_73[] = {
    {0x000c, 0x00000000},
    {0x000d, 0x40004000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_74[] = {
    {0x0191, 0x00000000},
    {0x0192, 0x00000000},
    {0x0193, 0x00000000},
    {0x0194, 0x00000000},
    {0x0195, 0x00000000},
    {0x0196, 0x00000000},
    {0x0197, 0x00000000},
    {0x0198, 0x00000000},
    {0x0199, 0x00000000},
    {0x019a, 0x00000000},
    {0x019b, 0x00000000},
    {0x019c, 0x00000000},
    {0x019d, 0x00000000},
    {0x019e, 0x00000000},
    {0x019f, 0x00000000},
    {0x01a0, 0x00000000},
    {0x01a1, 0x00000000},
    {0x01a2, 0x00000000},
    {0x01a3, 0x00000000},
    {0x01a4, 0x00000000},
    {0x01a5, 0x00000000},
    {0x01a6, 0x00000000},
    {0x01a7, 0x00000000},
    {0x01a8, 0x00000000},
    {0x01a9, 0x00000000},
    {0x01aa, 0x00000000},
    {0x01ab, 0x00000000},
    {0x01ac, 0x00000000},
    {0x01ad, 0x00000000},
    {0x01ae, 0x00000000},
    {0x01af, 0x00000000},
    {0x01b0, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_75[] = {
    {0x016f, 0x00000000},
    {0x0170, 0x00000000},
    {0x0171, 0x00000000},
    {0x0172, 0x00000000},
    {0x0173, 0x00000000},
    {0x0174, 0x00000000},
    {0x0175, 0x00000000},
    {0x0176, 0x00000000},
    {0x0177, 0x00000000},
    {0x0178, 0x00000000},
    {0x0179, 0x00000000},
    {0x017a, 0x00000000},
    {0x017b, 0x00000000},
    {0x017c, 0x00000000},
    {0x017d, 0x00000000},
    {0x017e, 0x00000000},
    {0x017f, 0x00000000},
    {0x0180, 0x00000000},
    {0x0181, 0x00000000},
    {0x0182, 0x00000000},
    {0x0183, 0x00000000},
    {0x0184, 0x00000000},
    {0x0185, 0x00000000},
    {0x0186, 0x00000000},
    {0x0187, 0x00000000},
    {0x0188, 0x00000000},
    {0x0189, 0x00000000},
    {0x018a, 0x00000000},
    {0x018b, 0x00000000},
    {0x018c, 0x00000000},
    {0x018d, 0x00000000},
    {0x018e, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_76[] = {
    {0x010f, 0x3f800000},
    {0x0111, 0x3f800000},
    {0x0113, 0x3f800000},
    {0x0110, 0x00000000},
    {0x0112, 0x00000000},
    {0x0114, 0x00000000},
    {0x0094, 0x80000000},
    {0x0095, 0x40004000},
    {0x00b4, 0x00000000},
    {0x00b5, 0x00000000},
    {0x0115, 0x3f800000},
    {0x0117, 0x3f800000},
    {0x0119, 0x3f800000},
    {0x0116, 0x00000000},
    {0x0118, 0x00000000},
    {0x011a, 0x00000000},
    {0x0096, 0x80000000},
    {0x0097, 0x40004000},
    {0x00b6, 0x00000000},
    {0x00b7, 0x00000000},
    {0x011b, 0x3f800000},
    {0x011d, 0x3f800000},
    {0x011f, 0x3f800000},
    {0x011c, 0x00000000},
    {0x011e, 0x00000000},
    {0x0120, 0x00000000},
    {0x0098, 0x80000000},
    {0x0099, 0x40004000},
    {0x00b8, 0x00000000},
    {0x00b9, 0x00000000},
    {0x0121, 0x3f800000},
    {0x0123, 0x3f800000},
    {0x0125, 0x3f800000},
    {0x0122, 0x00000000},
    {0x0124, 0x00000000},
    {0x0126, 0x00000000},
    {0x009a, 0x80000000},
    {0x009b, 0x40004000},
    {0x00ba, 0x00000000},
    {0x00bb, 0x00000000},
    {0x0127, 0x3f800000},
    {0x0129, 0x3f800000},
    {0x012b, 0x3f800000},
    {0x0128, 0x00000000},
    {0x012a, 0x00000000},
    {0x012c, 0x00000000},
    {0x009c, 0x80000000},
    {0x009d, 0x40004000},
    {0x00bc, 0x00000000},
    {0x00bd, 0x00000000},
    {0x012d, 0x3f800000},
    {0x012f, 0x3f800000},
    {0x0131, 0x3f800000},
    {0x012e, 0x00000000},
    {0x0130, 0x00000000},
    {0x0132, 0x00000000},
    {0x009e, 0x80000000},
    {0x009f, 0x40004000},
    {0x00be, 0x00000000},
    {0x00bf, 0x00000000},
    {0x0133, 0x3f800000},
    {0x0135, 0x3f800000},
    {0x0137, 0x3f800000},
    {0x0134, 0x00000000},
    {0x0136, 0x00000000},
    {0x0138, 0x00000000},
    {0x00a0, 0x80000000},
    {0x00a1, 0x40004000},
    {0x00c0, 0x00000000},
    {0x00c1, 0x00000000},
    {0x0139, 0x3f800000},
    {0x013b, 0x3f800000},
    {0x013d, 0x3f800000},
    {0x013a, 0x00000000},
    {0x013c, 0x00000000},
    {0x013e, 0x00000000},
    {0x00a2, 0x80000000},
    {0x00a3, 0x40004000},
    {0x00c2, 0x00000000},
    {0x00c3, 0x00000000},
    {0x013f, 0x3f800000},
    {0x0141, 0x3f800000},
    {0x0143, 0x3f800000},
    {0x0140, 0x00000000},
    {0x0142, 0x00000000},
    {0x0144, 0x00000000},
    {0x00a4, 0x80000000},
    {0x00a5, 0x40004000},
    {0x00c4, 0x00000000},
    {0x00c5, 0x00000000},
    {0x0145, 0x3f800000},
    {0x0147, 0x3f800000},
    {0x0149, 0x3f800000},
    {0x0146, 0x00000000},
    {0x0148, 0x00000000},
    {0x014a, 0x00000000},
    {0x00a6, 0x80000000},
    {0x00a7, 0x40004000},
    {0x00c6, 0x00000000},
    {0x00c7, 0x00000000},
    {0x014b, 0x3f800000},
    {0x014d, 0x3f800000},
    {0x014f, 0x3f800000},
    {0x014c, 0x00000000},
    {0x014e, 0x00000000},
    {0x0150, 0x00000000},
    {0x00a8, 0x80000000},
    {0x00a9, 0x40004000},
    {0x00c8, 0x00000000},
    {0x00c9, 0x00000000},
    {0x0151, 0x3f800000},
    {0x0153, 0x3f800000},
    {0x0155, 0x3f800000},
    {0x0152, 0x00000000},
    {0x0154, 0x00000000},
    {0x0156, 0x00000000},
    {0x00aa, 0x80000000},
    {0x00ab, 0x40004000},
    {0x00ca, 0x00000000},
    {0x00cb, 0x00000000},
    {0x0157, 0x3f800000},
    {0x0159, 0x3f800000},
    {0x015b, 0x3f800000},
    {0x0158, 0x00000000},
    {0x015a, 0x00000000},
    {0x015c, 0x00000000},
    {0x00ac, 0x80000000},
    {0x00ad, 0x40004000},
    {0x00cc, 0x00000000},
    {0x00cd, 0x00000000},
    {0x015d, 0x3f800000},
    {0x015f, 0x3f800000},
    {0x0161, 0x3f800000},
    {0x015e, 0x00000000},
    {0x0160, 0x00000000},
    {0x0162, 0x00000000},
    {0x00ae, 0x80000000},
    {0x00af, 0x40004000},
    {0x00ce, 0x00000000},
    {0x00cf, 0x00000000},
    {0x0163, 0x3f800000},
    {0x0165, 0x3f800000},
    {0x0167, 0x3f800000},
    {0x0164, 0x00000000},
    {0x0166, 0x00000000},
    {0x0168, 0x00000000},
    {0x00b0, 0x80000000},
    {0x00b1, 0x40004000},
    {0x00d0, 0x00000000},
    {0x00d1, 0x00000000},
    {0x0169, 0x3f800000},
    {0x016b, 0x3f800000},
    {0x016d, 0x3f800000},
    {0x016a, 0x00000000},
    {0x016c, 0x00000000},
    {0x016e, 0x00000000},
    {0x00b2, 0x80000000},
    {0x00b3, 0x40004000},
    {0x00d2, 0x00000000},
    {0x00d3, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_77[] = {
    {0x0081, 0x80000000},
    {0x0082, 0x40004000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_78[] = {
    {0x0212, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_79[] = {
    {0x0213, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_80[] = {
    {0x0228, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_81[] = {
    {0x0215, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_82[] = {
    {0x0218, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_83[] = {
    {0x008a, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_84[] = {
    {0x010a, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_85[] = {
    {0x000a, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_86[] = {
    {0x008b, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_87[] = {
    {0x010b, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_88[] = {
    {0x000b, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_89[] = {
    {0x0224, 0x00000000},
    {0x0225, 0x00000000},
    {0x0226, 0x00000000},
    {0x0227, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_90[] = {
    {0x0107, 0xffff0000},
    {0x0087, 0x0000ffff},
    {0x0007, 0x0000ffff},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_91[] = {
    {0x020c, 0x00000000},
    {0x020d, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_92[] = {
    {0x00c8, 0x00000000},
    {0x00c9, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_93[] = {
    {0x0088, 0x00000000},
    {0x0089, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_94[] = {
    {0x0108, 0x00000000},
    {0x0109, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_95[] = {
    {0x0148, 0x00000000},
    {0x0149, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_96[] = {
    {0x0008, 0x00000000},
    {0x0009, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_97[] = {
    {0x0280, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_98[] = {
    {0x00b2, 0x00000000},
    {0x00b3, 0x00000000},
    {0x00b4, 0x00000000},
    {0x00b5, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_99[] = {
    {0x0132, 0x00000000},
    {0x0133, 0x00000000},
    {0x0134, 0x00000000},
    {0x0135, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_100[] = {
    {0x0032, 0x00000000},
    {0x0033, 0x00000000},
    {0x0034, 0x00000000},
    {0x0035, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_101[] = {
    {0x0082, 0x00000000},
    {0x0083, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_102[] = {
    {0x0102, 0x00000000},
    {0x0103, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_103[] = {
    {0x0240, 0x00000000},
    {0x0241, 0x00000000},
    {0x0242, 0x00000000},
    {0x0243, 0x00000000},
    {0x0244, 0x00000000},
    {0x0245, 0x00000000},
    {0x0246, 0x00000000},
    {0x0247, 0x00000000},
    {0x0248, 0x00000000},
    {0x0249, 0x00000000},
    {0x024a, 0x00000000},
    {0x024b, 0x00000000},
    {0x024c, 0x00000000},
    {0x024d, 0x00000000},
    {0x024e, 0x00000000},
    {0x024f, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_104[] = {
    {0x008c, 0x00000000},
    {0x008d, 0x00000000},
    {0x008e, 0x00000000},
    {0x008f, 0x00000000},
    {0x0090, 0x00000000},
    {0x0091, 0x00000000},
    {0x0092, 0x00000000},
    {0x0093, 0x00000000},
    {0x0094, 0x00000000},
    {0x0095, 0x00000000},
    {0x0096, 0x00000000},
    {0x0097, 0x00000000},
    {0x0098, 0x00000000},
    {0x0099, 0x00000000},
    {0x009a, 0x00000000},
    {0x009b, 0x00000000},
    {0x009c, 0x00000000},
    {0x009d, 0x00000000},
    {0x009e, 0x00000000},
    {0x009f, 0x00000000},
    {0x00a0, 0x00000000},
    {0x00a1, 0x00000000},
    {0x00a2, 0x00000000},
    {0x00a3, 0x00000000},
    {0x00a4, 0x00000000},
    {0x00a5, 0x00000000},
    {0x00a6, 0x00000000},
    {0x00a7, 0x00000000},
    {0x00a8, 0x00000000},
    {0x00a9, 0x00000000},
    {0x00aa, 0x00000000},
    {0x00ab, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_105[] = {
    {0x010c, 0x00000000},
    {0x010d, 0x00000000},
    {0x010e, 0x00000000},
    {0x010f, 0x00000000},
    {0x0110, 0x00000000},
    {0x0111, 0x00000000},
    {0x0112, 0x00000000},
    {0x0113, 0x00000000},
    {0x0114, 0x00000000},
    {0x0115, 0x00000000},
    {0x0116, 0x00000000},
    {0x0117, 0x00000000},
    {0x0118, 0x00000000},
    {0x0119, 0x00000000},
    {0x011a, 0x00000000},
    {0x011b, 0x00000000},
    {0x011c, 0x00000000},
    {0x011d, 0x00000000},
    {0x011e, 0x00000000},
    {0x011f, 0x00000000},
    {0x0120, 0x00000000},
    {0x0121, 0x00000000},
    {0x0122, 0x00000000},
    {0x0123, 0x00000000},
    {0x0124, 0x00000000},
    {0x0125, 0x00000000},
    {0x0126, 0x00000000},
    {0x0127, 0x00000000},
    {0x0128, 0x00000000},
    {0x0129, 0x00000000},
    {0x012a, 0x00000000},
    {0x012b, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_106[] = {
    {0x000c, 0x00000000},
    {0x000d, 0x00000000},
    {0x000e, 0x00000000},
    {0x000f, 0x00000000},
    {0x0010, 0x00000000},
    {0x0011, 0x00000000},
    {0x0012, 0x00000000},
    {0x0013, 0x00000000},
    {0x0014, 0x00000000},
    {0x0015, 0x00000000},
    {0x0016, 0x00000000},
    {0x0017, 0x00000000},
    {0x0018, 0x00000000},
    {0x0019, 0x00000000},
    {0x001a, 0x00000000},
    {0x001b, 0x00000000},
    {0x001c, 0x00000000},
    {0x001d, 0x00000000},
    {0x001e, 0x00000000},
    {0x001f, 0x00000000},
    {0x0020, 0x00000000},
    {0x0021, 0x00000000},
    {0x0022, 0x00000000},
    {0x0023, 0x00000000},
    {0x0024, 0x00000000},
    {0x0025, 0x00000000},
    {0x0026, 0x00000000},
    {0x0027, 0x00000000},
    {0x0028, 0x00000000},
    {0x0029, 0x00000000},
    {0x002a, 0x00000000},
    {0x002b, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_107[] = {
    {0x041f, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_108[] = {
    {0x041d, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_109[] = {
    {0x041e, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_110[] = {
    {0x025b, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_111[] = {
    {0x024a, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_112[] = {
    {0x024b, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_113[] = {
    {0x025f, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_114[] = {
    {0x0262, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_115[] = {
    {0x02bb, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_116[] = {
    {0x0384, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_117[] = {
    {0x0383, 0x40000040},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_118[] = {
    {0x0248, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_119[] = {
    {0x0242, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_120[] = {
    {0x0380, 0x00000000},
    {0x0381, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_121[] = {
    {0x02e4, 0x00000000},
    {0x02e5, 0x00000000},
    {0x02e6, 0x00000000},
    {0x02e7, 0x00000000},
    {0x02e8, 0x00000000},
    {0x02e9, 0x00000000},
    {0x02ea, 0x00000000},
    {0x02eb, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_122[] = {
    {0x02c4, 0x00000000},
    {0x02c5, 0x00000000},
    {0x02c6, 0x00000000},
    {0x02c7, 0x00000000},
    {0x02c8, 0x00000000},
    {0x02c9, 0x00000000},
    {0x02ca, 0x00000000},
    {0x02cb, 0x00000000},
    {0x02cc, 0x00000000},
    {0x02cd, 0x00000000},
    {0x02ce, 0x00000000},
    {0x02cf, 0x00000000},
    {0x02d0, 0x00000000},
    {0x02d1, 0x00000000},
    {0x02d2, 0x00000000},
    {0x02d3, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_123[] = {
    {0x02bc, 0x00000000},
    {0x02bd, 0x00000000},
    {0x02be, 0x00000000},
    {0x02bf, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_124[] = {
    {0x0035, 0x00000000},
    {0x0036, 0x00000000},
    {0x0037, 0x00000000},
    {0x0038, 0x00000000},
    {0x0039, 0x00000000},
    {0x003a, 0x00000000},
    {0x003b, 0x00000000},
    {0x003c, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_125[] = {
    {0x00a2, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_primary_regs_126[] = {
    {0x025c, 0x00000000},
    {0x025d, 0x00000000},
    {0x025e, 0x00000000},
};

static const AgcRegisterDefaultsGroup s_regdef_v8_primary_defaults[] = {
    {kAgcRegisterDefaultSpaceCx, 0, 0xe24f806du, 1, s_regdef_v8_primary_regs_0},
    {kAgcRegisterDefaultSpaceCx, 1, 0xf6c28182u, 1, s_regdef_v8_primary_regs_1},
    {kAgcRegisterDefaultSpaceCx, 2, 0x6f6e55a5u, 1, s_regdef_v8_primary_regs_2},
    {kAgcRegisterDefaultSpaceCx, 3, 0x0bc65da4u, 1, s_regdef_v8_primary_regs_3},
    {kAgcRegisterDefaultSpaceCx, 4, 0x9e5ad592u, 1, s_regdef_v8_primary_regs_4},
    {kAgcRegisterDefaultSpaceCx, 5, 0xbb513b98u, 1, s_regdef_v8_primary_regs_5},
    {kAgcRegisterDefaultSpaceCx, 6, 0xab64b23bu, 1, s_regdef_v8_primary_regs_6},
    {kAgcRegisterDefaultSpaceCx, 7, 0x53c39964u, 1, s_regdef_v8_primary_regs_7},
    {kAgcRegisterDefaultSpaceCx, 8, 0x01396b11u, 1, s_regdef_v8_primary_regs_8},
    {kAgcRegisterDefaultSpaceCx, 9, 0x7d42019au, 1, s_regdef_v8_primary_regs_9},
    {kAgcRegisterDefaultSpaceCx, 10, 0x3548f523u, 1, s_regdef_v8_primary_regs_10},
    {kAgcRegisterDefaultSpaceCx, 11, 0xf43ad28au, 1, s_regdef_v8_primary_regs_11},
    {kAgcRegisterDefaultSpaceCx, 12, 0x6de4c312u, 1, s_regdef_v8_primary_regs_12},
    {kAgcRegisterDefaultSpaceCx, 13, 0x00a77ae0u, 1, s_regdef_v8_primary_regs_13},
    {kAgcRegisterDefaultSpaceCx, 14, 0x00a779b7u, 1, s_regdef_v8_primary_regs_14},
    {kAgcRegisterDefaultSpaceCx, 15, 0x5100100cu, 1, s_regdef_v8_primary_regs_15},
    {kAgcRegisterDefaultSpaceCx, 16, 0x59958bbau, 1, s_regdef_v8_primary_regs_16},
    {kAgcRegisterDefaultSpaceCx, 17, 0x0c06f17cu, 1, s_regdef_v8_primary_regs_17},
    {kAgcRegisterDefaultSpaceCx, 18, 0x6f104b72u, 1, s_regdef_v8_primary_regs_18},
    {kAgcRegisterDefaultSpaceCx, 19, 0x25c70d9cu, 1, s_regdef_v8_primary_regs_19},
    {kAgcRegisterDefaultSpaceCx, 20, 0x3881201eu, 1, s_regdef_v8_primary_regs_20},
    {kAgcRegisterDefaultSpaceCx, 21, 0x09afddafu, 1, s_regdef_v8_primary_regs_21},
    {kAgcRegisterDefaultSpaceCx, 22, 0x367d63cfu, 1, s_regdef_v8_primary_regs_22},
    {kAgcRegisterDefaultSpaceCx, 23, 0x43707db8u, 1, s_regdef_v8_primary_regs_23},
    {kAgcRegisterDefaultSpaceCx, 24, 0xf6ae26bau, 1, s_regdef_v8_primary_regs_24},
    {kAgcRegisterDefaultSpaceCx, 25, 0x1b917652u, 1, s_regdef_v8_primary_regs_25},
    {kAgcRegisterDefaultSpaceCx, 26, 0x94b1e4f7u, 1, s_regdef_v8_primary_regs_26},
    {kAgcRegisterDefaultSpaceCx, 27, 0xe3661b6cu, 1, s_regdef_v8_primary_regs_27},
    {kAgcRegisterDefaultSpaceCx, 28, 0x1eb8d73au, 1, s_regdef_v8_primary_regs_28},
    {kAgcRegisterDefaultSpaceCx, 29, 0x15051fa3u, 1, s_regdef_v8_primary_regs_29},
    {kAgcRegisterDefaultSpaceCx, 30, 0x9c51a7f1u, 1, s_regdef_v8_primary_regs_30},
    {kAgcRegisterDefaultSpaceCx, 31, 0xa20efc70u, 1, s_regdef_v8_primary_regs_31},
    {kAgcRegisterDefaultSpaceCx, 32, 0x0ec09f6eu, 1, s_regdef_v8_primary_regs_32},
    {kAgcRegisterDefaultSpaceCx, 33, 0x34a7d6d3u, 1, s_regdef_v8_primary_regs_33},
    {kAgcRegisterDefaultSpaceCx, 34, 0xce831b94u, 1, s_regdef_v8_primary_regs_34},
    {kAgcRegisterDefaultSpaceCx, 35, 0x5cc72a74u, 1, s_regdef_v8_primary_regs_35},
    {kAgcRegisterDefaultSpaceCx, 36, 0x3b77713cu, 1, s_regdef_v8_primary_regs_36},
    {kAgcRegisterDefaultSpaceCx, 37, 0x40f64410u, 1, s_regdef_v8_primary_regs_37},
    {kAgcRegisterDefaultSpaceCx, 38, 0x69441268u, 1, s_regdef_v8_primary_regs_38},
    {kAgcRegisterDefaultSpaceCx, 39, 0x2e418b83u, 1, s_regdef_v8_primary_regs_39},
    {kAgcRegisterDefaultSpaceCx, 40, 0xa00d0c8du, 1, s_regdef_v8_primary_regs_40},
    {kAgcRegisterDefaultSpaceCx, 41, 0xb1289fb3u, 1, s_regdef_v8_primary_regs_41},
    {kAgcRegisterDefaultSpaceCx, 42, 0x144832fbu, 1, s_regdef_v8_primary_regs_42},
    {kAgcRegisterDefaultSpaceCx, 43, 0x9890d9fau, 1, s_regdef_v8_primary_regs_43},
    {kAgcRegisterDefaultSpaceCx, 44, 0x9016faf1u, 1, s_regdef_v8_primary_regs_44},
    {kAgcRegisterDefaultSpaceCx, 45, 0x4b73ce27u, 1, s_regdef_v8_primary_regs_45},
    {kAgcRegisterDefaultSpaceCx, 46, 0x5f5a3e7bu, 1, s_regdef_v8_primary_regs_46},
    {kAgcRegisterDefaultSpaceCx, 47, 0xd4af3a51u, 1, s_regdef_v8_primary_regs_47},
    {kAgcRegisterDefaultSpaceCx, 48, 0x6cf4f543u, 1, s_regdef_v8_primary_regs_48},
    {kAgcRegisterDefaultSpaceCx, 49, 0x5fb86ccbu, 1, s_regdef_v8_primary_regs_49},
    {kAgcRegisterDefaultSpaceCx, 50, 0xedefa188u, 1, s_regdef_v8_primary_regs_50},
    {kAgcRegisterDefaultSpaceCx, 51, 0xd0de9ee6u, 1, s_regdef_v8_primary_regs_51},
    {kAgcRegisterDefaultSpaceCx, 52, 0xc5831803u, 1, s_regdef_v8_primary_regs_52},
    {kAgcRegisterDefaultSpaceCx, 53, 0x8e6de84bu, 1, s_regdef_v8_primary_regs_53},
    {kAgcRegisterDefaultSpaceCx, 54, 0xd0771662u, 2, s_regdef_v8_primary_regs_54},
    {kAgcRegisterDefaultSpaceCx, 55, 0x569f7444u, 16, s_regdef_v8_primary_regs_55},
    {kAgcRegisterDefaultSpaceCx, 56, 0x5c6637cdu, 2, s_regdef_v8_primary_regs_56},
    {kAgcRegisterDefaultSpaceCx, 57, 0xcae3e690u, 2, s_regdef_v8_primary_regs_57},
    {kAgcRegisterDefaultSpaceCx, 58, 0x43fbd769u, 4, s_regdef_v8_primary_regs_58},
    {kAgcRegisterDefaultSpaceCx, 59, 0xef550356u, 8, s_regdef_v8_primary_regs_59},
    {kAgcRegisterDefaultSpaceCx, 60, 0x8f52e279u, 2, s_regdef_v8_primary_regs_60},
    {kAgcRegisterDefaultSpaceCx, 61, 0x1f2d8149u, 8, s_regdef_v8_primary_regs_61},
    {kAgcRegisterDefaultSpaceCx, 62, 0x853d0614u, 1, s_regdef_v8_primary_regs_62},
    {kAgcRegisterDefaultSpaceCx, 63, 0x4413c6f9u, 2, s_regdef_v8_primary_regs_63},
    {kAgcRegisterDefaultSpaceCx, 64, 0x67096014u, 16, s_regdef_v8_primary_regs_64},
    {kAgcRegisterDefaultSpaceCx, 65, 0x88f5e915u, 2, s_regdef_v8_primary_regs_65},
    {kAgcRegisterDefaultSpaceCx, 66, 0x033f1effu, 2, s_regdef_v8_primary_regs_66},
    {kAgcRegisterDefaultSpaceCx, 67, 0x918106bbu, 2, s_regdef_v8_primary_regs_67},
    {kAgcRegisterDefaultSpaceCx, 68, 0x95f0e7acu, 4, s_regdef_v8_primary_regs_68},
    {kAgcRegisterDefaultSpaceCx, 69, 0xb48cbab2u, 2, s_regdef_v8_primary_regs_69},
    {kAgcRegisterDefaultSpaceCx, 70, 0x05bb3bc6u, 2, s_regdef_v8_primary_regs_70},
    {kAgcRegisterDefaultSpaceCx, 71, 0x94faba07u, 2, s_regdef_v8_primary_regs_71},
    {kAgcRegisterDefaultSpaceCx, 72, 0x38e92c91u, 128, s_regdef_v8_primary_regs_72},
    {kAgcRegisterDefaultSpaceCx, 73, 0x0b177b43u, 2, s_regdef_v8_primary_regs_73},
    {kAgcRegisterDefaultSpaceCx, 74, 0x48531062u, 32, s_regdef_v8_primary_regs_74},
    {kAgcRegisterDefaultSpaceCx, 75, 0xaaa964b9u, 32, s_regdef_v8_primary_regs_75},
    {kAgcRegisterDefaultSpaceCx, 76, 0x7690af6fu, 160, s_regdef_v8_primary_regs_76},
    {kAgcRegisterDefaultSpaceCx, 77, 0x078d7060u, 2, s_regdef_v8_primary_regs_77},
    {kAgcRegisterDefaultSpaceSh, 0, 0x5d6e3ec7u, 1, s_regdef_v8_primary_regs_78},
    {kAgcRegisterDefaultSpaceSh, 1, 0x57e7079au, 1, s_regdef_v8_primary_regs_79},
    {kAgcRegisterDefaultSpaceSh, 2, 0x7467fafdu, 1, s_regdef_v8_primary_regs_80},
    {kAgcRegisterDefaultSpaceSh, 3, 0x9e826b50u, 1, s_regdef_v8_primary_regs_81},
    {kAgcRegisterDefaultSpaceSh, 4, 0xdc484f18u, 1, s_regdef_v8_primary_regs_82},
    {kAgcRegisterDefaultSpaceSh, 5, 0x5da8bca3u, 1, s_regdef_v8_primary_regs_83},
    {kAgcRegisterDefaultSpaceSh, 6, 0x5ca726d8u, 1, s_regdef_v8_primary_regs_84},
    {kAgcRegisterDefaultSpaceSh, 7, 0x5dd28360u, 1, s_regdef_v8_primary_regs_85},
    {kAgcRegisterDefaultSpaceSh, 8, 0x57efa0beu, 1, s_regdef_v8_primary_regs_86},
    {kAgcRegisterDefaultSpaceSh, 9, 0x502363d5u, 1, s_regdef_v8_primary_regs_87},
    {kAgcRegisterDefaultSpaceSh, 10, 0x506d14bdu, 1, s_regdef_v8_primary_regs_88},
    {kAgcRegisterDefaultSpaceSh, 11, 0xb2609506u, 4, s_regdef_v8_primary_regs_89},
    {kAgcRegisterDefaultSpaceSh, 12, 0x9e5cfb8au, 3, s_regdef_v8_primary_regs_90},
    {kAgcRegisterDefaultSpaceSh, 13, 0xc918df3eu, 2, s_regdef_v8_primary_regs_91},
    {kAgcRegisterDefaultSpaceSh, 14, 0xc9751c9cu, 2, s_regdef_v8_primary_regs_92},
    {kAgcRegisterDefaultSpaceSh, 15, 0xc97ef77au, 2, s_regdef_v8_primary_regs_93},
    {kAgcRegisterDefaultSpaceSh, 16, 0xc927c6b9u, 2, s_regdef_v8_primary_regs_94},
    {kAgcRegisterDefaultSpaceSh, 17, 0xc92a1ec5u, 2, s_regdef_v8_primary_regs_95},
    {kAgcRegisterDefaultSpaceSh, 18, 0xc9e01b31u, 2, s_regdef_v8_primary_regs_96},
    {kAgcRegisterDefaultSpaceSh, 19, 0x50685f29u, 1, s_regdef_v8_primary_regs_97},
    {kAgcRegisterDefaultSpaceSh, 20, 0xb26219cau, 4, s_regdef_v8_primary_regs_98},
    {kAgcRegisterDefaultSpaceSh, 21, 0xb25b6cf9u, 4, s_regdef_v8_primary_regs_99},
    {kAgcRegisterDefaultSpaceSh, 22, 0xb2f86101u, 4, s_regdef_v8_primary_regs_100},
    {kAgcRegisterDefaultSpaceSh, 23, 0x07e3b155u, 2, s_regdef_v8_primary_regs_101},
    {kAgcRegisterDefaultSpaceSh, 24, 0x07e383c6u, 2, s_regdef_v8_primary_regs_102},
    {kAgcRegisterDefaultSpaceSh, 25, 0xbda98653u, 16, s_regdef_v8_primary_regs_103},
    {kAgcRegisterDefaultSpaceSh, 26, 0xbdbd1d0fu, 32, s_regdef_v8_primary_regs_104},
    {kAgcRegisterDefaultSpaceSh, 27, 0xbd946fd4u, 32, s_regdef_v8_primary_regs_105},
    {kAgcRegisterDefaultSpaceSh, 28, 0xbdf02a4cu, 32, s_regdef_v8_primary_regs_106},
    {kAgcRegisterDefaultSpaceUc, 0, 0x19e93e85u, 1, s_regdef_v8_primary_regs_107},
    {kAgcRegisterDefaultSpaceUc, 1, 0x3b5c2af3u, 1, s_regdef_v8_primary_regs_108},
    {kAgcRegisterDefaultSpaceUc, 2, 0x47974a35u, 1, s_regdef_v8_primary_regs_109},
    {kAgcRegisterDefaultSpaceUc, 3, 0x105971c2u, 1, s_regdef_v8_primary_regs_110},
    {kAgcRegisterDefaultSpaceUc, 4, 0x7d137765u, 1, s_regdef_v8_primary_regs_111},
    {kAgcRegisterDefaultSpaceUc, 5, 0xd187febcu, 1, s_regdef_v8_primary_regs_112},
    {kAgcRegisterDefaultSpaceUc, 6, 0x12f854acu, 1, s_regdef_v8_primary_regs_113},
    {kAgcRegisterDefaultSpaceUc, 7, 0x40d49ad1u, 1, s_regdef_v8_primary_regs_114},
    {kAgcRegisterDefaultSpaceUc, 8, 0x8c0923dau, 1, s_regdef_v8_primary_regs_115},
    {kAgcRegisterDefaultSpaceUc, 9, 0xbb8df494u, 1, s_regdef_v8_primary_regs_116},
    {kAgcRegisterDefaultSpaceUc, 10, 0xf6d8a76eu, 1, s_regdef_v8_primary_regs_117},
    {kAgcRegisterDefaultSpaceUc, 11, 0x7620f1e9u, 1, s_regdef_v8_primary_regs_118},
    {kAgcRegisterDefaultSpaceUc, 12, 0x9ebfab10u, 1, s_regdef_v8_primary_regs_119},
    {kAgcRegisterDefaultSpaceUc, 13, 0x98a09d0eu, 2, s_regdef_v8_primary_regs_120},
    {kAgcRegisterDefaultSpaceUc, 14, 0x195d37d2u, 8, s_regdef_v8_primary_regs_121},
    {kAgcRegisterDefaultSpaceUc, 15, 0xf9ec4f85u, 16, s_regdef_v8_primary_regs_122},
    {kAgcRegisterDefaultSpaceUc, 16, 0x4626b750u, 4, s_regdef_v8_primary_regs_123},
    {kAgcRegisterDefaultSpaceUc, 17, 0x4cc673a0u, 8, s_regdef_v8_primary_regs_124},
    {kAgcRegisterDefaultSpaceUc, 18, 0xde5b3431u, 1, s_regdef_v8_primary_regs_125},
    {kAgcRegisterDefaultSpaceUc, 19, 0x036ac8a6u, 3, s_regdef_v8_primary_regs_126},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_0[] = {
    {0x000e, 0x00000002},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_1[] = {
    {0x02af, 0x00040000},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_2[] = {
    {0x0314, 0x00000202},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_3[] = {
    {0x01b5, 0x00000001},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_4[] = {
    {0x0216, 0xffffffff},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_5[] = {
    {0x0217, 0xffffffff},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_6[] = {
    {0x0219, 0xffffffff},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_7[] = {
    {0x021a, 0xffffffff},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_8[] = {
    {0x027d, 0x000004ff},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_9[] = {
    {0x022a, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_10[] = {
    {0x0204, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_11[] = {
    {0x0205, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_12[] = {
    {0x0206, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_13[] = {
    {0x0080, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_14[] = {
    {0x0100, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_15[] = {
    {0x0006, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_16[] = {
    {0x0081, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_17[] = {
    {0x0101, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_18[] = {
    {0x0001, 0x00000003},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_19[] = {
    {0x026c, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_20[] = {
    {0x0094, 0x00000000},
    {0x0095, 0x00000000},
};

static const AgcRegisterDefaultValue s_regdef_v8_internal_regs_21[] = {
    {0x026e, 0x00000000},
    {0x0278, 0x00000000},
    {0x0262, 0x00000000},
};

static const AgcRegisterDefaultsGroup s_regdef_v8_internal_defaults[] = {
    {kAgcRegisterDefaultSpaceCx, 0, 0x8fb4edb5u, 1, s_regdef_v8_internal_regs_0},
    {kAgcRegisterDefaultSpaceCx, 1, 0xb994ad29u, 1, s_regdef_v8_internal_regs_1},
    {kAgcRegisterDefaultSpaceCx, 2, 0xd427322fu, 1, s_regdef_v8_internal_regs_2},
    {kAgcRegisterDefaultSpaceCx, 3, 0xf58fea31u, 1, s_regdef_v8_internal_regs_3},
    {kAgcRegisterDefaultSpaceSh, 0, 0x6ac156efu, 1, s_regdef_v8_internal_regs_4},
    {kAgcRegisterDefaultSpaceSh, 1, 0x6ac15610u, 1, s_regdef_v8_internal_regs_5},
    {kAgcRegisterDefaultSpaceSh, 2, 0x6ac15009u, 1, s_regdef_v8_internal_regs_6},
    {kAgcRegisterDefaultSpaceSh, 3, 0x6ac153bau, 1, s_regdef_v8_internal_regs_7},
    {kAgcRegisterDefaultSpaceSh, 4, 0xbe7dcd73u, 1, s_regdef_v8_internal_regs_8},
    {kAgcRegisterDefaultSpaceSh, 5, 0x0c4b1438u, 1, s_regdef_v8_internal_regs_9},
    {kAgcRegisterDefaultSpaceSh, 6, 0xdb00d71au, 1, s_regdef_v8_internal_regs_10},
    {kAgcRegisterDefaultSpaceSh, 7, 0xdb00d249u, 1, s_regdef_v8_internal_regs_11},
    {kAgcRegisterDefaultSpaceSh, 8, 0xdb00ec60u, 1, s_regdef_v8_internal_regs_12},
    {kAgcRegisterDefaultSpaceSh, 9, 0x0c4d6fe4u, 1, s_regdef_v8_internal_regs_13},
    {kAgcRegisterDefaultSpaceSh, 10, 0x0c4a80efu, 1, s_regdef_v8_internal_regs_14},
    {kAgcRegisterDefaultSpaceSh, 11, 0x0dd283e7u, 1, s_regdef_v8_internal_regs_15},
    {kAgcRegisterDefaultSpaceSh, 12, 0xc620e68cu, 1, s_regdef_v8_internal_regs_16},
    {kAgcRegisterDefaultSpaceSh, 13, 0xc67efacfu, 1, s_regdef_v8_internal_regs_17},
    {kAgcRegisterDefaultSpaceSh, 14, 0xd9e6d9f7u, 1, s_regdef_v8_internal_regs_18},
    {kAgcRegisterDefaultSpaceUc, 0, 0x31f34b9fu, 1, s_regdef_v8_internal_regs_19},
    {kAgcRegisterDefaultSpaceUc, 1, 0xac0f9e76u, 2, s_regdef_v8_internal_regs_20},
    {kAgcRegisterDefaultSpaceUc, 2, 0x929fd95du, 3, s_regdef_v8_internal_regs_21},
};

const AgcRegisterDefaultsGroup *agcRegisterDefaultsV8GetPrimaryGroups(uint32_t *out_count) {
    if (out_count)
        *out_count = (uint32_t)(sizeof(s_regdef_v8_primary_defaults) /
                               sizeof(s_regdef_v8_primary_defaults[0]));
    return s_regdef_v8_primary_defaults;
}

const AgcRegisterDefaultsGroup *agcRegisterDefaultsV8GetInternalGroups(uint32_t *out_count) {
    if (out_count)
        *out_count = (uint32_t)(sizeof(s_regdef_v8_internal_defaults) /
                               sizeof(s_regdef_v8_internal_defaults[0]));
    return s_regdef_v8_internal_defaults;
}
