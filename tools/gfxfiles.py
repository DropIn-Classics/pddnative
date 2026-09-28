#!/usr/bin/env python3
"""Inspect the pictures and sprites used by the Deluxe programs.

    gfxfiles.py FILE [--png OUT] [--table EXT]

The tool reads raw ``.VGA`` pictures, ``TABLE2M`` table pictures, and
the pointer-table ``.SPR`` format.  It checks that encoding the parsed
file recreates its bytes exactly.  A PNG uses the VGA DAC palette that
the program loads from DDPCINTR.EXE, DDPCMAIN.EXE, PD.EXE, or PD2.EXE;
no palette bytes are kept in this source tree.

``FLIPPERS.SPR`` is shared by four tables, whose palette colours differ.
Its PNG uses the first table by default; ``--table`` selects another
three-letter table extension.
"""
import argparse
from dataclasses import dataclass
from pathlib import Path
import struct
import zlib


PICTURE_WIDTH = 320
TABLE_HEIGHT = 512
PALETTE_BYTES = 0x300

# File name: (program, segment frame, palette offset, description).
VGA_PALETTES = {
    '21STLOGO.VGA': ('DDPCINTR.EXE', 0x0097, 0x0000,
                     'DDPCINTR PALSEG:0000 (griflogo)'),
    'SPIDER.VGA': ('DDPCINTR.EXE', 0x0097, 0x0300,
                   'DDPCINTR PALSEG:0300 (spiderlogo)'),
    'PRESENTS.VGA': ('DDPCINTR.EXE', 0x0097, 0x0600,
                     'DDPCINTR PALSEG:0600 (presents)'),
    'TITLE.VGA': ('DDPCINTR.EXE', 0x0097, 0x0900,
                  'DDPCINTR PALSEG:0900 (titl)'),
    'SELECT.VGA': ('DDPCMAIN.EXE', 0x0448, 0x02F9,
                   'DDPCMAIN DATA:02F9 (select_palette)'),
    'DDPCLANG.VGA': ('DDPCMAIN.EXE', 0x0448, 0x05F9,
                     'DDPCMAIN DATA:05F9 (language_palette)'),
    'DDPCBKGD.VGA': ('DDPCMAIN.EXE', 0x0B5E, 0x0301,
                     'DDPCMAIN OPTIONS:0301 (options_palette)'),
}

# Directory: (program, TDATA frame, table extension ->
#             (name, palette offset, light count)).  set_palette first
# uploads the 40h base colours and the initially-off lamp colours, fills
# the rest through colour 7Fh with black, then uploads two more 40h-colour
# ranges from the same source.
TABLE_PALETTES = {
    'DREAMS1': ('PD.EXE', 0x0570, {
        'IGN': ('Ignition', 0x1EA8, 0x3D),
        'STW': ('Steel Wheel', 0x20D6, 0x2E),
        'BBX': ('Beat Box', 0x22AA, 0x33),
        'NTM': ('Nightmare', 0x249C, 0x3E),
    }),
    'DREAMS2': ('PD2.EXE', 0x054B, {
        'UND': ('Neptune', 0x1EB8, 0x3D),
        'SFR': ('Safari', 0x2278, 0x2E),
        'MNG': ('Revenge of the Robot Warriors', 0x2638, 0x33),
        'STT': ('Stall Turn', 0x29F8, 0x3E),
    }),
}


@dataclass(frozen=True)
class Sprite:
    width: int
    height: int
    pixels: bytes
    padding: bytes


def read_file(path):
    with open(path, 'rb') as f:
        return f.read()


def parse_raw_picture(data, height=None):
    if len(data) % PICTURE_WIDTH:
        raise ValueError(f'{len(data)} bytes is not a whole number of '
                         f'{PICTURE_WIDTH}-pixel rows')
    actual_height = len(data) // PICTURE_WIDTH
    if height is not None and actual_height != height:
        raise ValueError(f'expected {PICTURE_WIDTH}x{height}, got '
                         f'{PICTURE_WIDTH}x{actual_height}')
    return PICTURE_WIDTH, actual_height, data


def encode_raw_picture(picture):
    width, height, pixels = picture
    if width != PICTURE_WIDTH or len(pixels) != width * height:
        raise ValueError('raw picture dimensions do not match its pixels')
    return pixels


def parse_sprites(data):
    if len(data) < 2:
        raise ValueError('truncated sprite pointer table')
    first = struct.unpack_from('<H', data)[0]
    if first == 0 or first % 2:
        raise ValueError(f'invalid first sprite offset {first:#x}')
    count = first // 2
    if first > len(data):
        raise ValueError('sprite pointer table extends past the file')
    offsets = struct.unpack_from(f'<{count}H', data)
    if offsets[0] != first or any(a >= b for a, b in zip(offsets, offsets[1:])):
        raise ValueError('sprite offsets are not a contiguous ordered table')

    sprites = []
    for index, start in enumerate(offsets):
        end = offsets[index + 1] if index + 1 < count else len(data)
        if start + 4 > end:
            raise ValueError(f'truncated sprite {index} header')
        width, height = struct.unpack_from('<HH', data, start)
        pixel_end = start + 4 + width * height
        if not width or not height or pixel_end > end:
            raise ValueError(f'invalid {width}x{height} sprite {index}')
        sprites.append(Sprite(width, height, data[start + 4:pixel_end],
                              data[pixel_end:end]))
    return tuple(sprites)


