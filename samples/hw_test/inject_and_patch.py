#!/usr/bin/env python3
"""
Inject videoout_linear.elf into a running game process (eboot.bin)
and patch libSceVideoOut to bypass the linear tiling check.
"""

import socket
import struct
import sys
import time

PACKET_MAGIC = 0xFFAABBCC
CMD_PROC_LIST = 0xBDAA0001
CMD_PROC_READ = 0xBDAA0002
CMD_PROC_WRITE = 0xBDAA0003
CMD_PROC_MAPS = 0xBDAA0004
CMD_PROC_ELF = 0xBDAA0007
CMD_DEBUG_PROCESS_STOP = 0xBDBB0500

def bitswap32(x):
    return ((x >> 1) & 0x55555555) | ((x << 1) & 0xAAAAAAAA)

CMD_SUCCESS_WIRE = bitswap32(0x40000000)
CMD_ERROR_WIRE = bitswap32(0x80000000)

PS5_HOST = '10.0.1.41'
PS5_PORT = 744

# Patch offsets in libSceVideoOut.sprx (FW 5.50)
PATCH_OFFSET = 0x7e61
PATCH_ORIGINAL = bytes([0x0f, 0x84, 0x15, 0x02, 0x00, 0x00])
PATCH_NEW = bytes([0x90, 0x90, 0x90, 0x90, 0x90, 0x90])

def send_packet(s, cmd, body=b''):
    s.send(struct.pack('<III', PACKET_MAGIC, cmd, len(body)) + body)

def recv_status(s):
    data = b''
    while len(data) < 4:
        data += s.recv(4 - len(data))
    return struct.unpack('<I', data)[0]

def is_success(status):
    return status == CMD_SUCCESS_WIRE

def list_processes(s):
    send_packet(s, CMD_PROC_LIST)
    status = recv_status(s)
    if not is_success(status):
        return []
    num = struct.unpack('<I', s.recv(4))[0]
    procs = []
    for _ in range(num):
        entry = b''
        while len(entry) < 36:
            entry += s.recv(36 - len(entry))
        name = entry[:32].split(b'\x00')[0].decode('ascii', errors='replace')
        pid = struct.unpack('<i', entry[32:36])[0]
        procs.append((pid, name))
    return procs

def get_proc_maps(s, pid):
    body = struct.pack('<I', pid)
    send_packet(s, CMD_PROC_MAPS, body)
    status = recv_status(s)
    if not is_success(status):
        return []
    num = struct.unpack('<I', s.recv(4))[0]
    maps = []
    for _ in range(num):
        entry = b''
        while len(entry) < 58:
            entry += s.recv(58 - len(entry))
        name = entry[:32].split(b'\x00')[0].decode('ascii', errors='replace')
        start = struct.unpack('<Q', entry[32:40])[0]
        end = struct.unpack('<Q', entry[40:48])[0]
        offset = struct.unpack('<Q', entry[48:56])[0]
        prot = struct.unpack('<H', entry[56:58])[0]
        maps.append((name, start, end, offset, prot))
    return maps

def proc_read(s, pid, addr, length):
    body = struct.pack('<IQI', pid, addr, length)
    send_packet(s, CMD_PROC_READ, body)
    status = recv_status(s)
    if not is_success(status):
        return None
    data = b''
    while len(data) < length:
        chunk = s.recv(min(length - len(data), 0x10000))
        if not chunk:
            break
        data += chunk
    return data

def proc_write(s, pid, addr, data):
    body = struct.pack('<IQI', pid, addr, len(data))
    send_packet(s, CMD_PROC_WRITE, body)
    status = recv_status(s)
    if not is_success(status):
        return False
    sent = 0
    while sent < len(data):
        chunk = data[sent:sent+0x10000]
        s.send(chunk)
        sent += len(chunk)
    status2 = recv_status(s)
    return is_success(status2)

def inject_elf(s, pid, elf_data):
    """Inject ELF into target process using CMD_PROC_ELF."""
    body = struct.pack('<II', pid, len(elf_data))
    send_packet(s, CMD_PROC_ELF, body)
    status = recv_status(s)
    if not is_success(status):
        print(f"  ELF inject status1 failed: 0x{status:08x}")
        return False
    # Send ELF data in chunks
    sent = 0
    while sent < len(elf_data):
        chunk = elf_data[sent:sent+0x10000]
        s.send(chunk)
        sent += len(chunk)
    # Second status word
    status2 = recv_status(s)
    if is_success(status2):
        print("  ELF injected successfully!")
        return True
    else:
        print(f"  ELF inject status2 failed: 0x{status2:08x}")
        return False

