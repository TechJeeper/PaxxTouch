import struct
import os

def convert_bmp(input_path, output_header_path):
    print(f"Opening {input_path}...")
    with open(input_path, 'rb') as f:
        f.seek(10)
        offset = struct.unpack('<I', f.read(4))[0]
        f.seek(18)
        src_w, src_h = struct.unpack('<ii', f.read(8))
        f.seek(offset)
        raw = f.read()

    dst_w, dst_h = 800, 480
    print(f"Resampling {src_w}x{src_h} down to {dst_w}x{dst_h} RGB565...")

    pixels = []
    is_bottom_up = src_h > 0
    abs_src_h = abs(src_h)

    for dst_y in range(dst_h):
        src_y = int(dst_y * abs_src_h / dst_h)
        if is_bottom_up:
            bmp_row = (abs_src_h - 1 - src_y)
        else:
            bmp_row = src_y
        row_offset = bmp_row * src_w * 4

        for dst_x in range(dst_w):
            src_x = int(dst_x * src_w / dst_w)
            px_idx = row_offset + src_x * 4
            b = raw[px_idx]
            g = raw[px_idx + 1]
            r = raw[px_idx + 2]

            r5 = (r >> 3) & 0x1F
            g6 = (g >> 2) & 0x3F
            b5 = (b >> 3) & 0x1F

            px565 = (b5 << 11) | (g6 << 5) | r5
            pixels.append(px565)

    print(f"Writing {output_header_path} ({len(pixels)} pixels)...")
    with open(output_header_path, 'w') as out:
        out.write("#pragma once\n#include <stdint.h>\n#include <pgmspace.h>\n\n")
        out.write(f"// Auto-generated boot screen ({dst_w}x{dst_h} BGR565)\n")
        out.write(f"static const uint16_t boot_logo_rgb565[{dst_w * dst_h}] PROGMEM = {{\n")
        for i in range(0, len(pixels), 16):
            chunk = pixels[i:i+16]
            hex_str = ", ".join(f"0x{px:04X}" for px in chunk)
            out.write(f"    {hex_str},\n")
        out.write("};\n")

    print("Done!")

if __name__ == '__main__':
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    bmp_path = os.path.join(root, 'boot.bmp')
    header_path = os.path.join(root, 'include', 'boot_logo.h')
    convert_bmp(bmp_path, header_path)
