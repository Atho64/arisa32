#!/usr/bin/env python3
"""MES command-stream verifier/disassembler for D.C.4 (adv32).

Authoritative model from IDA (sub_438790 = the VM fetcher, 0x438790):
  op 0x00        : nop, no advance
  op 0x01-0x3A   : u8, u8                        (3 bytes total)
  op 0x3B-0x47   : u8 param + NUL-string         (2 + strlen)
  op 0x48-0x68   : NUL-string raw                (1 + strlen)
  op 0x69-0x6D   : NUL-string, text: stored = real - 0x20
  op 0x6E-0xFF   : u16 x4                        (9 bytes total)
  op 0x77 ('w')  : wait modifier, consumed together with the PREVIOUS command
Labels (u32, low 24 bits) are relative to code start (= version byte 0xB6).
"""
import glob
import struct
import sys
import collections


def decode_text(raw: bytes) -> str:
    return bytes(((b + 0x20) & 0xFF) for b in raw).decode('cp932', 'replace')


def scan(code, start):
    """Yield (pos, kind, payload) walking from a label. Returns ('FAIL', op, pos) on error."""
    pos = start
    n = len(code)
    events = []
    while pos < n:
        op = code[pos]
        if op == 0x00:
            return events, pos
        if 0x01 <= op <= 0x3A:
            if pos + 3 > n:
                return ('FAIL', op, pos), None
            events.append((pos, 'cmd', (op, code[pos + 1], code[pos + 2])))
            pos += 3
        elif 0x3B <= op <= 0x47:
            end = code.find(b'\x00', pos + 2)
            if end < 0:
                return ('FAIL', op, pos), None
            events.append((pos, 'cmd_str', (op, code[pos + 1], code[pos + 2:end])))
            pos = end + 1
        elif 0x48 <= op <= 0x68:
            end = code.find(b'\x00', pos + 1)
            if end < 0:
                return ('FAIL', op, pos), None
            events.append((pos, 'str', (op, code[pos + 1:end])))
            pos = end + 1
        elif 0x69 <= op <= 0x6D:
            end = code.find(b'\x00', pos + 1)
            if end < 0:
                return ('FAIL', op, pos), None
            try:
                txt = decode_text(code[pos + 1:end])
            except Exception:
                return ('FAIL', op, pos), None
            events.append((pos, 'text', (op, txt)))
            pos = end + 1
        else:  # 0x6E-0xFF: 4x u16 (0x77 handled as modifier by engine)
            if pos + 9 > n:
                return ('FAIL', op, pos), None
            w = struct.unpack_from('<4H', code, pos + 1)
            events.append((pos, 'cmd_w', (op,) + w))
            pos += 9
    return events, pos


def main():
    files = glob.glob(r"G:\DC4 new\DC4\AdvData\MES\*.mes")
    ok = fail = 0
    fail_examples = []
    op_freq = collections.Counter()
    text_count = 0
    for fp in files:
        d = open(fp, 'rb').read()
        count = struct.unpack_from('<I', d, 0)[0]
        if count == 0 or count * 6 + 8 > len(d):
            continue
        te = count * 6 + 4
        offs = sorted({o & 0xFFFFFF for o in struct.unpack_from('<%dI' % count, d, 4)})
        code = d[te:]
        for i, o in enumerate(offs):
            nxt = offs[i + 1] if i + 1 < len(offs) else len(code)
            if o >= len(code):
                continue
            ev, endpos = scan(code, o)
            if isinstance(ev, tuple) and ev[0] == 'FAIL':
                fail += 1
                if len(fail_examples) < 10:
                    fail_examples.append((fp, o, ev[1], ev[2]))
                continue
            # must land on next label (or padding before it)
            if endpos <= nxt:
                ok += 1
                for p, k, payload in ev:
                    if k == 'text':
                        text_count += 1
                    elif k in ('cmd', 'cmd_w'):
                        op_freq[payload[0]] += 1
                    elif k == 'str':
                        op_freq[payload[0]] += 1
                    elif k == 'cmd_str':
                        op_freq[payload[0]] += 1
            else:
                fail += 1
                if len(fail_examples) < 10:
                    fail_examples.append((fp, o, 'LAND', endpos - nxt))
    print(f"label walks: clean {ok}, failed {fail}")
    print(f"text runs decoded: {text_count}")
    print("top commands:", {hex(k): v for k, v in op_freq.most_common(15)})
    for e in fail_examples:
        print("FAIL:", e)


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[1] == 'dis':
        # dis <file.mes> — verified-model disassembly
        d = open(sys.argv[2], 'rb').read()
        count = struct.unpack_from('<I', d, 0)[0]
        te = count * 6 + 4
        offs = sorted({o & 0xFFFFFF for o in struct.unpack_from('<%dI' % count, d, 4)})
        code = d[te:]
        ev, end = scan(code, offs[0] if offs else 3)
        for p, k, payload in ev:
            lbl = "  L%d" % p if p in set(offs) else ""
            if k == 'text':
                print(f"{p:6d}  TEXT  {payload[1]}{lbl}")
            elif k == 'str':
                print(f"{p:6d}  STR   0x{payload[0]:02X} {payload[1].decode('cp932','replace')!r}{lbl}")
            elif k == 'cmd_str':
                print(f"{p:6d}  CMD   0x{payload[0]:02X} param={payload[1]} str={payload[2].decode('cp932','replace')!r}{lbl}")
            elif k == 'cmd_w':
                print(f"{p:6d}  CMD   0x{payload[0]:02X} words={payload[1:]}{lbl}")
            else:
                print(f"{p:6d}  CMD   0x{payload[0]:02X} ({payload[1]}, {payload[2]}){lbl}")
        sys.exit(0)
    main()
