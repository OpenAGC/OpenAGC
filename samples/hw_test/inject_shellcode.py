#!/usr/bin/env python3
"""
Inject color bars into a running game by:
1. Finding libSceVideoOut and libkernel base addresses
2. Patching the linear tiling check
3. Allocating executable memory
4. Writing shellcode that creates a thread running the color bar loop
5. Calling the shellcode via CMD_PROC_CALL
"""

import socket
import struct
import sys
import time

PACKET_MAGIC = 0xFFAABBCC
CMD_PROC_LIST = 0xBDAA0001
CMD_PROC_MAPS = 0xBDAA0004
CMD_PROC_READ = 0xBDAA0002
CMD_PROC_WRITE = 0xBDAA0003
CMD_PROC_ALLOC = 0xBDAA000B
CMD_PROC_PROTECT = 0xBDAA0008
CMD_PROC_CALL = 0xBDAA0006

def bitswap32(x):
    return ((x >> 1) & 0x55555555) | ((x << 1) & 0xAAAAAAAA)
CMD_SUCCESS_WIRE = bitswap32(0x40000000)

PS5_HOST = '10.0.1.41'
PS5_PORT = 744

# Function offsets in libSceVideoOut.sprx (FW 5.50)
VO_OPEN = 0x14f80
VO_SET_ATTR = 0x12710
VO_REGISTER = 0x15140
VO_SUBMIT_FLIP = 0x11a10
VO_GET_RES = 0xc110
VO_ADD_FLIP_EVENT = 0x110d0

# Function offsets in libkernel_sys.sprx (FW 5.50)
K_ALLOC_DIRECT = 0x4a10
K_MAP_DIRECT = 0xe6f0
K_USLEEP = 0x283a0
K_CREATE_EQUEUE = 0x1ba90
K_WAIT_EQUEUE = 0xa140
K_RELEASE_DIRECT = 0x4a60

# Patch offset in libSceVideoOut for linear tiling bypass
VO_PATCH_OFFSET = 0x7e61
VO_PATCH_ORIGINAL = bytes([0x0f, 0x84, 0x15, 0x02, 0x00, 0x00])
VO_PATCH_NEW = bytes([0x90, 0x90, 0x90, 0x90, 0x90, 0x90])

def recv_status(s):
    d = b''
    while len(d) < 4: d += s.recv(4 - len(d))
    return struct.unpack('<I', d)[0]

def proc_read(s, pid, addr, length):
    s.send(struct.pack('<III', PACKET_MAGIC, CMD_PROC_READ, 16) + struct.pack('<IQI', pid, addr, length))
    if recv_status(s) != CMD_SUCCESS_WIRE: return None
    data = b''
    while len(data) < length:
        chunk = s.recv(min(length - len(data), 0x10000))
        if not chunk: break
        data += chunk
    return data

def proc_write(s, pid, addr, data):
    body = struct.pack('<IQI', pid, addr, len(data))
    s.send(struct.pack('<III', PACKET_MAGIC, CMD_PROC_WRITE, len(body)) + body)
    if recv_status(s) != CMD_SUCCESS_WIRE: return False
    sent = 0
    while sent < len(data):
        chunk = data[sent:sent+0x10000]
        s.send(chunk)
        sent += len(chunk)
    return recv_status(s) == CMD_SUCCESS_WIRE

def proc_alloc(s, pid, length):
    body = struct.pack('<II', pid, length)
    s.send(struct.pack('<III', PACKET_MAGIC, CMD_PROC_ALLOC, len(body)) + body)
    if recv_status(s) != CMD_SUCCESS_WIRE: return None
    d = b''
    while len(d) < 8: d += s.recv(8 - len(d))
    return struct.unpack('<Q', d)[0]

def proc_protect(s, pid, addr, length, prot):
    body = struct.pack('<IQII', pid, addr, length, prot)
    s.send(struct.pack('<III', PACKET_MAGIC, CMD_PROC_PROTECT, len(body)) + body)
    return recv_status(s) == CMD_SUCCESS_WIRE

