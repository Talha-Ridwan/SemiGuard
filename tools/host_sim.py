#!/usr/bin/env python3
# Minimal SECS/GEM "host" for manually exercising SemiGuardApp over HSMS/TCP.
# Connects to the equipment, drives the Select handshake, sends S1F13 and
# S2F41 (STOP then START), and prints every frame it receives, decoded.
#
# Usage: python3 tools/host_sim.py [host] [port]   (default 127.0.0.1 5000)
import socket
import struct
import sys
import threading
import time

FMT_L, FMT_B, FMT_A, FMT_U1, FMT_U4, FMT_F4 = 0x00, 0x08, 0x10, 0x29, 0x2C, 0x24

SESSION_CTRL = 0xFFFF
SESSION_DATA = 0x0001

STYPE_NAME = {0: "Data", 1: "Select.req", 2: "Select.rsp", 5: "Linktest.req", 6: "Linktest.rsp"}


# ---------- SECS-II item builders ----------
def item_leaf(code, payload):
    return bytes([(code << 2) | 1, len(payload)]) + payload

def item_list(children):
    return bytes([(FMT_L << 2) | 1, len(children)]) + b"".join(children)

def i_ascii(s):  return item_leaf(FMT_A, s.encode("ascii"))
def i_binary(v): return item_leaf(FMT_B, bytes([v]))
def i_u4(v):     return item_leaf(FMT_U4, struct.pack(">I", v))
def i_f4(v):     return item_leaf(FMT_F4, struct.pack(">f", v))


# ---------- SECS-II decoder (mirror of SecsItem::decodeAt) ----------
def decode_at(b, pos):
    fmt_byte = b[pos]; pos += 1
    code = fmt_byte >> 2
    nlen = fmt_byte & 0x03
    length = int.from_bytes(b[pos:pos + nlen], "big"); pos += nlen
    if code == FMT_L:
        parts = []
        for _ in range(length):
            child, pos = decode_at(b, pos)
            parts.append(child)
        return "L[" + ", ".join(parts) + "]", pos
    payload = b[pos:pos + length]; pos += length
    if code == FMT_A:  return f'A"{payload.decode("ascii", "replace")}"', pos
    if code == FMT_B:  return f"B {list(payload)}", pos
    if code == FMT_U4: return f"U4 {int.from_bytes(payload, 'big')}", pos
    if code == FMT_U1: return f"U1 {payload[0] if payload else 0}", pos
    if code == FMT_F4: return f"F4 {struct.unpack('>f', payload)[0]:.2f}", pos
    return f"?0x{code:02X} {list(payload)}", pos

def decode_secs(body):
    if not body:
        return "(empty)"
    try:
        text, _ = decode_at(body, 0)
        return text
    except Exception as e:
        return f"(undecodable: {e})"


# ---------- HSMS framing ----------
def pack_header(session, stream, function, wbit, stype, sysbytes):
    return bytes([
        (session >> 8) & 0xFF, session & 0xFF,
        (0x80 if wbit else 0x00) | (stream & 0x7F),
        function & 0xFF,
        0x00,
        stype & 0xFF,
        (sysbytes >> 24) & 0xFF, (sysbytes >> 16) & 0xFF,
        (sysbytes >> 8) & 0xFF, sysbytes & 0xFF,
    ])

def send_frame(sock, header10, body=b""):
    length = len(header10) + len(body)
    sock.sendall(struct.pack(">I", length) + header10 + body)

def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf

def recv_frame(sock):
    lenbuf = recv_exact(sock, 4)
    if lenbuf is None:
        return None
    length = struct.unpack(">I", lenbuf)[0]
    hdr = recv_exact(sock, 10)
    if hdr is None:
        return None
    body = b""
    if length > 10:
        body = recv_exact(sock, length - 10)
        if body is None:
            return None
    return dict(
        session=(hdr[0] << 8) | hdr[1],
        wbit=bool(hdr[2] & 0x80),
        stream=hdr[2] & 0x7F,
        function=hdr[3],
        stype=hdr[5],
        sysbytes=int.from_bytes(hdr[6:10], "big"),
        body=body,
    )


def label(fr):
    if fr["stype"] == 0:
        return f'S{fr["stream"]}F{fr["function"]}{"W" if fr["wbit"] else ""}'
    return STYPE_NAME.get(fr["stype"], f'stype{fr["stype"]}')


def reader_loop(sock, stop):
    while not stop.is_set():
        fr = recv_frame(sock)
        if fr is None:
            print("<-- (server closed connection)")
            return
        print(f'<-- {label(fr):<12} sys={fr["sysbytes"]:<3} {decode_secs(fr["body"])}')


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 5000

    sock = socket.create_connection((host, port))
    print(f"connected to {host}:{port}")

    stop = threading.Event()
    reader = threading.Thread(target=reader_loop, args=(sock, stop), daemon=True)
    reader.start()

    time.sleep(0.4)  # let the pre-sent S6F11 event reports land first

    print("--> Select.req")
    send_frame(sock, pack_header(SESSION_CTRL, 0, 0, False, 1, 1))
    time.sleep(0.3)

    print('--> S1F13  L[A"SEMIGUARD", A"1.0"]')
    send_frame(sock, pack_header(SESSION_DATA, 1, 13, True, 0, 2),
               item_list([i_ascii("SEMIGUARD"), i_ascii("1.0")]))
    time.sleep(0.3)

    print('--> S2F41  STOP   (reset ALARM -> IDLE)')
    send_frame(sock, pack_header(SESSION_DATA, 2, 41, True, 0, 3),
               item_list([i_ascii("STOP"), item_list([])]))
    time.sleep(0.3)

    print('--> S2F41  START')
    send_frame(sock, pack_header(SESSION_DATA, 2, 41, True, 0, 4),
               item_list([i_ascii("START"), item_list([])]))
    time.sleep(0.6)

    stop.set()
    sock.close()
    print("host done, socket closed")


if __name__ == "__main__":
    main()
