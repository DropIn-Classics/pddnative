#!/usr/bin/env python3
"""Inspect the data files read by PD.EXE and PD2.EXE.

    pdfiles.py map FILE [--png OUT]
    pdfiles.py hiscores FILE
    pdfiles.py options FILE

``map`` decodes a TBLDETLO/TBLDETHI collision map, prints its surface
and special-line counts, and checks that encoding the decoded map gives
the input bytes exactly.  ``--png`` writes a 320 by 512 false-colour
view using only the Python standard library.  The other commands decode
HISCORES.PD1/PD2 and the 13-byte DDPCOPTN.BIN written by DDPCMAIN.EXE.
"""
import argparse
from collections import Counter
import os
import struct
import zlib


ROWS = 512
WIDTH = 320
HISCORE_TABLES = 4
HISCORES_PER_TABLE = 4
HISCORE_SIZE = 9

TABLE_NAMES = {
    'HISCORES.PD1': ('Ignition', 'Steel Wheel', 'Beat Box', 'Nightmare'),
    'HISCORES.PD2': ('Neptune', 'Safari',
                     'Revenge of the Robot Warriors', 'Stall Turn'),
}

MAP_CODES = {
    0xFB: 'gravity 9',
    0xFC: 'gravity 16',
    0xFD: 'lower level',
    0xFE: 'upper level',
}

KEY_NAMES = {
    0x2A: 'Left Shift',
    0x36: 'Right Shift',
    0x39: 'Space',
    0xD0: 'E0 Down',
}


def read_file(path):
    with open(path, 'rb') as f:
        return f.read()


def parse_map(data):
    """Return 512 rows, each containing its low and high x-half.

    A half is a list of (surface, angle, x-low-byte) tuples.  Keeping
    the file's order makes the encoder an independent round-trip check,
    rather than rebuilding a merely equivalent dense bitmap.
    """
    pos = 0
    rows = []
    for y in range(ROWS):
        halves = []
        for high_half in range(2):
            if pos >= len(data):
                raise ValueError(f'truncated at row {y}, half {high_half}')
            if data[pos] == 0xFF:
                pos += 1
                halves.append([])
                continue

            surfaces = []
            while True:
                if pos >= len(data):
                    raise ValueError(f'truncated surface list at row {y}')
                value = data[pos]
                pos += 1
                surfaces.append(value & 0x7F)
                if value & 0x80:
                    break
                if len(surfaces) == 256:
                    raise ValueError(f'unterminated surface list at row {y}')

            count = len(surfaces)
            if pos + 2 * count > len(data):
                raise ValueError(f'truncated angle/x arrays at row {y}')
            angles = data[pos:pos + count]
            xs = data[pos + count:pos + 2 * count]
            pos += 2 * count

            pixels = []
            for surface, angle, xlow in zip(surfaces, angles, xs):
                x = xlow + (256 if high_half else 0)
                if x >= WIDTH:
                    raise ValueError(f'pixel outside 320x512 at ({x},{y})')
                pixels.append((surface, angle, xlow))
            halves.append(pixels)
        rows.append(tuple(halves))

    if pos != len(data):
        raise ValueError(f'{len(data) - pos} trailing bytes after row 511')
    return rows


def encode_map(rows):
    if len(rows) != ROWS:
        raise ValueError(f'expected {ROWS} rows, got {len(rows)}')
    out = bytearray()
    for y, halves in enumerate(rows):
        if len(halves) != 2:
            raise ValueError(f'row {y} does not have two halves')
        for pixels in halves:
            if not pixels:
                out.append(0xFF)
                continue
            for i, (surface, _angle, _xlow) in enumerate(pixels):
                if not 0 <= surface < 0x80:
                    raise ValueError(f'bad surface {surface:#x} at row {y}')
                out.append(surface | (0x80 if i == len(pixels) - 1 else 0))
            out.extend(angle for _surface, angle, _xlow in pixels)
            out.extend(xlow for _surface, _angle, xlow in pixels)
    return bytes(out)


def map_counts(rows):
    surfaces = Counter()
    special = Counter()
    for halves in rows:
        for pixels in halves:
            for surface, angle, _xlow in pixels:
                surfaces[surface] += 1
                if surface == 0x1F:
                    special[angle] += 1
    return surfaces, special


def png_chunk(kind, data):
    body = kind + data
    return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body))


def surface_colour(surface):
    """A stable bright RGB colour, distinct for the map's byte values."""
    # These odd multipliers spread adjacent surface values through RGB
    # space.  Components start at 48 so every surface stands out on black.
    return (48 + surface * 97 % 208,
            48 + surface * 57 % 208,
            48 + surface * 23 % 208)


def write_map_png(path, rows):
    image = bytearray(WIDTH * ROWS * 3)
    drawn = set()
    for y, halves in enumerate(rows):
        for high_half, pixels in enumerate(halves):
            for surface, _angle, xlow in pixels:
                x = xlow + 256 * high_half
                if (x, y) in drawn:
                    # map_test returns the first entry at a coordinate
                    continue
                drawn.add((x, y))
                off = (y * WIDTH + x) * 3
                image[off:off + 3] = bytes(surface_colour(surface))
    raw = b''.join(b'\0' + image[y * WIDTH * 3:(y + 1) * WIDTH * 3]
                   for y in range(ROWS))
    png = (b'\x89PNG\r\n\x1a\n' +
           png_chunk(b'IHDR', struct.pack('>IIBBBBB', WIDTH, ROWS,
                                           8, 2, 0, 0, 0)) +
           png_chunk(b'IDAT', zlib.compress(raw, 9)) +
           png_chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)


