#!/usr/bin/env python3
"""Inspect files used by DDPCMAIN.EXE's History of Pinball viewer.

    ddfiles.py idx FILE
    ddfiles.py hop FILE
    ddfiles.py font FILE [--png OUT]
    ddfiles.py picture FILE [--png OUT]

``idx`` and ``hop`` decode the five language-specific index/text pairs.
The companion file with the same stem must be beside a HOP file.  Text
bytes are printed as code page 437, the closest terminal representation
of the glyph indices used by HISTORY.FNT.  ``font`` renders that 8x8
font, while ``picture`` understands the viewer's planar .016 images,
paletted .256 images, and its raw 320x200 .VGA background.  Every command
encodes its parsed representation again and requires identical bytes.
PNG output uses only the Python standard library.
"""
import argparse
from dataclasses import dataclass
from pathlib import Path
import struct
import sys
import zlib


INDEX_RECORDS = 53
INDEX_RECORD_SIZE = 5
FONT_GLYPHS = 256
GLYPH_SIZE = 8
HISTORY_WIDTH = 640
HISTORY_HEIGHT = 480
PLANE_SIZE = HISTORY_WIDTH // 8 * HISTORY_HEIGHT
PALETTE_SIZE = 256 * 3

EGA_PALETTE = (
    (0x00, 0x00, 0x00), (0x00, 0x00, 0xAA),
    (0x00, 0xAA, 0x00), (0x00, 0xAA, 0xAA),
    (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA),
    (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA),
    (0x55, 0x55, 0x55), (0x55, 0x55, 0xFF),
    (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF),
    (0xFF, 0xFF, 0x55), (0xFF, 0xFF, 0xFF),
)


@dataclass(frozen=True)
class IndexEntry:
    start: int
    end: int
    line_count_minus_one: int


@dataclass(frozen=True)
class HopLine:
    text: bytes
    blank_lines_after: int


@dataclass(frozen=True)
class HopRecord:
    record_id: int
    entry: IndexEntry
    lines: tuple


def read_file(path):
    with open(path, 'rb') as f:
        return f.read()


def parse_idx(data):
    expected = INDEX_RECORDS * INDEX_RECORD_SIZE
    if len(data) != expected:
        raise ValueError(f'expected {expected} bytes, got {len(data)}')
    return [IndexEntry(*values)
            for values in struct.iter_unpack('<HHB', data)]


def encode_idx(entries):
    if len(entries) != INDEX_RECORDS:
        raise ValueError(f'expected {INDEX_RECORDS} entries, got {len(entries)}')
    return b''.join(struct.pack('<HHB', entry.start, entry.end,
                                entry.line_count_minus_one)
                    for entry in entries)


def parse_hop_lines(data, start, end):
    lines = []
    pos = start
    while pos < end:
        zero = data.find(b'\0', pos, end)
        if zero < 0:
            raise ValueError(f'unterminated text at offset {pos:#x}')
        if zero + 1 >= end or data[zero + 1] != 0:
            raise ValueError(f'text at offset {pos:#x} lacks its second NUL')
        text = data[pos:zero]
        if not text:
            raise ValueError(f'empty text at offset {pos:#x}')
        pos = zero + 2
        blanks = 0
        while pos + 2 <= end and data[pos:pos + 2] == b'\r\n':
            blanks += 1
            pos += 2
        lines.append(HopLine(text, blanks))
    if pos != end:
        raise ValueError(f'text record overruns end offset {end:#x}')
    return tuple(lines)


def parse_hop(data, entries):
    if len(entries) != INDEX_RECORDS:
        raise ValueError(f'expected {INDEX_RECORDS} index entries')
    records = []
    for record_id, entry in enumerate(entries):
        if not 0 <= entry.start <= entry.end < len(data):
            raise ValueError(f'record {record_id} has invalid offsets')
        lines = parse_hop_lines(data, entry.start, entry.end)
        expected = entry.line_count_minus_one + 1
        if len(lines) < expected:
            raise ValueError(f'record {record_id} has only {len(lines)} lines, '
                             f'index displays {expected}')
        records.append(HopRecord(record_id, entry, lines))

    physical = sorted(records, key=lambda record: record.entry.start)
    if physical[0].entry.start != 0:
        raise ValueError('first physical text record does not start at zero')
    for i, record in enumerate(physical):
        marker = b'@\0\0@' if i == len(physical) - 1 else b'@\0\0\r\n'
        marker_start = record.entry.end
        if data[marker_start:marker_start + len(marker)] != marker:
            raise ValueError(f'bad separator after record {record.record_id}')
        next_start = marker_start + len(marker)
        if i + 1 < len(physical):
            if physical[i + 1].entry.start != next_start:
                raise ValueError('gap between physical text records')
        elif next_start != len(data):
            raise ValueError('trailing bytes after last text record')
    return records


def encode_hop_line(line):
    if b'\0' in line.text:
        raise ValueError('NUL in HOP text')
    return line.text + b'\0\0' + b'\r\n' * line.blank_lines_after


