#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 path/to/FW11.60/libSceAgcDriver.sprx path/to/FW11.60/libSceAgc.sprx" >&2
    exit 2
fi

driver=$1
agc=$2
objdump=${OBJDUMP:-llvm-objdump}
readelf=${READELF:-llvm-readelf}
tmp_base=${TMPDIR:-/tmp}/openagc-fw1160.$$
driver_disasm=$tmp_base.driver.disasm
driver_symbols=$tmp_base.driver.symbols
driver_strings=$tmp_base.driver.strings
agc_disasm=$tmp_base.agc.disasm
agc_symbols=$tmp_base.agc.symbols
trap 'rm -f "$driver_disasm" "$driver_symbols" "$driver_strings" "$agc_disasm" "$agc_symbols"' \
    EXIT HUP INT TERM

"$objdump" -d "$driver" >"$driver_disasm"
"$readelf" -Ws "$driver" >"$driver_symbols"
strings "$driver" >"$driver_strings"
"$objdump" -d "$agc" >"$agc_disasm"
"$readelf" -Ws "$agc" >"$agc_symbols"

require()
{
    file=$1
    label=$2
    pattern=$3
    if ! grep -Eiq "$pattern" "$file"; then
        echo "FAIL: FW 11.60 missing $label ($pattern)" >&2
        exit 1
    fi
}

# Tie public wrappers to exact NID, address, and size before checking their
# internal paths. This prevents an unrelated immediate elsewhere in the image
# from being accepted as operation evidence.
require "$driver_symbols" "SubmitDcb export" \
    '0000000000002960[[:space:]]+15[[:space:]]+FUNC.*UglJIZjGssM#G#A'
require "$driver_symbols" "NotifyDefaultStates export" \
    '0000000000003840[[:space:]]+1225[[:space:]]+FUNC.*nR6xhiFsOoc#G#A'
require "$driver_symbols" "SetTFRing export" \
    '0000000000006340[[:space:]]+73[[:space:]]+FUNC.*XlNp7jzGiPo#G#A'
require "$driver_symbols" "SetHsOffchipParam export" \
    '0000000000006400[[:space:]]+27[[:space:]]+FUNC.*MM4IZSEYytQ#G#A'
require "$driver_symbols" "workload-active export" \
    '0000000000000d10[[:space:]]+338[[:space:]]+FUNC.*UM9b9NunSrE#G#A'
require "$driver_symbols" "workload-complete export" \
    '0000000000000e70[[:space:]]+244[[:space:]]+FUNC.*i6bfTi13ApA#G#A'

# submit16 payload: uint32 queue/type, uint32 count, uint64 descriptor pointer.
require "$driver_disasm" "submit field 0 store" '^ *8507:.*movl.*%esi, -0x20\(%rbp\)'
require "$driver_disasm" "submit field 4 store" '^ *850a:.*movl.*%edx, -0x1c\(%rbp\)'
require "$driver_disasm" "submit field 8 store" '^ *8518:.*movq.*%rcx, -0x18\(%rbp\)'
require "$driver_disasm" "submit16 request" '^ *8511:.*\$0xc0108102'

# Authenticated queue path and exact carved-memory offsets.
require "$driver_disasm" "queue token 0" '^ *87a9:.*\$0xaf1e80b7'
require "$driver_disasm" "queue token 1" '^ *87ae:.*\$0x8b4cdd90'
require "$driver_disasm" "queue token 2" '^ *87b4:.*\$0x99f68d6c'
require "$driver_disasm" "queue token 3" '^ *8836:.*\$0xe5fcc174'
require "$driver_disasm" "queue EOP offset" '^ *87bc:.*\$0x39000'
require "$driver_disasm" "queue create request" '^ *87e3:.*\$0xc0408121'
require "$driver_disasm" "queue destroy request" '^ *8e35:.*\$0xc00c810e'
require "$driver_disasm" "ACQRB read-pointer offset" '^ *2f28:.*0x1c8000'
require "$driver_disasm" "ACQRB metadata offset" '^ *2f2f:.*\$0x1cc000'

# Suspend submission is a four-uint32 payload. Query semantics are deliberately
# not enabled: both exported query forms are permission stubs on this image.
require "$driver_disasm" "suspend field 0 store" '^ *9447:.*movl.*%esi, -0x20\(%rbp\)'
require "$driver_disasm" "suspend field 1 store" '^ *944a:.*movl.*%edx, -0x1c\(%rbp\)'
require "$driver_disasm" "suspend field 2 store" '^ *9456:.*movl.*%ecx, -0x18\(%rbp\)'
require "$driver_disasm" "suspend field 3 store" '^ *945b:.*movl.*%r8d, -0x14\(%rbp\)'
require "$driver_disasm" "primary suspend request" '^ *9451:.*\$0xc010811c'
require "$driver_disasm" "final suspend request" '^ *9fe0:.*\$0xc0108139'
require "$driver_symbols" "direct suspend permission export" \
    '0000000000002da0[[:space:]]+39[[:space:]]+FUNC.*ZV04pRl7cWU#J#A'