def encode_sprites(sprites):
    if not sprites:
        raise ValueError('expected at least one sprite')
    offset = len(sprites) * 2
    if offset > 0xFFFF:
        raise ValueError('sprite pointer table is too large')
    offsets = []
    records = []
    for sprite in sprites:
        if not sprite.width or not sprite.height:
            raise ValueError('sprite dimensions must be nonzero')
        if len(sprite.pixels) != sprite.width * sprite.height:
            raise ValueError('sprite dimensions do not match its pixels')
        offsets.append(offset)
        record = (struct.pack('<HH', sprite.width, sprite.height) +
                  sprite.pixels + sprite.padding)
        records.append(record)
        offset += len(record)
        if offset > 0x10000:
            raise ValueError('sprite data exceeds 16-bit offsets')
    return struct.pack(f'<{len(offsets)}H', *offsets) + b''.join(records)


def mz_bytes(path, frame, offset, count):
    data = read_file(path)
    if len(data) < 0x1C or data[:2] not in (b'MZ', b'ZM'):
        raise ValueError(f'{path} is not an MZ program')
    header_size = struct.unpack_from('<H', data, 8)[0] * 16
    start = header_size + frame * 16 + offset
    end = start + count
    if start < header_size or end > len(data):
        raise ValueError(f'{frame:04X}:{offset:04X} is outside {path}')
    return data[start:end]


def dac_colours(data):
    if not data or len(data) % 3:
        raise ValueError('VGA DAC colour data is not complete RGB triples')
    if data and max(data) > 0x3F:
        raise ValueError('VGA DAC palette component is above 3Fh')
    return tuple(tuple(data[i:i + 3]) for i in range(0, len(data), 3))


def display_colours(palette):
    return tuple(tuple((component * 255 + 31) // 63 for component in colour)
                 for colour in palette)


def companion(asset, program):
    path = asset.parent / program
    if not path.is_file():
        raise ValueError(f'cannot find companion program {path}')
    return path


def vga_palette(asset):
    try:
        program, frame, offset, source = VGA_PALETTES[asset.name.upper()]
    except KeyError as e:
        raise ValueError(f'no program palette known for {asset.name}') from e
    program_path = companion(asset, program)
    return dac_colours(mz_bytes(program_path, frame, offset, PALETTE_BYTES)), source


def table_info(asset):
    directory = asset.parent.name.upper()
    try:
        return TABLE_PALETTES[directory]
    except KeyError as e:
        raise ValueError('table asset must be in DREAMS1 or DREAMS2') from e


def table_palette(asset, extension):
    program, frame, tables = table_info(asset)
    extension = extension.upper()
    try:
        table_name, offset, light_count = tables[extension]
    except KeyError as e:
        choices = ', '.join(tables)
        raise ValueError(f'unknown table {extension}; expected {choices}') from e
    # set_palette consumes 80h consecutive source colours over its three
    # passes.  The first 40h are the base colours and the rest begin with
    # the initially-off lamp colours.
    source_data = mz_bytes(companion(asset, program), frame, offset, 0x180)
    source = dac_colours(source_data)
    palette = (source[:0x40] + source[0x40:0x40 + light_count] +
               ((0, 0, 0),) * (0x40 - light_count) +
               source[:0x40] + source[0x40:0x80])
    description = (f'{program} TDATA:{offset:04X} '
                   f'({table_name}, {light_count} lamp colours)')
    return palette, description


def png_chunk(kind, data):
    body = kind + data
    return (struct.pack('>I', len(data)) + body +
            struct.pack('>I', zlib.crc32(body)))


def write_indexed_png(path, width, height, pixels, palette,
                      transparent_zero=False):
    if len(pixels) != width * height:
        raise ValueError('pixel count does not match image dimensions')
    if len(palette) != 256:
        raise ValueError('expected a 256-colour palette')
    plte = bytes(component for colour in display_colours(palette)
                 for component in colour)
    raw = b''.join(b'\0' + pixels[y * width:(y + 1) * width]
                   for y in range(height))
    transparency = png_chunk(b'tRNS', b'\0') if transparent_zero else b''
    png = (b'\x89PNG\r\n\x1a\n' +
           png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height,
                                           8, 3, 0, 0, 0)) +
           png_chunk(b'PLTE', plte) + transparency +
           png_chunk(b'IDAT', zlib.compress(raw, 9)) +
           png_chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)