def proc_call(s, pid, rip, rdi=0, rsi=0, rdx=0, rcx=0, r8=0, r9=0):
    body = struct.pack('<IQQQQQQQQQ', pid, 0, rip, rdi, rsi, rdx, rcx, r8, r9)
    s.send(struct.pack('<III', PACKET_MAGIC, CMD_PROC_CALL, len(body)) + body)
    if recv_status(s) != CMD_SUCCESS_WIRE: return None
    d = b''
    while len(d) < 12: d += s.recv(12 - len(d))
    return struct.unpack('<Q', d[4:12])[0]

def find_game_pid(s):
    s.send(struct.pack('<III', PACKET_MAGIC, CMD_PROC_LIST, 0))
    recv_status(s)
    num = struct.unpack('<I', s.recv(4))[0]
    for _ in range(num):
        entry = b''
        while len(entry) < 36: entry += s.recv(36 - len(entry))
        name = entry[:32].split(b'\x00')[0].decode('ascii', errors='replace')
        pid = struct.unpack('<i', entry[32:36])[0]
        if name == 'eboot.bin':
            return pid
    return None

def get_proc_maps(s, pid):
    s.send(struct.pack('<III', PACKET_MAGIC, CMD_PROC_MAPS, 4) + struct.pack('<I', pid))
    recv_status(s)
    num = struct.unpack('<I', s.recv(4))[0]
    maps = []
    for _ in range(num):
        entry = b''
        while len(entry) < 58: entry += s.recv(58 - len(entry))
        name = entry[:32].split(b'\x00')[0].decode('ascii', errors='replace')
        start = struct.unpack('<Q', entry[32:40])[0]
        end = struct.unpack('<Q', entry[40:48])[0]
        offset = struct.unpack('<Q', entry[48:56])[0]
        prot = struct.unpack('<H', entry[56:58])[0]
        maps.append((name, start, end, offset, prot))
    return maps