def describe_angle(angle):
    if angle in MAP_CODES:
        return MAP_CODES[angle]
    if angle <= 0x0F:
        return f'event {angle}'
    return 'unknown'


def command_map(path, png_path):
    data = read_file(path)
    rows = parse_map(data)
    encoded = encode_map(rows)
    if encoded != data:
        raise ValueError('decoded map did not encode to identical bytes')
    surfaces, special = map_counts(rows)
    total = sum(surfaces.values())
    print(f'{path}: {WIDTH}x{ROWS}, {total} surface pixels, {len(data)} bytes')
    for surface, count in sorted(surfaces.items()):
        print(f'  surface {surface:02X}: {count}')
    if special:
        print('  surface 1F angles:')
        for angle, count in sorted(special.items()):
            print(f'    {angle:02X} ({describe_angle(angle)}): {count}')
    print('  round trip: identical')
    if png_path:
        write_map_png(png_path, rows)
        print(f'  png: {png_path}')


def bcd_score(data):
    digits = []
    for value in data:
        high, low = value >> 4, value & 0x0F
        if high > 9 or low > 9:
            raise ValueError(f'invalid packed BCD byte {value:02X}')
        digits.extend((str(high), str(low)))
    return int(''.join(digits))


def parse_hiscores(data):
    expected = HISCORE_TABLES * HISCORES_PER_TABLE * HISCORE_SIZE
    if len(data) != expected:
        raise ValueError(f'expected {expected} bytes, got {len(data)}')
    tables = []
    pos = 0
    for _table in range(HISCORE_TABLES):
        entries = []
        for _entry in range(HISCORES_PER_TABLE):
            raw_name = data[pos:pos + 3]
            try:
                letters = raw_name.decode('ascii')
            except UnicodeDecodeError as e:
                raise ValueError(f'non-ASCII initials at offset {pos:#x}') from e
            score = bcd_score(data[pos + 3:pos + HISCORE_SIZE])
            entries.append((letters, score))
            pos += HISCORE_SIZE
        tables.append(entries)
    return tables


def command_hiscores(path):
    tables = parse_hiscores(read_file(path))
    basename = os.path.basename(path).upper()
    names = TABLE_NAMES.get(basename,
                            tuple(f'Table {i}' for i in range(1, 5)))
    print(f'{path}:')
    for table, entries in zip(names, tables):
        for rank, (letters, score) in enumerate(entries, 1):
            print(f"  {table}, {rank}: letters={letters!r}, score={score}")


def key_description(word):
    byte_index = word & 0xFF
    mask = word >> 8
    if not mask or mask & (mask - 1):
        raise ValueError(f'key mask {mask:02X} is not one bit')
    scancode = byte_index * 8 + (mask.bit_length() - 1)
    name = KEY_NAMES.get(scancode, 'unknown')
    return (f'byte {byte_index:02X}, mask {mask:02X}, '
            f'scancode {scancode:02X} ({name})')


def parse_options(data):
    if len(data) != 13:
        raise ValueError(f'expected 13 bytes, got {len(data)}')
    return {
        'opt_balls': data[0],
        'opt_music': data[1],
        'opt_palette': data[2],
        'opt_slope': data[3],
        'key_lflipper': struct.unpack_from('<H', data, 4)[0],
        'key_rflipper': struct.unpack_from('<H', data, 6)[0],
        'key_nudge': struct.unpack_from('<H', data, 8)[0],
        'key_plunger': struct.unpack_from('<H', data, 10)[0],
        'opt_screen': data[12],
    }


def command_options(path):
    options = parse_options(read_file(path))
    balls = 3 if options['opt_balls'] == 1 else 5
    music = 'game tunes and jingles' if options['opt_music'] == 1 else 'one tune'
    palette = 'grey' if options['opt_palette'] == 1 else 'colour'
    gravity = 9 + 2 * (options['opt_slope'] - 1)
    screen = {1: '320x200', 2: '320x350'}.get(options['opt_screen'], 'unknown')
    print(f'{path}:')
    print(f"  opt_balls: {options['opt_balls']} ({balls} balls)")
    print(f"  opt_music: {options['opt_music']} ({music})")
    print(f"  opt_palette: {options['opt_palette']} ({palette})")
    print(f"  opt_slope: {options['opt_slope']} (gravity {gravity})")
    for name in ('key_lflipper', 'key_rflipper', 'key_nudge', 'key_plunger'):
        print(f'  {name}: {key_description(options[name])}')
    print(f"  opt_screen: {options['opt_screen']} ({screen})")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest='command', required=True)
    map_ap = sub.add_parser('map', help='read a TBLDET collision map')
    map_ap.add_argument('file')
    map_ap.add_argument('--png', metavar='OUT', help='write a false-colour PNG')
    hs_ap = sub.add_parser('hiscores', help='read HISCORES.PD1/PD2')
    hs_ap.add_argument('file')
    opt_ap = sub.add_parser('options', help='read DDPCOPTN.BIN')
    opt_ap.add_argument('file')
    args = ap.parse_args()
    try:
        if args.command == 'map':
            command_map(args.file, args.png)
        elif args.command == 'hiscores':
            command_hiscores(args.file)
        else:
            command_options(args.file)
    except (OSError, ValueError) as e:
        raise SystemExit(f'{args.file}: {e}') from e


if __name__ == '__main__':
    main()