def sprite_sheet(sprites):
    columns = min(5, len(sprites))
    rows = (len(sprites) + columns - 1) // columns
    cell_width = max(sprite.width for sprite in sprites) + 4
    cell_height = max(sprite.height for sprite in sprites) + 4
    width = columns * cell_width
    height = rows * cell_height
    pixels = bytearray(width * height)
    for index, sprite in enumerate(sprites):
        cell_x = index % columns * cell_width
        cell_y = index // columns * cell_height
        x0 = cell_x + (cell_width - sprite.width) // 2
        y0 = cell_y + (cell_height - sprite.height) // 2
        for y in range(sprite.height):
            source = y * sprite.width
            dest = (y0 + y) * width + x0
            pixels[dest:dest + sprite.width] = \
                sprite.pixels[source:source + sprite.width]
    return width, height, bytes(pixels)


def command_vga(asset, data, png_path):
    picture = parse_raw_picture(data)
    if encode_raw_picture(picture) != data:
        raise ValueError('decoded picture did not encode to identical bytes')
    palette, source = vga_palette(asset)
    width, height, pixels = picture
    print(f'{asset}: raw {width}x{height}, 8-bit pixels, {len(data)} bytes')
    print(f'  palette: {source}')
    print('  round trip: identical')
    if png_path:
        write_indexed_png(png_path, width, height, pixels, palette)
        print(f'  png: {png_path}')


def command_table(asset, data, png_path):
    extension = asset.suffix[1:].upper()
    picture = parse_raw_picture(data, TABLE_HEIGHT)
    if encode_raw_picture(picture) != data:
        raise ValueError('decoded picture did not encode to identical bytes')
    palette, source = table_palette(asset, extension)
    width, height, pixels = picture
    print(f'{asset}: raw {width}x{height}, 8-bit pixels, {len(data)} bytes')
    print(f'  palette: {source}')
    print('  round trip: identical')
    if png_path:
        write_indexed_png(png_path, width, height, pixels, palette)
        print(f'  png: {png_path}')


def command_sprites(asset, data, png_path, selected_table):
    sprites = parse_sprites(data)
    if encode_sprites(sprites) != data:
        raise ValueError('decoded sprites did not encode to identical bytes')
    padding = sum(len(sprite.padding) for sprite in sprites)
    dimensions = {}
    for sprite in sprites:
        dimensions[(sprite.width, sprite.height)] = \
            dimensions.get((sprite.width, sprite.height), 0) + 1
    print(f'{asset}: {len(sprites)} sprites, {len(data)} bytes')
    for (width, height), count in dimensions.items():
        print(f'  {count} x {width}x{height}')
    print(f'  record padding: {padding} bytes')

    if asset.name.upper() == 'DDPCICON.SPR':
        print('  record 0 treats colour 00 as transparent; records 1-14 are opaque')
        main = companion(asset, 'DDPCMAIN.EXE')
        palette = dac_colours(mz_bytes(main, 0x0B5E, 0x0301,
                                       PALETTE_BYTES))
        source = 'DDPCMAIN OPTIONS:0301 (options_palette)'
    elif asset.name.upper() == 'FLIPPERS.SPR':
        print('  colour 00 is transparent')
        _program, _frame, tables = table_info(asset)
        extension = selected_table.upper() if selected_table else next(iter(tables))
        palette, source = table_palette(asset, extension)
    else:
        raise ValueError(f'no program palette known for {asset.name}')
    print(f'  palette: {source}')
    print('  round trip: identical')
    if png_path:
        width, height, pixels = sprite_sheet(sprites)
        write_indexed_png(png_path, width, height, pixels, palette,
                          transparent_zero=asset.name.upper() == 'FLIPPERS.SPR')
        print(f'  png: {png_path}')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('file')
    ap.add_argument('--png', metavar='OUT', help='write a PNG preview')
    ap.add_argument('--table', metavar='EXT',
                    help='palette for FLIPPERS.SPR (for example IGN or SFR)')
    args = ap.parse_args()
    asset = Path(args.file)
    try:
        data = read_file(asset)
        basename = asset.name.upper()
        if asset.suffix.upper() == '.VGA':
            if args.table:
                raise ValueError('--table applies only to FLIPPERS.SPR')
            command_vga(asset, data, args.png)
        elif basename.startswith('TABLE2M.'):
            if args.table:
                raise ValueError('TABLE2M selects its palette by extension')
            command_table(asset, data, args.png)
        elif asset.suffix.upper() == '.SPR':
            if args.table and basename != 'FLIPPERS.SPR':
                raise ValueError('--table applies only to FLIPPERS.SPR')
            command_sprites(asset, data, args.png, args.table)
        else:
            raise ValueError('expected a listed .VGA, .SPR, or TABLE2M file')
    except (OSError, ValueError, struct.error) as e:
        raise SystemExit(f'{args.file}: {e}') from e


if __name__ == '__main__':
    main()
