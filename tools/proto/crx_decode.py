#!/usr/bin/env python3
"""CRX (CRXG) image decoder for the CIRCUS adv32 engine — prototype.

Port of GARbro ArcFormats/Circus/ImageCRX.cs (MIT, morkt) to Python for
validation against D.C.4 assets. The C++ core decoder will be written from
the same spec (see docs/findings.md).

Usage: crx_decode.py <file.crx> [more.crx ...]   -> writes <file>.png next to it
"""
import struct
import sys
import zlib

from PIL import Image


class Src:
    def __init__(self, data, pos):
        self.d = data
        self.p = pos

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u16(self):
        v = struct.unpack_from('<H', self.d, self.p)[0]
        self.p += 2
        return v

    def i32(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]
        self.p += 4
        return v

    def read(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v


def unpack_v1(s, out):
    window = bytearray(0x10000)
    flag = 0
    win_pos = 0
    dst = 0
    n = len(out)
    while dst < n:
        flag >>= 1
        if 0 == (flag & 0x100):
            flag = s.u8() | 0xff00
        if flag & 1:
            dat = s.u8()
            window[win_pos] = dat
            win_pos = (win_pos + 1) & 0xffff
            out[dst] = dat
            dst += 1
        else:
            control = s.u8()
            if control >= 0xC0:
                offset = ((control & 3) << 8) | s.u8()
                count = 4 + ((control >> 2) & 0xF)
            elif control & 0x80:
                offset = control & 0x1F
                count = 2 + ((control >> 5) & 3)
                if offset == 0:
                    offset = s.u8()
            elif control == 0x7F:
                count = 2 + s.u16()
                offset = s.u16()
            else:
                offset = s.u16()
                count = control + 4
            offset = win_pos - offset
            k = 0
            while k < count and dst < n:
                offset &= 0xffff
                dat = window[offset]
                offset += 1
                window[win_pos] = dat
                win_pos = (win_pos + 1) & 0xffff
                out[dst] = dat
                dst += 1
                k += 1
    return out


def unpack_v2(raw, out, w, h, stride, bpp):
    pixel_size = bpp // 8
    src_stride = w * pixel_size
    s = Src(raw, 0)
    if bpp >= 24:
        for y in range(h):
            ctl = s.u8()
            dst = y * stride
            prev = dst - stride
            if ctl == 0:      # horizontal delta
                out[dst:dst + pixel_size] = s.read(pixel_size)
                for x in range(pixel_size, src_stride):
                    out[dst + x] = (s.u8() + out[dst + x - pixel_size]) & 0xFF
            elif ctl == 1:    # vertical delta
                for x in range(src_stride):
                    out[dst + x] = (s.u8() + out[prev + x]) & 0xFF
            elif ctl == 2:    # diagonal delta
                out[dst:dst + pixel_size] = s.read(pixel_size)
                for x in range(pixel_size, src_stride):
                    out[dst + x] = (s.u8() + out[prev + x - pixel_size]) & 0xFF
            elif ctl == 3:    # right-to-left vertical delta
                d = dst
                p = prev
                for _ in range(src_stride - pixel_size, 0, -1):
                    out[d] = (s.u8() + out[p + pixel_size]) & 0xFF
                    d += 1
                    p += 1
                out[d:d + pixel_size] = s.read(pixel_size)
            elif ctl == 4:    # RLE per channel
                for ch in range(pixel_size):
                    d = dst + ch
                    wd = w
                    val = s.u8()
                    while wd > 0:
                        out[d] = val
                        d += pixel_size
                        wd -= 1
                        if wd == 0:
                            break
                        nxt = s.u8()
                        if val == nxt:
                            count = s.u8()
                            for _ in range(count):
                                out[d] = val
                                d += pixel_size
                            wd -= count
                            if wd > 0:
                                val = s.u8()
                        else:
                            val = nxt
            # other: row left as-is
    else:
        dst = 0
        for _ in range(h):
            out[dst:dst + src_stride] = s.read(src_stride)
            dst += stride
    return out


def decode(data):
    if data[:4] != b'CRXG':
        raise ValueError('not CRXG: %r' % data[:4])
    off_x, off_y, w, h, comp, flags, depth, mode = struct.unpack_from(
        '<hhHHHHhH', data, 4)
    bpp = 24 if depth == 0 else 32 if depth == 1 else 8
    if comp < 1 or comp > 3:
        raise ValueError('bad compression %d' % comp)
    pos = 0x14
    palette = None
    if bpp == 8:
        colors = depth
        csize = 4 if colors == 0x102 else 3
        if colors > 0x100:
            colors = 0x100
        pal = data[pos:pos + colors * csize]
        pos += colors * csize
        palette = []
        for i in range(colors):
            r, g, b = pal[i * csize], pal[i * csize + 1], pal[i * csize + 2]
            if r == 0xFF and g == 0 and b == 0xFF:
                g = 0xFF
            palette += [r, g, b]
    if comp >= 3:
        count = struct.unpack_from('<i', data, pos)[0]
        pos += 4 + count * 0x10
    if flags & 0x10:
        pos += 4
    stride = (w * bpp // 8 + 3) & ~3
    out = bytearray(h * stride)
    if comp == 1:
        unpack_v1(Src(data, pos), out)
    else:
        raw = zlib.decompress(data[pos:])
        unpack_v2(raw, out, w, h, stride, bpp)
    if bpp == 32 and mode != 1:
        flip = 0x00 if mode == 2 else 0xFF
        for row in range(h):
            base = row * stride
            for x in range(w):
                p = base + x * 4
                a, b, g, r = out[p], out[p + 1], out[p + 2], out[p + 3]
                out[p], out[p + 1], out[p + 2], out[p + 3] = b, g, r, a ^ flip
    if bpp == 8:
        img = Image.frombytes('P', (w, h), bytes(out), 'raw', 'P', stride)
        pal = palette + [0] * (768 - len(palette))
        img.putpalette(pal)
    elif bpp == 24:
        img = Image.frombytes('RGB', (w, h), bytes(out), 'raw', 'BGR', stride)
    else:
        img = Image.frombytes('RGBA', (w, h), bytes(out), 'raw', 'BGRA', stride)
    meta = dict(off=(off_x, off_y), size=(w, h), comp=comp, flags=flags,
                colors=depth, mode=mode, bpp=bpp)
    return img, meta


def main():
    for path in sys.argv[1:]:
        with open(path, 'rb') as f:
            data = f.read()
        try:
            img, meta = decode(data)
        except Exception as e:
            print('%-40s FAIL: %s' % (path, e))
            continue
        outp = path.rsplit('.', 1)[0] + '.png'
        img.save(outp)
        print('%-40s %dx%d bpp=%d comp=%d flags=0x%x colors=%d mode=%d -> %s'
              % (path, meta['size'][0], meta['size'][1], meta['bpp'],
                 meta['comp'], meta['flags'], meta['colors'], meta['mode'], outp))


if __name__ == '__main__':
    main()
