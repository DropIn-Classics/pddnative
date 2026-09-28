#!/usr/bin/env python3
"""Inspect Autodesk Animator FLI animations used by DDFLIPLY.EXE.

    flifiles.py FILE [--png N OUT] [--all DIR]

The tool parses the 128-byte FLI header, every frame and every chunk,
decodes the animation, and checks that encoding the parsed structure gives
the input bytes exactly.  ``--png`` writes zero-based displayed frame N as
an indexed PNG using that frame's six-bit VGA palette; ``--all`` writes every
displayed frame into DIR as NAME_0000.png, NAME_0001.png ... (NAME the
file's name without extension).  PNG output uses only the Python standard
library.
"""
import argparse
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import struct
import zlib


FLI_MAGIC = 0xAF11
FRAME_MAGIC = 0xF1FA
HEADER_SIZE = 128
FRAME_HEADER_SIZE = 16
CHUNK_HEADER_SIZE = 6

CHUNK_NAMES = {
    11: 'COLOR_64',
    12: 'LC',
    13: 'BLACK',
    15: 'BRUN',
    16: 'COPY',
}


@dataclass(frozen=True)
class FliHeader:
    frames: int
    width: int
    height: int
    depth: int
    flags: int
    speed: int
    reserved: bytes


@dataclass(frozen=True)
class Chunk:
    declared_size: int
    kind: int
    data: bytes


@dataclass(frozen=True)
class Frame:
    magic: int
    reserved: bytes
    chunks: tuple


@dataclass(frozen=True)
class Fli:
    header: FliHeader
    frames: tuple


@dataclass(frozen=True)
class Image:
    pixels: bytes
    palette: tuple


def read_file(path):
    with open(path, 'rb') as f:
        return f.read()


def parse_fli(data):
    if len(data) < HEADER_SIZE:
        raise ValueError('truncated FLI header')
    size, magic, frames, width, height, depth, flags, speed = \
        struct.unpack_from('<I7H', data)
    if size != len(data):
        raise ValueError(f'header size {size} does not match {len(data)} bytes')
    if magic != FLI_MAGIC:
        raise ValueError(f'expected FLI magic AF11, got {magic:04X}')
    if not frames or not width or not height:
        raise ValueError('frame count and dimensions must be nonzero')
    if depth != 8:
        raise ValueError(f'expected 8-bit pixels, got depth {depth}')
    header = FliHeader(frames, width, height, depth, flags, speed,
                       data[18:HEADER_SIZE])

    parsed_frames = []
    pos = HEADER_SIZE
    while pos < len(data):
        frame_number = len(parsed_frames)
        if pos + FRAME_HEADER_SIZE > len(data):
            raise ValueError(f'truncated frame {frame_number} header')
        frame_size, frame_magic, chunk_count = struct.unpack_from('<IHH', data, pos)
        if frame_size < FRAME_HEADER_SIZE:
            raise ValueError(f'frame {frame_number} has size {frame_size}')
        frame_end = pos + frame_size
        if frame_end > len(data):
            raise ValueError(f'frame {frame_number} extends past the file')
        if frame_magic != FRAME_MAGIC:
            raise ValueError(f'frame {frame_number} has magic {frame_magic:04X}')
        reserved = data[pos + 8:pos + FRAME_HEADER_SIZE]
        chunk_pos = pos + FRAME_HEADER_SIZE
        chunks = []
        for chunk_number in range(chunk_count):
            if chunk_pos + CHUNK_HEADER_SIZE > frame_end:
                raise ValueError(f'truncated frame {frame_number} chunk '
                                 f'{chunk_number} header')
            chunk_size, kind = struct.unpack_from('<IH', data, chunk_pos)
            if chunk_size < CHUNK_HEADER_SIZE:
                raise ValueError(f'frame {frame_number} chunk {chunk_number} '
                                 f'has size {chunk_size}')
            declared_end = chunk_pos + chunk_size
            # The shipped Animator files sometimes include an omitted pad
            # byte in the final chunk's size.  The enclosing frame size is
            # authoritative: DDFLIPLY also advances by frames, while its
            # known-chunk decoders consume the packet data itself.
            chunk_end = frame_end if chunk_number == chunk_count - 1 \
                else declared_end
            if chunk_number == chunk_count - 1 and declared_end not in \
                    (frame_end, frame_end + 1):
                raise ValueError(f'frame {frame_number} final chunk size '
                                 'does not match its frame')
            if chunk_end > frame_end:
                raise ValueError(f'frame {frame_number} chunk {chunk_number} '
                                 'extends past its frame')
            chunks.append(Chunk(chunk_size, kind,
                                data[chunk_pos + CHUNK_HEADER_SIZE:chunk_end]))
            chunk_pos = chunk_end
        if chunk_pos != frame_end:
            raise ValueError(f'frame {frame_number} has '
                             f'{frame_end - chunk_pos} bytes after its chunks')
        parsed_frames.append(Frame(frame_magic, reserved, tuple(chunks)))
        pos = frame_end

    if len(parsed_frames) not in (frames, frames + 1):
        raise ValueError(f'header names {frames} frames, file has '
                         f'{len(parsed_frames)} frame chunks')
    return Fli(header, tuple(parsed_frames))


