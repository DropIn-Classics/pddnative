#!/usr/bin/env python3
"""Unpack GOG's Pinball Dreams Deluxe CD image into a folder.

    gogx.py [GAME.GOG] [OUT]

game.gog is the data track of the CD as a raw MODE2/2352 image (game.inst
is its cue sheet; tracks 2 and 3 are the .ogg files in MUSIC).  Every data
sector is Mode 2 Form 1: 24 bytes of sync, header and subheader, then 2048
bytes of user data.  The ISO 9660 file system on it is read as is; file
names lose their ';1' version.  Defaults: the GOG folder on C: and game/
beside tools/.
"""
import os, struct, sys

RAW = 2352
DEFAULT_IMAGE = r'C:\GOG Games\Pinball Dreams Deluxe\game.gog'


class Image:
    def __init__(self, path):
        self.f = open(path, 'rb')

    def sector(self, n):
        self.f.seek(n * RAW)
        raw = self.f.read(RAW)
        mode = raw[15]
        if mode == 1:
            return raw[16:16 + 2048]
        if mode == 2:
            return raw[24:24 + 2048]
        raise ValueError(f'sector {n}: mode {mode}')

    def read(self, lba, size):
        data = b''.join(self.sector(lba + i) for i in range((size + 2047) // 2048))
        return data[:size]


def walk(img, lba, size, path=''):
    """-> (path, lba, size, is_dir) for every entry below a directory"""
    data = img.read(lba, size)
    i = 0
    while i < len(data):
        n = data[i]
        if n == 0:                          # records never cross a sector
            i = (i // 2048 + 1) * 2048
            continue
        rec = data[i:i + n]
        i += n
        name = rec[33:33 + rec[32]]
        if name in (b'\0', b'\1'):
            continue
        name = name.decode('ascii').split(';')[0]
        elba, esize = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
        p = f'{path}/{name}' if path else name
        isdir = bool(rec[25] & 2)
        yield p, elba, esize, isdir
        if isdir:
            yield from walk(img, elba, esize, p)


def unpack(image, out):
    img = Image(image)
    pvd = img.sector(16)
    if pvd[1:6] != b'CD001':
        raise ValueError('no ISO 9660 volume')
    root = pvd[156:156 + 34]
    n = 0
    for p, lba, size, isdir in walk(img, struct.unpack_from('<I', root, 2)[0],
                                    struct.unpack_from('<I', root, 10)[0]):
        dst = os.path.join(out, *p.split('/'))
        if isdir:
            os.makedirs(dst, exist_ok=True)
        else:
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, 'wb') as f:
                f.write(img.read(lba, size))
            n += 1
    return n


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    image = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_IMAGE
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, '..', 'game')
    print(f'{unpack(image, out)} files -> {os.path.normpath(out)}')


if __name__ == '__main__':
    main()