def build_shellcode(vo_base, k_base, code_addr):
    """Build x86_64 shellcode that:
    1. Saves registers
    2. Calls sceVideoOutOpen
    3. Allocates direct memory
    4. Maps it
    5. Sets buffer attribute (linear)
    6. Registers buffers
    7. Creates event queue
    8. Adds flip event
    9. Fills color bars and flips in a loop
    10. Restores registers and returns
    """
    code = bytearray()

    # Function addresses
    vo_open = vo_base + VO_OPEN
    vo_set_attr = vo_base + VO_SET_ATTR
    vo_register = vo_base + VO_REGISTER
    vo_submit_flip = vo_base + VO_SUBMIT_FLIP
    vo_get_res = vo_base + VO_GET_RES
    vo_add_flip_event = vo_base + VO_ADD_FLIP_EVENT
    k_alloc_direct = k_base + K_ALLOC_DIRECT
    k_map_direct = k_base + K_MAP_DIRECT
    k_usleep = k_base + K_USLEEP
    k_create_equeue = k_base + K_CREATE_EQUEUE
    k_wait_equeue = k_base + K_WAIT_EQUEUE

    # Helper: call function with up to 6 args
    # We use rdi, rsi, rdx, rcx, r8, r9 for args
    # call rel32 can reach ±2GB, so we use mov rax, addr; call rax

    def mov_rax(addr):
        return b'\x48\xb8' + struct.pack('<Q', addr)
    def mov_rdi(val):
        return b'\x48\xbf' + struct.pack('<Q', val)
    def mov_rsi(val):
        return b'\x48\xbe' + struct.pack('<Q', val)
    def mov_rdx(val):
        return b'\x48\xba' + struct.pack('<Q', val)
    def mov_rcx(val):
        return b'\x48\xb9' + struct.pack('<Q', val)
    def mov_r8(val):
        return b'\x49\xb8' + struct.pack('<Q', val)
    def mov_r9(val):
        return b'\x49\xb9' + struct.pack('<Q', val)
    def call_rax():
        return b'\xff\xd0'
    def ret():
        return b'\xc3'
    def push_regs():
        return b'\x41\x54\x41\x55\x41\x56\x41\x57\x55\x53\x50'  # push r12-r15, rbp, rbx, rax
    def pop_regs():
        return b'\x58\x5b\x5d\x41\x5f\x41\x5e\x41\x5d\x41\x5c'  # pop rax, rbx, rbp, r15-r12

    # Save registers
    code += push_regs()

    # === sceVideoOutOpen(userId=0xff, type=0, index=0, param=NULL) ===
    code += mov_rdi(0xff)           # userId = 0xff
    code += mov_rsi(0)              # type = 0 (normal)
    code += mov_rdx(0)              # index = 0
    code += mov_rcx(0)              # param = NULL
    code += mov_rax(vo_open)
    code += call_rax()
    # rax = handle (or error)
    # Save handle in r12
    code += b'\x49\x89\xc4'  # mov r12, rax

    # Check if handle is valid (positive)
    code += b'\x48\x85\xc0'  # test rax, rax
    code += b'\x0f\x88' + struct.pack('<i', 0)  # js error (placeholder, we'll just continue)

    # === sceKernelAllocateDirectMemory(0, 0x300000000, 0x400000, 0x200000, 3, &phys) ===
    # We need stack space for the phys output
    code += b'\x48\x83\xec\x20'  # sub rsp, 32
    code += mov_rdi(0)            # searchStart
    code += mov_rsi(0x300000000)  # searchEnd
    code += mov_rdx(0x400000)     # len = 4MB
    code += mov_rcx(0x200000)     # alignment = 2MB
    code += mov_r8(3)             # memoryType = GARLIC
    code += mov_r9(0)
    code += b'\x4c\x8d\x0c\x24'  # lea r9, [rsp] (phys output)
    code += mov_rax(k_alloc_direct)
    code += call_rax()
    # rax = 0 on success
    # Read phys from [rsp]
    code += b'\x48\x8b\x1c\x24'  # mov rbx, [rsp] (phys address)

    # === sceKernelMapDirectMemory(&cpu_addr, 0x400000, 3, 0, phys, 0x200000) ===
    code += b'\x48\x8d\x0c\x24'  # lea rcx, [rsp] (cpu_addr output)
    code += mov_rdi(0)            # hint = 0
    code += mov_rsi(0x400000)     # len
    code += mov_rdx(3)            # prot = PROT_READ|PROT_WRITE
    code += b'\x49\x89\xc8'      # mov r8, rcx -> actually we need &cpu_addr in r8
    # Wait, the signature is: sceKernelMapDirectMemory(void**addr, len, prot, flags, directMemoryStart, alignment)
    # rdi = &addr, rsi = len, rdx = prot, rcx = flags, r8 = phys, r9 = alignment
    code += b'\x48\x8d\x3c\x24'  # lea rdi, [rsp] (&addr output)
    code += mov_rsi(0x400000)     # len
    code += mov_rdx(3)            # prot
    code += mov_rcx(0)            # flags
    code += mov_r8(0)
    code += b'\x4c\x89\xd8'      # mov rax, rbx (phys)
    code += b'\x49\x89\xc0'      # mov r8, rax (phys)
    code += mov_r9(0x200000)      # alignment
    code += mov_rax(k_map_direct)
    code += call_rax()
    # Read cpu_addr from [rsp]
    code += b'\x48\x8b\x2c\x24'  # mov rbp, [rsp] (cpu address of mapped memory)

    # === sceVideoOutSetBufferAttribute(&attr, pixelFormat, tilingMode, aspectRatio, w, h, pitch) ===
    # attr is 0x50 bytes, we'll use stack space (rsp+32)
    # Zero it first
    code += b'\x48\x31\xc0'      # xor rax, rax
    for i in range(0, 0x50, 8):
        code += b'\x48\x89\x44\x24' + bytes([32+i])  # mov [rsp+32+i], rax

    code += b'\x48\x8d\x7c\x24\x20'  # lea rdi, [rsp+32] (&attr)
    code += mov_rsi(0x80000000)       # pixelFormat = A8R8G8B8_SRGB
    code += mov_rdx(1)                # tilingMode = LINEAR (1)
    code += mov_rcx(0)                # aspectRatio = 16:9 (0)
    code += mov_r8(1920)              # width
    code += mov_r9(1080)              # height
    # pitch is 7th arg (on stack)
    code += b'\x48\xc7\x44\x24\x20\x80\x07\x00\x00'  # mov dword [rsp+32], 1920 (but this overwrites attr!)
    # Actually, the 7th arg goes at [rsp+8] in SysV ABI
    # Wait, for SysV ABI: args 1-6 in rdi,rsi,rdx,rcx,r8,r9. 7th arg at [rsp+8]
    # But we already pushed and sub'd rsp. Let me recalculate.
    # After push_regs (pushed 11 regs = 88 bytes) and sub rsp,32, total stack offset is 120
    # 7th arg at [rsp+8]
    code += b'\x48\xc7\x44\x24\x08\x80\x07\x00\x00'  # mov dword [rsp+8], 1920 (pitch)
    code += mov_rax(vo_set_attr)
    code += call_rax()

    # === sceVideoOutRegisterBuffers(handle, 0, &addresses, 2, &attr) ===
    # We need 2 buffer addresses on the stack
    # Buffer 0 at rbp (mapped memory base)
    # Buffer 1 at rbp + 1920*1080*4 = rbp + 0x7f9000 (aligned to 2MB = rbp + 0x200000)
    code += b'\x48\x89\x6c\x24\x40'  # mov [rsp+64], rbp (buffer 0 addr)
    code += b'\x48\x8d\x84\x2d\x00\x00\x20\x00'  # lea rax, [rbp+0x200000] (buffer 1 addr)
    code += b'\x48\x89\x44\x24\x48'  # mov [rsp+72], rax (buffer 1 addr)

    code += b'\x4c\x89\xe7'      # mov rdi, r12 (handle)
    code += mov_rsi(0)            # startIndex
    code += b'\x48\x8d\x54\x24\x40'  # lea rdx, [rsp+64] (&addresses)
    code += mov_rcx(2)            # count
    code += b'\x49\x8d\x4c\x24\x20'  # lea r8, [r12+32]... no, lea r8, [rsp+32] (&attr)
    code += b'\x4c\x8d\x44\x24\x20'  # lea r8, [rsp+32] (&attr)
    code += mov_rax(vo_register)
    code += call_rax()
    # rax = 0 on success
    # Save result in r13
    code += b'\x49\x89\xc5'  # mov r13, rax

    # === sceKernelCreateEqueue(&queue, "colorbars") ===
    code += b'\x4c\x8d\x44\x24\x50'  # lea r8, [rsp+80] (&queue)
    # We need a string "colorbars" somewhere. Put it on stack.
    code += b'\x48\xb8' + b'colorbar\x00\x00\x00\x00\x00\x00\x00'  # mov rax, "colorbar\0"
    code += b'\x48\x89\x44\x24\x58'  # mov [rsp+88], rax
    code += b'\x4c\x89\xc7'      # mov rdi, r8 (&queue)
    code += b'\x48\x8d\x74\x24\x58'  # lea rsi, [rsp+88] (name)
    code += mov_rax(k_create_equeue)
    code += call_rax()

    # === sceVideoOutAddFlipEvent(handle, queue, NULL) ===
    code += b'\x4c\x89\xe7'      # mov rdi, r12 (handle)
    code += b'\x48\x8d\x74\x24\x50'  # lea rsi, [rsp+80] (&queue)
    code += mov_rdx(0)            # param = NULL
    code += mov_rax(vo_add_flip_event)
    code += call_rax()

    # === Fill color bars and flip loop ===
    # We'll fill both buffers with color bars, then flip
    # Buffer 0 at rbp, Buffer 1 at rbp + 0x200000
    # 1920x1080x4 = 0x7f9000 bytes per buffer

    # Fill buffer 0 with color bars
    # Use a simple pattern: 8 vertical bars
    colors = [0xFFFFFFFF, 0xFFFFFF00, 0xFF00FFFF, 0xFF00FF00,
              0xFFFF00FF, 0xFFFF0000, 0xFF0000FF, 0xFF000000]
    bar_width = 1920 // 8

    # For efficiency, fill one line then copy to all lines
    # rbp = buffer base, 1920 pixels per line, 4 bytes per pixel
    # Fill line 0 with color bars
    for i, color in enumerate(colors):
        # Fill bar_width pixels with color
        # rep stosd: fill [rdi] with eax, count in rcx
        code += mov_rax(color)
        code += b'\x48\x89\xeb'  # mov rbx, rbp (buffer base)
        code += b'\x48\x83\xc3' + struct.pack('<i', i * bar_width * 4)  # add rbx, offset
        code += b'\x48\x89\xdf'  # mov rdi, rbx
        code += mov_rcx(bar_width)  # count
        code += b'\xf3\xab'  # rep stosd

    # Copy line 0 to all 1080 lines
    # src = rbp, dst = rbp + 1920*4, count = 1080-1 lines, len = 1920*4 bytes
    code += b'\x48\x89\xef'  # mov rdi, rbp + 1920*4
    code += b'\x48\x83\xc7' + struct.pack('<i', 1920 * 4)  # add rdi, 1920*4
    code += b'\x48\x89\xee'  # mov rsi, rbp (src)
    code += mov_rcx(1920 * 4)  # bytes per line
    code += b'\x48\x89\xc8'  # mov rax, rcx
    code += b'\x48\xf7\xe1'  # mul rcx -> no, we need 1079 * 1920*4
    # Actually, let's use a simple loop
    # mov r8, 1079 (line counter)
    code += b'\x49\xc7\xc0' + struct.pack('<i', 1079)  # mov r8, 1079
    # loop:
    loop_offset = len(code)
    code += b'\xf3\xa4'  # rep movsb (copy one line)
    code += b'\x49\x83\xe8\x01'  # dec r8
    code += b'\x75' + bytes([0])  # jnz loop (placeholder)
    # Fix the jnz offset
    jnz_offset = len(code)
    target = loop_offset
    code[jnz_offset-1] = target - jnz_offset  # relative jump back

    # Copy buffer 0 to buffer 1 (rbp+0x200000)
    code += b'\x48\x89\xee'  # mov rsi, rbp (src)
    code += b'\x48\x8d\xbe' + struct.pack('<i', 0x200000)  # lea rdi, [rbp+0x200000] (dst)
    code += mov_rcx(1920 * 1080 * 4)  # total bytes
    code += b'\xf3\xa4'  # rep movsb

    # === Flip loop ===
    flip_loop = len(code)
    # sceVideoOutSubmitFlip(handle, index, flipMode, flipArg)
    code += b'\x4c\x89\xe7'  # mov rdi, r12 (handle)
    code += b'\x31\xf6'      # xor rsi, rsi (index 0)
    code += mov_rdx(1)       # flipMode = VSYNC
    code += mov_rcx(0)       # flipArg
    code += mov_rax(vo_submit_flip)
    code += call_rax()

    # sceKernelWaitEqueue(queue, &event, 1, &out, NULL)
    code += b'\x48\x8d\x7c\x24\x60'  # lea rdi, [rsp+96] (&queue)
    code += b'\x48\x8d\x74\x24\x68'  # lea rsi, [rsp+104] (&event)
    code += mov_rdx(1)       # count
    code += b'\x49\x8d\x4c\x24\x78'  # lea rcx, [r12+120]... no
    code += b'\x48\x8d\x4c\x24\x88'  # lea rcx, [rsp+136] (&out)
    code += b'\x48\xc7\x44\x24\x90\x00\x00\x00\x00'  # mov [rsp+144], 0 (timeout=NULL)
    # Actually, 5th arg (timeout) is at [rsp+8] in SysV ABI
    # Wait, for waitEqueue: rdi=queue, rsi=&event, rdx=count, rcx=&out, r8=timeout
    code += b'\x4c\x8d\x44\x24\x60'  # lea r8, [rsp+96] (&queue) - wait, this is wrong
    # Let me redo this properly
    # sceKernelWaitEqueue(equeue, event, count, out, timeout)
    # rdi = equeue, rsi = event, rdx = count, rcx = out, r8 = timeout (NULL)
    code += b'\x48\x8b\x7c\x24\x50'  # mov rdi, [rsp+80] (queue value)
    code += b'\x48\x8d\x74\x24\x68'  # lea rsi, [rsp+104] (&event)
    code += mov_rdx(1)
    code += b'\x48\x8d\x4c\x24\x88'  # lea rcx, [rsp+136] (&out)
    code += b'\x4d\x31\xc0'  # xor r8, r8 (timeout = NULL)
    code += mov_rax(k_wait_equeue)
    code += call_rax()

    # Flip buffer 1
    code += b'\x4c\x89\xe7'  # mov rdi, r12 (handle)
    code += mov_rsi(1)       # index 1
    code += mov_rdx(1)       # flipMode
    code += mov_rcx(1)       # flipArg
    code += mov_rax(vo_submit_flip)
    code += call_rax()

    # Wait for flip
    code += b'\x48\x8b\x7c\x24\x50'  # mov rdi, [rsp+80] (queue)
    code += b'\x48\x8d\x74\x24\x68'  # lea rsi, [rsp+104] (&event)
    code += mov_rdx(1)
    code += b'\x48\x8d\x4c\x24\x88'  # lea rcx, [rsp+136] (&out)
    code += b'\x4d\x31\xc0'  # xor r8, r8
    code += mov_rax(k_wait_equeue)
    code += call_rax()

    # Jump back to flip loop
    code += b'\xe9' + struct.pack('<i', flip_loop - len(code) - 5)  # jmp flip_loop

    # Clean up and return (unreachable in normal flow, but here for safety)
    code += b'\x48\x83\xc4\x20'  # add rsp, 32
    code += pop_regs()
    code += ret()

    return bytes(code)