def encode_chunk(chunk):
    if chunk.declared_size < CHUNK_HEADER_SIZE:
        raise ValueError('chunk declared size is smaller than its header')
    return struct.pack('<IH', chunk.declared_size, chunk.kind) + chunk.data


def encode_frame(frame):
    body = b''.join(encode_chunk(chunk) for chunk in frame.chunks)
    size = FRAME_HEADER_SIZE + len(body)
    if len(frame.reserved) != 8:
        raise ValueError('frame reserved field must be eight bytes')
    return (struct.pack('<IHH', size, frame.magic, len(frame.chunks)) +
            frame.reserved + body)


def encode_fli(fli):
    header = fli.header
    if len(header.reserved) != HEADER_SIZE - 18:
        raise ValueError('FLI reserved field has the wrong size')
    body = b''.join(encode_frame(frame) for frame in fli.frames)
    size = HEADER_SIZE + len(body)
    return (struct.pack('<I7H', size, FLI_MAGIC, header.frames,
                        header.width, header.height, header.depth,
                        header.flags, header.speed) + header.reserved + body)


def byte(data, pos, context):
    if pos >= len(data):
        raise ValueError(f'truncated {context}')
    return data[pos], pos + 1


def signed_byte(data, pos, context):
    value, pos = byte(data, pos, context)
    return value - 256 if value >= 128 else value, pos


def check_padding(data, pos, context):
    padding = data[pos:]
    if padding not in (b'', b'\0'):
        raise ValueError(f'{context} has {len(padding)} trailing bytes')


def decode_color(data, palette):
    if len(data) < 2:
        raise ValueError('truncated COLOR_64 packet count')
    packet_count = struct.unpack_from('<H', data)[0]
    pos = 2
    index = 0
    for packet in range(packet_count):
        skip, pos = byte(data, pos, f'COLOR_64 packet {packet} skip')
        count, pos = byte(data, pos, f'COLOR_64 packet {packet} count')
        count = count or 256
        index += skip
        end = pos + count * 3
        if index + count > 256 or end > len(data):
            raise ValueError(f'invalid COLOR_64 packet {packet}')
        for colour in range(count):
            rgb = tuple(data[pos + colour * 3:pos + colour * 3 + 3])
            if max(rgb) > 63:
                raise ValueError('COLOR_64 component is above 63')
            palette[index + colour] = rgb
        index += count
        pos = end
    check_padding(data, pos, 'COLOR_64')


def decode_lc(data, pixels, width, height):
    if len(data) < 4:
        raise ValueError('truncated LC header')
    skip_lines, line_count = struct.unpack_from('<HH', data)
    if skip_lines + line_count > height:
        raise ValueError('LC lines extend below the picture')
    pos = 4
    for line in range(skip_lines, skip_lines + line_count):
        packet_count, pos = byte(data, pos, f'LC line {line} packet count')
        x = 0
        for packet in range(packet_count):
            skip, pos = byte(data, pos, f'LC line {line} packet {packet} skip')
            count, pos = signed_byte(data, pos,
                                     f'LC line {line} packet {packet} count')
            x += skip
            if count >= 0:
                end = pos + count
                if end > len(data) or x + count > width:
                    raise ValueError(f'invalid LC literal on line {line}')
                start = line * width + x
                pixels[start:start + count] = data[pos:end]
                pos = end
                x += count
            else:
                value, pos = byte(data, pos, f'LC line {line} repeat value')
                count = -count
                if x + count > width:
                    raise ValueError(f'invalid LC repeat on line {line}')
                start = line * width + x
                pixels[start:start + count] = bytes((value,)) * count
                x += count
    check_padding(data, pos, 'LC')


def decode_brun(data, pixels, width, height):
    pos = 0
    for line in range(height):
        packet_count, pos = byte(data, pos, f'BRUN line {line} packet count')
        x = 0
        for packet in range(packet_count):
            count, pos = signed_byte(data, pos,
                                     f'BRUN line {line} packet {packet} count')
            if count >= 0:
                value, pos = byte(data, pos, f'BRUN line {line} repeat value')
                if x + count > width:
                    raise ValueError(f'invalid BRUN repeat on line {line}')
                start = line * width + x
                pixels[start:start + count] = bytes((value,)) * count
                x += count
            else:
                count = -count
                end = pos + count
                if end > len(data) or x + count > width:
                    raise ValueError(f'invalid BRUN literal on line {line}')
                start = line * width + x
                pixels[start:start + count] = data[pos:end]
                pos = end
                x += count
        if x != width:
            raise ValueError(f'BRUN line {line} decodes to {x} pixels')
    check_padding(data, pos, 'BRUN')