require "$driver_symbols" "direct suspend-query permission export" \
    '0000000000004be0[[:space:]]+39[[:space:]]+FUNC.*I6elAJxk6Jo#J#A'
require "$driver_disasm" "public suspend-query permission stub" \
    '^ *4bd0:.*\$0x8a6d0001'

# Ring and HS payloads are uint64 address + uint32 size/count + zero padding.
require "$driver_disasm" "public TF address store" '^ *8ff0:.*movq.*%rsi, -0x20\(%rbp\)'
require "$driver_disasm" "public TF size store" '^ *8ffa:.*movl.*%edx, -0x18\(%rbp\)'
require "$driver_disasm" "public TF request" '^ *9006:.*\$0x80108128'
require "$driver_disasm" "privileged TF request" '^ *97d6:.*\$0xc0108120'
require "$driver_disasm" "HS address store" '^ *9837:.*movq.*%rsi, -0x20\(%rbp\)'
require "$driver_disasm" "HS count store" '^ *983b:.*movl.*%edx, -0x18\(%rbp\)'
require "$driver_disasm" "HS request" '^ *9842:.*\$0xc010812c'
require "$driver_disasm" "async request" '^ *a0fe:.*\$0x80048126'
require "$driver_disasm" "distinct queue-status request" '^ *a14e:.*\$0x80048127'

# Workload exports construct nine-dword 0xc0071e00 packets, not OpenAGC's
# FW 5.50 three-dword convenience packet contract.
require "$driver_disasm" "workload-active packet" '^ *de8:.*\$0xc0071e00'
require "$driver_disasm" "workload-complete packet" '^ *ef0:.*\$0xc0071e00'

# Standard/Trinity memory branches recovered from the allocation routine.
require "$driver_disasm" "Trinity CWSR allocation" '^ *7d6f:.*\$0x1600000'
require "$driver_disasm" "standard CWSR allocation" '^ *7d7c:.*\$0x1000000'
require "$driver_disasm" "standard CWSR work offset" '^ *7e5b:.*\$0xa00000'
require "$driver_disasm" "Trinity CWSR work offset" '^ *7e60:.*\$0x1000000'
require "$driver_disasm" "Trinity GPU-info span" '^ *a920:.*\$0x180000'

# The standard-console constructor allocates a distinct 2 MiB direct-memory
# aperture immediately below /dev/gc MMIO, then publishes its two 0x19000
# register-shadow slices through Gn2/Gn3/Gn4. The vmovups at 0x75d6 reads
# virtual address 0x10220. In this sectionless ELF that virtual address maps to
# file offset 0x14220 (LOAD file=0x10000, vaddr=0xc000); do not confuse it with
# raw file offset 0x10220, which contains unrelated bytes.
require "$driver_disasm" "driver-memory address hint" \
    '^ *501:.*\$0xfe0000000'
require "$driver_disasm" "standard driver-memory size" \
    '^ *540:.*\$0x200000'
require "$driver_disasm" "driver-memory type" '^ *577:.*\$0xc, %r8d'
require "$driver_disasm" "driver-memory CPU/GPU protection" \
    '^ *593:.*\$0x33, %edx'
require "$driver_disasm" "register-shadow first four words load" \
    '^ *75d6:.*0x10220'
require "$driver_disasm" "register-shadow final two words" \
    '^ *75e3:.*\$0x284300002400'
shadow_words=$(od -An -tx1 -j $((0x14220)) -N 16 "$driver" | tr -d ' \n')
if [ "$shadow_words" != 00000000bf0300000020000081220000 ]; then
    echo "FAIL: FW 11.60 register-shadow words at mapped vaddr 0x10220" >&2
    exit 1
fi
require "$driver_disasm" "standard Gn2/Gn3/Gn4 helper call" \
    '^ *786a:.*0x7f70'
require "$driver_disasm" "two 40-byte shadow descriptor copies" \
    '^ *8003:.*0x28\(%rax\)'
for property_name in Sce.Debug:Gn2 Sce.Debug:Gn3 Sce.Debug:Gn4 \
    SceAgcRegShadow SceAgcRegShadowCopy SceAgcRegShadowInfo \
    SceAgcGprDumpArea SceAgcDdid; do
    require "$driver_strings" "$property_name name" "^${property_name}$"
done

# libSceAgc exposes a versioned 0..12 dispatcher, but the no-argument API reads
# a runtime-selected table field. This proves that 11.60 cannot safely inherit
# the FW 5.50 version-8 choice from the driver wrapper alone.
require "$agc_symbols" "versioned defaults export" \
    '0000000000009db0[[:space:]]+476[[:space:]]+FUNC.*2JtWUUiYBXs#G#A'
require "$agc_symbols" "runtime defaults export" \
    '000000000000a170[[:space:]]+46[[:space:]]+FUNC.*Wi82ArQtAwg#G#A'
require "$agc_disasm" "defaults upper version bound" '^ *9e3d:.*\$0xc, %ebx'
require "$agc_disasm" "runtime defaults table index" '^ *a186:.*movslq.*0x34014'
require "$agc_disasm" "runtime defaults version field" '^ *a195:.*movl.*0x44'

echo "PASS: FW 11.60 exact driver/AGC operation evidence verified"