def encode_hop(records):
    if len(records) != INDEX_RECORDS:
        raise ValueError(f'expected {INDEX_RECORDS} records')
    physical = sorted(records, key=lambda record: record.entry.start)
    out = bytearray()
    for i, record in enumerate(physical):
        if len(out) != record.entry.start:
            raise ValueError(f'record {record.record_id} start changed')
        for line in record.lines:
            out.extend(encode_hop_line(line))
        if len(out) != record.entry.end:
            raise ValueError(f'record {record.record_id} end changed')
        out.extend(b'@\0\0@' if i == len(physical) - 1 else b'@\0\0\r\n')
    return bytes(out)


def companion(path, suffix):
    return Path(path).with_suffix(suffix)


def decode_text(data):
    return data.decode('cp437')


def record_title(record):
    return decode_text(record.lines[0].text)


def command_idx(path):
    data = read_file(path)
    entries = parse_idx(data)
    if encode_idx(entries) != data:
        raise ValueError('decoded index did not encode to identical bytes')
    hop_path = companion(path, '.HOP')
    records = None
    if hop_path.exists():
        records = parse_hop(read_file(hop_path), entries)
    print(f'{path}: {len(entries)} entries, {len(data)} bytes')
    for i, entry in enumerate(entries):
        title = f', {record_title(records[i])}' if records else ''
        stored = f', stored={len(records[i].lines)}' if records else ''
        print(f'  record {i:02d}: start={entry.start:04X}, '
              f'end={entry.end:04X}, displayed={entry.line_count_minus_one + 1}'
              f'{stored}{title}')
    print('  round trip: identical')


def command_hop(path):
    data = read_file(path)
    idx_path = companion(path, '.IDX')
    entries = parse_idx(read_file(idx_path))
    records = parse_hop(data, entries)
    if encode_hop(records) != data:
        raise ValueError('decoded text did not encode to identical bytes')
    print(f'{path}: {len(records)} records, {len(data)} bytes')
    for record in records:
        displayed = record.entry.line_count_minus_one + 1
        print(f'  record {record.record_id:02d}: {record_title(record)} '
              f'({displayed} displayed, {len(record.lines)} stored)')
        for i, line in enumerate(record.lines):
            prefix = '    ' if i < displayed else '    [not displayed] '
            print(f'{prefix}{decode_text(line.text)}')
            for _ in range(line.blank_lines_after):
                print()
    print('  round trip: identical')


def parse_font(data):
    expected = FONT_GLYPHS * GLYPH_SIZE
    if len(data) != expected:
        raise ValueError(f'expected {expected} bytes, got {len(data)}')
    return tuple(data[i:i + GLYPH_SIZE]
                 for i in range(0, len(data), GLYPH_SIZE))


def encode_font(glyphs):
    if len(glyphs) != FONT_GLYPHS:
        raise ValueError(f'expected {FONT_GLYPHS} glyphs')
    if any(len(glyph) != GLYPH_SIZE for glyph in glyphs):
        raise ValueError('each glyph must have eight rows')
    return b''.join(glyphs)


def font_pixels(glyphs):
    width = 16 * GLYPH_SIZE
    pixels = bytearray(width * width)
    for code, glyph in enumerate(glyphs):
        gx = code % 16 * GLYPH_SIZE
        gy = code // 16 * GLYPH_SIZE
        for y, bits in enumerate(glyph):
            off = (gy + y) * width + gx
            for x in range(GLYPH_SIZE):
                pixels[off + x] = 1 if bits & (0x80 >> x) else 0
    return width, width, bytes(pixels)


def png_chunk(kind, data):
    body = kind + data
    return (struct.pack('>I', len(data)) + body +
            struct.pack('>I', zlib.crc32(body)))


def write_indexed_png(path, width, height, pixels, palette):
    if len(pixels) != width * height:
        raise ValueError('pixel count does not match image dimensions')
    if not 1 <= len(palette) <= 256:
        raise ValueError('PNG palette must have 1..256 colours')
    if pixels and max(pixels) >= len(palette):
        raise ValueError('pixel index is outside PNG palette')
    plte = bytes(component for colour in palette for component in colour)
    raw = b''.join(b'\0' + pixels[y * width:(y + 1) * width]
                   for y in range(height))
    png = (b'\x89PNG\r\n\x1a\n' +
           png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height,
                                           8, 3, 0, 0, 0)) +
           png_chunk(b'PLTE', plte) +
           png_chunk(b'IDAT', zlib.compress(raw, 9)) +
           png_chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)


def command_font(path, png_path):
    data = read_file(path)
    glyphs = parse_font(data)
    if encode_font(glyphs) != data:
        raise ValueError('decoded font did not encode to identical bytes')
    print(f'{path}: {len(glyphs)} glyphs, 8x8 pixels, {len(data)} bytes')
    print('  round trip: identical')
    if png_path:
        width, height, pixels = font_pixels(glyphs)
        write_indexed_png(png_path, width, height, pixels,
                          ((0, 0, 0), (255, 255, 255)))
        print(f'  png: {png_path}')


