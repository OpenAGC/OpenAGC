#!/usr/bin/env python3
"""
Patch libSceVideoOut.sprx in a running PS5 process to bypass the
linear tiling mode check.

The check at offset 0x7e61 in libSceVideoOut.sprx (FW 5.50):
  7e61: je 0x807c   (0f 84 15 02 00 00)
If [port+0x700] == 0, it jumps to the linear tiling rejection at 0x807c.
We NOP this jump so it falls through to 0x7e67 (continue normally).

Also patch the rejection itself at 0x807c:
  807c: cmp dword ptr [rbp-0x110], 0x2  (83 bd f0 fe ff ff 02)
Change the 0x2 to 0xff so it never matches linear mode 2.
(But since we NOP'd the jump, this is just a belt-and-suspenders fix.)
"""

import socket
import struct
import sys

PACKET_MAGIC = 0xFFAABBCC
CMD_PROC_LIST = 0xBDAA0001
CMD_PROC_READ = 0xBDAA0002
CMD_PROC_WRITE = 0xBDAA0003
CMD_PROC_MAPS = 0xBDAA0004

def bitswap32(x):
    return ((x >> 1) & 0x55555555) | ((x << 1) & 0xAAAAAAAA)

CMD_SUCCESS_WIRE = bitswap32(0x40000000)
CMD_ERROR_WIRE = bitswap32(0x80000000)

PS5_HOST = '10.0.1.41'
PS5_PORT = 744

# Offsets in libSceVideoOut.sprx (FW 5.50)
# The je at 0x7e61: 0f 84 15 02 00 00 -> NOP (6 bytes)
PATCH_OFFSET = 0x7e61
PATCH_ORIGINAL = bytes([0x0f, 0x84, 0x15, 0x02, 0x00, 0x00])
PATCH_NEW = bytes([0x90, 0x90, 0x90, 0x90, 0x90, 0x90])  # 6x NOP

# Also patch the cmp at 0x807c: change 0x2 to 0xff
PATCH2_OFFSET = 0x807c + 7  # the immediate byte 0x02 is at offset +7
PATCH2_ORIGINAL = bytes([0x02])
PATCH2_NEW = bytes([0xff])

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
        print(f"PROC_LIST failed: 0x{status:08x}")
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
        print(f"PROC_MAPS failed: 0x{status:08x}")
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
        print(f"PROC_READ failed: 0x{status:08x}")
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
        print(f"PROC_WRITE status1 failed: 0x{status:08x}")
        return False
    # Send data in 64KiB chunks
    sent = 0
    while sent < len(data):
        chunk = data[sent:sent+0x10000]
        s.send(chunk)
        sent += len(chunk)
    # Second status word
    status2 = recv_status(s)
    if not is_success(status2):
        print(f"PROC_WRITE status2 failed: 0x{status2:08x}")
        return False
    return True

def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))

    # 1. List processes
    print("Listing processes...")
    procs = list_processes(s)
    s.close()

    # Search all processes for libSceVideoOut
    target_pid = None
    vo_base = None

    for pid, name in procs:
        if pid == 0:  # skip kernel
            continue
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(5)
            s.connect((PS5_HOST, PS5_PORT))
            maps = get_proc_maps(s, pid)
            s.close()

            for mname, start, end, offset, prot in maps:
                if 'VideoOut' in mname and prot == 0x5:  # executable segment
                    print(f"  Found libSceVideoOut in pid={pid} ({name}): 0x{start:x} offset=0x{offset:x}")
                    target_pid = pid
                    vo_base = start - offset
                    break
        except Exception as e:
            pass

        if target_pid:
            break

    if target_pid is None:
        print("libSceVideoOut not found in any process!")
        print("Make sure the ELF is running and has called sceVideoOutOpen.")
        return

    pid = target_pid
    print(f"\nTarget: pid={pid}, libSceVideoOut base: 0x{vo_base:x}")

    # 3. Read current bytes at patch location to verify
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))

    patch_addr = vo_base + PATCH_OFFSET
    print(f"\nReading bytes at 0x{patch_addr:x} (offset 0x{PATCH_OFFSET:x})...")
    current = proc_read(s, pid, patch_addr, len(PATCH_ORIGINAL))
    s.close()

    if current is None:
        print("Failed to read!")
        return

    print(f"  Current: {current.hex()}")
    print(f"  Expected: {PATCH_ORIGINAL.hex()}")

    if current != PATCH_ORIGINAL:
        print("WARNING: Bytes don't match expected pattern!")
        # Try anyway if we're confident
        if current[:2] == b'\x0f\x84':  # still a je
            print("  Still a je instruction, proceeding with patch...")
        else:
            print("  Aborting - unknown code at patch site")
            return

    # 4. Apply patch
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))

    print(f"\nPatching je -> nop at 0x{patch_addr:x}...")
    if proc_write(s, pid, patch_addr, PATCH_NEW):
        print("  Patch 1 applied successfully!")
    else:
        print("  Patch 1 FAILED!")
        s.close()
        return
    s.close()

    # 5. Apply second patch (change cmp value from 0x2 to 0xff)
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))

    patch2_addr = vo_base + PATCH2_OFFSET
    print(f"Patching cmp 0x2 -> 0xff at 0x{patch2_addr:x}...")
    if proc_write(s, pid, patch2_addr, PATCH2_NEW):
        print("  Patch 2 applied successfully!")
    else:
        print("  Patch 2 FAILED!")
    s.close()

    # 6. Verify patches
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))

    print("\nVerifying patches...")
    verify = proc_read(s, pid, patch_addr, 8)
    s.close()

    if verify:
        print(f"  Bytes at 0x{patch_addr:x}: {verify.hex()}")
        if verify[:6] == PATCH_NEW:
            print("  Patch verified: je -> nop OK!")
        else:
            print("  Patch verification FAILED!")

    print("\nDone! Now load your ELF via the web elfldr.")

if __name__ == '__main__':
    main()