def decode_chunk(chunk, pixels, palette, width, height):
    if chunk.kind == 11:
        decode_color(chunk.data, palette)
    elif chunk.kind == 12:
        decode_lc(chunk.data, pixels, width, height)
    elif chunk.kind == 13:
        check_padding(chunk.data, 0, 'BLACK')
        pixels[:] = bytes(len(pixels))
    elif chunk.kind == 15:
        decode_brun(chunk.data, pixels, width, height)
    elif chunk.kind == 16:
        pixel_count = width * height
        if len(chunk.data) not in (pixel_count, pixel_count + 1):
            raise ValueError(f'COPY has {len(chunk.data)} pixels, expected '
                             f'{pixel_count}')
        check_padding(chunk.data, pixel_count, 'COPY')
        pixels[:] = chunk.data[:pixel_count]
    else:
        raise ValueError(f'unsupported chunk type {chunk.kind}')


def decode_fli(fli):
    width, height = fli.header.width, fli.header.height
    pixels = bytearray(width * height)
    palette = [(0, 0, 0)] * 256
    images = []
    for frame_number, frame in enumerate(fli.frames):
        for chunk_number, chunk in enumerate(frame.chunks):
            try:
                decode_chunk(chunk, pixels, palette, width, height)
            except ValueError as e:
                name = CHUNK_NAMES.get(chunk.kind, str(chunk.kind))
                raise ValueError(f'frame {frame_number} chunk {chunk_number} '
                                 f'({name}): {e}') from e
        images.append(Image(bytes(pixels), tuple(palette)))
    return tuple(images)


def png_chunk(kind, data):
    body = kind + data
    return (struct.pack('>I', len(data)) + body +
            struct.pack('>I', zlib.crc32(body)))


def write_png(path, width, height, image):
    if len(image.pixels) != width * height or len(image.palette) != 256:
        raise ValueError('decoded image has invalid dimensions or palette')
    palette = bytes((component * 255 + 31) // 63
                    for colour in image.palette for component in colour)
    raw = b''.join(b'\0' + image.pixels[y * width:(y + 1) * width]
                   for y in range(height))
    png = (b'\x89PNG\r\n\x1a\n' +
           png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height,
                                           8, 3, 0, 0, 0)) +
           png_chunk(b'PLTE', palette) +
           png_chunk(b'IDAT', zlib.compress(raw, 9)) +
           png_chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)


def command_fli(path, png_args, all_dir=None):
    data = read_file(path)
    fli = parse_fli(data)
    if encode_fli(fli) != data:
        raise ValueError('parsed FLI did not encode to identical bytes')
    images = decode_fli(fli)
    header = fli.header
    counts = Counter(chunk.kind for frame in fli.frames for chunk in frame.chunks)
    physical = len(fli.frames)
    print(f'{path}: {header.width}x{header.height}, {header.depth}-bit, '
          f'{header.frames} displayed frames, {physical} frame chunks, '
          f'{len(data)} bytes')
    milliseconds = header.speed * 1000 / 70
    print(f'  speed: {header.speed} jiffies ({milliseconds:.3f} ms at 70 Hz)')
    print('  chunks:')
    for kind, count in sorted(counts.items()):
        name = CHUNK_NAMES.get(kind, 'unknown')
        print(f'    {kind:2d} {name}: {count}')
    if physical == header.frames + 1:
        ring_matches = images[-1] == images[0]
        result = 'yes' if ring_matches else 'no'
        print(f'  final frame chunk is a ring frame leading back to frame 0: '
              f'{result}')
    else:
        print('  final frame chunk is a ring frame: no extra frame chunk present')
    print('  round trip: identical')

    if png_args:
        number_text, out = png_args
        try:
            number = int(number_text, 0)
        except ValueError as e:
            raise ValueError(f'invalid frame number {number_text!r}') from e
        if not 0 <= number < header.frames:
            raise ValueError(f'frame {number} is outside 0..{header.frames - 1}')
        write_png(out, header.width, header.height, images[number])
        print(f'  png frame {number}: {out}')

    if all_dir:
        out_dir = Path(all_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        for number in range(header.frames):
            write_png(out_dir / f'{path.stem}_{number:04d}.png',
                      header.width, header.height, images[number])
        print(f'  png frames 0..{header.frames - 1}: {out_dir}')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('file')
    ap.add_argument('--png', nargs=2, metavar=('N', 'OUT'),
                    help='write zero-based displayed frame N as a PNG')
    ap.add_argument('--all', metavar='DIR',
                    help='write every displayed frame into DIR as PNGs')
    args = ap.parse_args()
    try:
        command_fli(Path(args.file), args.png, args.all)
    except (OSError, ValueError, struct.error) as e:
        raise SystemExit(f'{args.file}: {e}') from e


if __name__ == '__main__':
    main()