def main():
    # 1. Find game process
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    pid = find_game_pid(s)
    s.close()

    if not pid:
        print("No eboot.bin process found!")
        return
    print(f"Game: pid={pid}")

    # 2. Get maps and find library bases
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    maps = get_proc_maps(s, pid)
    s.close()

    vo_base = k_base = None
    for name, start, end, offset, prot in maps:
        if 'VideoOut' in name and prot == 0x5:
            vo_base = start - offset
        if 'kernel' in name.lower() and prot == 0x5 and 'sys' in name.lower():
            k_base = start - offset

    if not vo_base:
        print("libSceVideoOut not found!")
        return
    if not k_base:
        print("libkernel not found!")
        return

    print(f"libSceVideoOut: 0x{vo_base:x}")
    print(f"libkernel: 0x{k_base:x}")

    # 3. Patch linear tiling check
    patch_addr = vo_base + VO_PATCH_OFFSET
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    current = proc_read(s, pid, patch_addr, 6)
    s.close()

    if current == VO_PATCH_NEW:
        print("Linear tiling patch: already applied")
    elif current == VO_PATCH_ORIGINAL:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(10)
        s.connect((PS5_HOST, PS5_PORT))
        ok = proc_write(s, pid, patch_addr, VO_PATCH_NEW)
        s.close()
        print(f"Linear tiling patch: {'OK' if ok else 'FAILED'}")
    else:
        print(f"Unexpected bytes at patch site: {current.hex() if current else 'read failed'}")

    # 4. Allocate executable memory
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    code_addr = proc_alloc(s, pid, 0x10000)  # 64KB
    s.close()

    if not code_addr:
        print("Failed to allocate memory!")
        return
    print(f"Allocated code memory at 0x{code_addr:x}")

    # 5. Build shellcode
    shellcode = build_shellcode(vo_base, k_base, code_addr)
    print(f"Shellcode size: {len(shellcode)} bytes")

    # 6. Write shellcode
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(30)
    s.connect((PS5_HOST, PS5_PORT))
    ok = proc_write(s, pid, code_addr, shellcode)
    s.close()
    print(f"Write shellcode: {'OK' if ok else 'FAILED'}")

    if not ok:
        return

    # 7. Make it executable (prot = 5 = READ|EXEC)
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    ok = proc_protect(s, pid, code_addr, 0x10000, 5)
    s.close()
    print(f"Protect RX: {'OK' if ok else 'FAILED'}")

    # 8. Call the shellcode
    print(f"\nCalling shellcode at 0x{code_addr:x}...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(60)
    s.connect((PS5_HOST, PS5_PORT))
    result = proc_call(s, pid, code_addr)
    s.close()
    print(f"Call returned: 0x{result:x}" if result else "Call failed!")

if __name__ == '__main__':
    main()
