#!/usr/bin/env python3
"""
Inject color bars into a running game by:
1. Finding libSceVideoOut and libkernel base addresses
2. Patching the linear tiling check in libSceVideoOut
3. Allocating RWX memory in the game
4. Writing the colorbars blob (code + rodata)
5. Writing a function pointer table after the blob
6. Calling colorbars_main(func_table) via CMD_PROC_CALL
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
VO_OPEN           = 0x14f80
VO_SET_ATTR       = 0x12710
VO_REGISTER       = 0x15140
VO_SUBMIT_FLIP    = 0x11a10
VO_ADD_FLIP_EVENT = 0x110d0
VO_GET_FLIP_STATUS = 0x12fd0

# Function offsets in libkernel.sprx (FW 5.50)
K_ALLOC_DIRECT   = 0x3de0
K_MAP_DIRECT     = 0xdac0
K_CREATE_EQUEUE  = 0x1ae60
K_WAIT_EQUEUE    = 0x9510
K_USLEEP         = 0x27770

# Patch offset in libSceVideoOut for linear tiling bypass
VO_PATCH_OFFSET = 0x7e61
VO_PATCH_ORIGINAL = bytes([0x0f, 0x84, 0x15, 0x02, 0x00, 0x00])
VO_PATCH_NEW = bytes([0x90, 0x90, 0x90, 0x90, 0x90, 0x90])

BLOB_PATH = '/Users/bizkut/Downloads/PS5/homebrew/ps4-freegnm/openagc/samples/hw_test/colorbars_blob.bin'
TRACE_OFFSET = 0x1964  # offset of trace[] in the blob

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
    body = struct.pack('<IQQQQQQQQ', pid, 0, rip, rdi, rsi, rdx, rcx, r8, r9)
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

def main():
    # Load the blob
    blob = open(BLOB_PATH, 'rb').read()
    blob_size = len(blob)
    print(f"Color bars blob: {blob_size} bytes")

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
        if 'kernel' in name.lower() and prot == 0x5:
            # Find the main libkernel module (not libSceKernelModule etc)
            if 'libkernel_sys' in name or name == 'libkernel_sys.sprx':
                k_base = start - offset
            elif k_base is None and 'kernel' in name.lower():
                k_base = start - offset

    if not vo_base:
        print("libSceVideoOut not found!")
        # List all modules for debugging
        for name, start, end, offset, prot in maps:
            if prot == 0x5:
                print(f"  RX: {name} at 0x{start:x}")
        return
    if not k_base:
        print("libkernel not found!")
        for name, start, end, offset, prot in maps:
            if prot == 0x5 and 'kernel' in name.lower():
                print(f"  kernel: {name} at 0x{start:x}")
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

    # 4. Build function pointer table
    # struct func_table { void* open, set_attr, register_bufs, submit_flip,
    #                     add_flip_event, alloc_direct, map_direct,
    #                     create_equeue, wait_equeue, usleep; }
    func_table = struct.pack('<11Q',
        vo_base + VO_OPEN,
        vo_base + VO_SET_ATTR,
        vo_base + VO_REGISTER,
        vo_base + VO_SUBMIT_FLIP,
        vo_base + VO_ADD_FLIP_EVENT,
        k_base + K_ALLOC_DIRECT,
        k_base + K_MAP_DIRECT,
        k_base + K_CREATE_EQUEUE,
        k_base + K_WAIT_EQUEUE,
        k_base + K_USLEEP,
        vo_base + VO_GET_FLIP_STATUS,
    )
    print(f"Function table: {len(func_table)} bytes")

    # 5. Allocate memory: blob + func_table + some padding
    total_size = blob_size + len(func_table) + 0x100
    # Round up to page
    total_size = (total_size + 0xFFF) & ~0xFFF

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    code_addr = proc_alloc(s, pid, total_size)
    s.close()

    if not code_addr:
        print("Failed to allocate memory!")
        return
    print(f"Allocated 0x{total_size:x} bytes at 0x{code_addr:x}")

    # 6. Write the blob
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(30)
    s.connect((PS5_HOST, PS5_PORT))
    ok = proc_write(s, pid, code_addr, blob)
    s.close()
    print(f"Write blob: {'OK' if ok else 'FAILED'}")
    if not ok: return

    # 7. Write the function table right after the blob
    table_addr = code_addr + blob_size
    # Align to 8 bytes
    table_addr = (table_addr + 7) & ~7
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    ok = proc_write(s, pid, table_addr, func_table)
    s.close()
    print(f"Write func table at 0x{table_addr:x}: {'OK' if ok else 'FAILED'}")
    if not ok: return

    # 8. Make it RWX (prot = 7)
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    ok = proc_protect(s, pid, code_addr, total_size, 7)
    s.close()
    print(f"Protect RWX: {'OK' if ok else 'FAILED'}")

    # 9. Call colorbars_main(func_table)
    # The function signature is: void colorbars_main(struct func_table *funcs)
    # In SysV ABI, first arg goes in rdi
    print(f"\nCalling colorbars_main at 0x{code_addr:x} with rdi=0x{table_addr:x}...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    result = proc_call(s, pid, code_addr, rdi=table_addr)
    s.close()
    print(f"Call returned: 0x{result:x}" if result is not None else "Call completed")

    # Wait a moment for colorbars_main to run, then read the trace buffer
    print("\nWaiting 5 seconds for code to run...")
    time.sleep(5)

    trace_addr = code_addr + TRACE_OFFSET
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    trace_data = proc_read(s, pid, trace_addr, 128)  # 16 * 8 bytes
    s.close()

    if trace_data:
        print("\nTrace buffer:")
        for i in range(16):
            val = struct.unpack('<Q', trace_data[i*8:i*8+8])[0]
            if val == 0:
                break
            print(f"  [{i}] 0x{val:016x}")
    else:
        print("Failed to read trace buffer")

if __name__ == '__main__':
    main()
