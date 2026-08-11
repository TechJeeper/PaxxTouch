import os
from PIL import Image
import io

def convert_bmp_to_jpg_header(input_path, output_header_path):
    print(f"Opening {input_path} with Pillow...")
    img = Image.open(input_path)
    print(f"Original image size: {img.size}, mode: {img.mode}")
    
    # Convert RGBA/1-bit to RGB
    if img.mode != 'RGB':
        img = img.convert('RGB')
        
    # Resize to native panel resolution 800x480
    dst_w, dst_h = 800, 480
    print(f"Resampling to {dst_w}x{dst_h}...")
    img_resized = img.resize((dst_w, dst_h), Image.Resampling.LANCZOS)
    
    # Save as high-quality JPEG in memory buffer
    buf = io.BytesIO()
    img_resized.save(buf, format='JPEG', quality=92)
    jpg_bytes = buf.getvalue()
    
    print(f"Generated JPEG payload size: {len(jpg_bytes)} bytes ({len(jpg_bytes)/1024:.1f} KB)")
    
    # Write C header
    with open(output_header_path, 'w') as out:
        out.write("#pragma once\n#include <stdint.h>\n#include <pgmspace.h>\n\n")
        out.write(f"// Auto-generated 800x480 boot logo JPEG ({len(jpg_bytes)} bytes)\n")
        out.write(f"static const uint8_t boot_logo_jpg[{len(jpg_bytes)}] PROGMEM = {{\n")
        for i in range(0, len(jpg_bytes), 16):
            chunk = jpg_bytes[i:i+16]
            hex_str = ", ".join(f"0x{b:02X}" for b in chunk)
            out.write(f"    {hex_str},\n")
        out.write("};\n")
        
    print(f"Saved header to {output_header_path} successfully!")

if __name__ == '__main__':
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    bmp_path = os.path.join(root, 'boot.bmp')
    header_path = os.path.join(root, 'include', 'boot_logo.h')
    convert_bmp_to_jpg_header(bmp_path, header_path)
