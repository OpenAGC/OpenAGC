#!/usr/bin/env python3
"""
Write color bars directly to the game's framebuffer using small chunks.
The game's flip loop will display our colors.
"""

import socket
import struct
import sys
import time

PACKET_MAGIC = 0xFFAABBCC
CMD_PROC_LIST = 0xBDAA0001
CMD_PROC_WRITE = 0xBDAA0003

def bitswap32(x):
    return ((x >> 1) & 0x55555555) | ((x << 1) & 0xAAAAAAAA)
CMD_SUCCESS_WIRE = bitswap32(0x40000000)

PS5_HOST = '10.0.1.41'
PS5_PORT = 744

FB_WIDTH = 3840
FB_HEIGHT = 2160
FB_BPP = 4

COLORS = [
    0xFFFFFFFF,  # White
    0xFFFFFF00,  # Yellow
    0xFF00FFFF,  # Cyan
    0xFF00FF00,  # Green
    0xFFFF00FF,  # Magenta
    0xFFFF0000,  # Red
    0xFF0000FF,  # Blue
    0xFF000000,  # Black
]

def send_packet(s, cmd, body=b''):
    s.send(struct.pack('<III', PACKET_MAGIC, cmd, len(body)) + body)

def recv_status(s):
    data = b''
    while len(data) < 4:
        data += s.recv(4 - len(data))
    return struct.unpack('<I', data)[0]

def proc_write(s, pid, addr, data):
    body = struct.pack('<IQI', pid, addr, len(data))
    s.send(struct.pack('<III', PACKET_MAGIC, CMD_PROC_WRITE, len(body)) + body)
    if recv_status(s) != CMD_SUCCESS_WIRE:
        return False
    sent = 0
    while sent < len(data):
        chunk = data[sent:sent+0x10000]
        s.send(chunk)
        sent += len(chunk)
    return recv_status(s) == CMD_SUCCESS_WIRE

def find_game_pid(s):
    send_packet(s, CMD_PROC_LIST)
    recv_status(s)
    num = struct.unpack('<I', s.recv(4))[0]
    for _ in range(num):
        entry = b''
        while len(entry) < 36:
            entry += s.recv(36 - len(entry))
        name = entry[:32].split(b'\x00')[0].decode('ascii', errors='replace')
        pid = struct.unpack('<i', entry[32:36])[0]
        if name == 'eboot.bin':
            return pid
    return None

def generate_line(y, width):
    """Generate one line of color bars."""
    bar_width = width // len(COLORS)
    data = bytearray()
    for x in range(width):
        bar_idx = min(x // bar_width, len(COLORS) - 1)
        data += struct.pack('<I', COLORS[bar_idx])
    return bytes(data)

def main():
    # Find game process
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((PS5_HOST, PS5_PORT))
    pid = find_game_pid(s)
    s.close()

    if not pid:
        print("No eboot.bin process found!")
        return

    print(f"Game process: pid={pid}")

    # Framebuffer addresses
    fb_addrs = [0x8fc0000000, 0x8fc2000000]

    # Generate one line of color bars
    line_data = generate_line(0, FB_WIDTH)
    line_size = len(line_data)  # 15360 bytes

    # Write 4 lines per chunk (61440 bytes, under 64KB limit)
    lines_per_chunk = 4
    chunk_data = line_data * lines_per_chunk
    chunk_size = len(chunk_data)

    for fb_idx, fb_addr in enumerate(fb_addrs):
        print(f"\nWriting color bars to framebuffer {fb_idx} at 0x{fb_addr:x}...")
        t0 = time.time()
        total_written = 0

        for start_line in range(0, FB_HEIGHT, lines_per_chunk):
            offset = start_line * line_size

            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(10)
            s.connect((PS5_HOST, PS5_PORT))

            ok = proc_write(s, pid, fb_addr + offset, chunk_data)
            s.close()
            total_written += chunk_size

            if not ok:
                print(f"  FAILED at line {start_line}!")
                break

            if start_line % 200 == 0:
                elapsed = time.time() - t0
                pct = (start_line / FB_HEIGHT) * 100
                print(f"  {pct:5.1f}% (line {start_line}/{FB_HEIGHT}) {elapsed:.1f}s")

        elapsed = time.time() - t0
        print(f"  Framebuffer {fb_idx} done in {elapsed:.1f}s ({total_written} bytes)")

    print("\nColor bars written! Check your screen.")

if __name__ == '__main__':
    main()