def find_vo_base(s, pid):
    """Find libSceVideoOut base address in target process."""
    maps = get_proc_maps(s, pid)
    for mname, start, end, offset, prot in maps:
        if 'VideoOut' in mname and prot == 0x5:
            return start - offset
    return None

def patch_vo(s, pid, vo_base):
    """Patch the linear tiling check in libSceVideoOut."""
    patch_addr = vo_base + PATCH_OFFSET
    
    # Read current bytes
    current = proc_read(s, pid, patch_addr, len(PATCH_ORIGINAL))
    if current is None:
        print("  Failed to read patch site!")
        return False
    
    print(f"  Current bytes: {current.hex()}")
    
    if current == PATCH_NEW:
        print("  Already patched!")
        return True
    
    if current != PATCH_ORIGINAL:
        print(f"  WARNING: Unexpected bytes (expected {PATCH_ORIGINAL.hex()})")
        if current[:2] != b'\x0f\x84':
            print("  Not a je instruction, aborting!")
            return False
    
    # Apply patch
    if proc_write(s, pid, patch_addr, PATCH_NEW):
        print("  Patch applied successfully!")
        return True
    else:
        print("  Patch FAILED!")
        return False

def main():
    elf_path = sys.argv[1] if len(sys.argv) > 1 else 'videoout_linear.elf'
    
    # Read ELF
    with open(elf_path, 'rb') as f:
        elf_data = f.read()
    print(f"ELF: {elf_path} ({len(elf_data)} bytes)")
    
    # 1. Find eboot.bin process
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    procs = list_processes(s)
    s.close()
    
    game_pid = None
    for pid, name in procs:
        if name == 'eboot.bin':
            print(f"Found game: pid={pid} name={name}")
            game_pid = pid
            break
    
    if game_pid is None:
        print("No eboot.bin process found! Launch a game first.")
        return
    
    # 2. Check if libSceVideoOut is already loaded in the game
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    vo_base = find_vo_base(s, game_pid)
    s.close()
    
    if vo_base:
        print(f"libSceVideoOut already loaded at 0x{vo_base:x}")
        # Patch it now
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(10)
        s.connect((PS5_HOST, PS5_PORT))
        patch_vo(s, game_pid, vo_base)
        s.close()
    else:
        print("libSceVideoOut not loaded yet — will patch after injection")
    
    # 3. Inject ELF into game process
    print(f"\nInjecting ELF into pid={game_pid}...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(30)
    s.connect((PS5_HOST, PS5_PORT))
    inject_elf(s, game_pid, elf_data)
    s.close()
    
    # 4. Wait for ELF to call sceVideoOutOpen and load libSceVideoOut
    print("\nWaiting 4s for sceVideoOutOpen...")
    time.sleep(4)
    
    # 5. Find and patch libSceVideoOut
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    vo_base = find_vo_base(s, game_pid)
    s.close()
    
    if vo_base is None:
        print("libSceVideoOut still not found! Checking all processes...")
        # Maybe the ELF loaded into a different process
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(10)
        s.connect((PS5_HOST, PS5_PORT))
        procs = list_processes(s)
        s.close()
        
        for pid, name in procs:
            if pid == 0:
                continue
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(5)
                s.connect((PS5_HOST, PS5_PORT))
                vo_base = find_vo_base(s, pid)
                s.close()
                if vo_base:
                    print(f"  Found libSceVideoOut in pid={pid} ({name}) at 0x{vo_base:x}")
                    game_pid = pid
                    break
            except:
                pass
        
        if vo_base is None:
            print("ERROR: libSceVideoOut not found in any process!")
            return
    
    # 6. Patch
    print(f"\nPatching libSceVideoOut at 0x{vo_base:x} in pid={game_pid}...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    patch_vo(s, game_pid, vo_base)
    s.close()
    
    print("\nDone! Color bars should appear on screen.")

if __name__ == '__main__':
    main()