def parse_picture_016(data):
    expected = 4 * PLANE_SIZE
    if len(data) != expected:
        raise ValueError(f'expected {expected} bytes, got {len(data)}')
    return tuple(data[i * PLANE_SIZE:(i + 1) * PLANE_SIZE]
                 for i in range(4))


def encode_picture_016(planes):
    if len(planes) != 4 or any(len(plane) != PLANE_SIZE for plane in planes):
        raise ValueError('expected four 80x480 VGA bit planes')
    return b''.join(planes)


def pixels_016(planes):
    pixels = bytearray(HISTORY_WIDTH * HISTORY_HEIGHT)
    for y in range(HISTORY_HEIGHT):
        row = y * (HISTORY_WIDTH // 8)
        out = y * HISTORY_WIDTH
        for xb in range(HISTORY_WIDTH // 8):
            values = [plane[row + xb] for plane in planes]
            for bit in range(8):
                mask = 0x80 >> bit
                colour = sum((1 << p) for p, value in enumerate(values)
                             if value & mask)
                pixels[out + xb * 8 + bit] = colour
    return bytes(pixels)


def parse_picture_256(data):
    expected = PALETTE_SIZE + HISTORY_WIDTH * HISTORY_HEIGHT
    if len(data) != expected:
        raise ValueError(f'expected {expected} bytes, got {len(data)}')
    palette_data = data[:PALETTE_SIZE]
    if max(palette_data) > 63:
        raise ValueError('VGA DAC palette component is above 63')
    palette = tuple(tuple(palette_data[i:i + 3])
                    for i in range(0, PALETTE_SIZE, 3))
    return palette, data[PALETTE_SIZE:]


def encode_picture_256(picture):
    palette, pixels = picture
    if len(palette) != 256 or any(len(colour) != 3 for colour in palette):
        raise ValueError('expected 256 three-component palette entries')
    if len(pixels) != HISTORY_WIDTH * HISTORY_HEIGHT:
        raise ValueError('expected 640x480 pixels')
    return bytes(component for colour in palette for component in colour) + pixels


def dac_palette(palette):
    return tuple(tuple((component * 255 + 31) // 63 for component in colour)
                 for colour in palette)


def parse_picture_vga(data):
    expected = 320 * 200
    if len(data) != expected:
        raise ValueError(f'expected {expected} bytes, got {len(data)}')
    return data


def command_picture(path, png_path):
    data = read_file(path)
    suffix = Path(path).suffix.upper()
    if suffix == '.016':
        picture = parse_picture_016(data)
        encoded = encode_picture_016(picture)
        description = '640x480, 16 colours, four VGA bit planes'
        pixels = pixels_016(picture) if png_path else None
        palette = EGA_PALETTE
        width, height = HISTORY_WIDTH, HISTORY_HEIGHT
    elif suffix == '.256':
        picture = parse_picture_256(data)
        encoded = encode_picture_256(picture)
        description = '640x480, 256 colours, 768-byte VGA DAC palette'
        palette = dac_palette(picture[0])
        pixels = picture[1]
        width, height = HISTORY_WIDTH, HISTORY_HEIGHT
    elif suffix == '.VGA':
        pixels = parse_picture_vga(data)
        encoded = pixels
        description = '320x200 raw pixel indices (palette stored in DDPCMAIN)'
        palette = tuple((i, i, i) for i in range(256))
        width, height = 320, 200
    else:
        raise ValueError('picture extension must be .016, .256, or .VGA')
    if encoded != data:
        raise ValueError('decoded picture did not encode to identical bytes')
    print(f'{path}: {description}, {len(data)} bytes')
    print('  round trip: identical')
    if png_path:
        write_indexed_png(png_path, width, height, pixels, palette)
        print(f'  png: {png_path}')


def main():
    # Some Windows consoles cannot encode every CP437 glyph (notably theta).
    # Keep a damaged source character from aborting an otherwise valid dump.
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(errors='replace')
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest='command', required=True)
    idx_ap = sub.add_parser('idx', help='read a language .IDX file')
    idx_ap.add_argument('file')
    hop_ap = sub.add_parser('hop', help='read a language .HOP file')
    hop_ap.add_argument('file')
    font_ap = sub.add_parser('font', help='read HISTORY.FNT')
    font_ap.add_argument('file')
    font_ap.add_argument('--png', metavar='OUT', help='write a font-sheet PNG')
    picture_ap = sub.add_parser('picture', help='read a history picture')
    picture_ap.add_argument('file')
    picture_ap.add_argument('--png', metavar='OUT', help='write a picture PNG')
    args = ap.parse_args()
    try:
        if args.command == 'idx':
            command_idx(args.file)
        elif args.command == 'hop':
            command_hop(args.file)
        elif args.command == 'font':
            command_font(args.file, args.png)
        else:
            command_picture(args.file, args.png)
    except (OSError, ValueError) as e:
        raise SystemExit(f'{args.file}: {e}') from e


if __name__ == '__main__':
    main()
