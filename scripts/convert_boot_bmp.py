"""Convert boot.png (or .jpg/.bmp) to a size-capped JPEG C header for flash-only boot splash.

Keeps RAM cost low at runtime: JPEG lives in PROGMEM; decode uses TJpg MCU tiles
(no full-frame RGB565 array — that would be ~768 KB).
"""
import os
import io
from PIL import Image

DST_W, DST_H = 800, 480
# Flash budget for the embedded JPEG (keeps firmware lean).
MAX_JPEG_BYTES = 48 * 1024
QUALITY_START = 72
QUALITY_MIN = 35


def fit_on_black(img: Image.Image, dst_w: int, dst_h: int) -> Image.Image:
    """Letterbox/pillarbox onto a black 800x480 canvas (preserves aspect)."""
    src = img.convert("RGB")
    scale = min(dst_w / src.width, dst_h / src.height)
    new_w = max(1, int(round(src.width * scale)))
    new_h = max(1, int(round(src.height * scale)))
    resized = src.resize((new_w, new_h), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (dst_w, dst_h), (0, 0, 0))
    canvas.paste(resized, ((dst_w - new_w) // 2, (dst_h - new_h) // 2))
    return canvas


def encode_jpeg(img: Image.Image, max_bytes: int) -> tuple[bytes, int]:
    quality = QUALITY_START
    best = b""
    best_q = quality
    while quality >= QUALITY_MIN:
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=quality, optimize=True, progressive=False)
        data = buf.getvalue()
        best, best_q = data, quality
        print(f"  quality={quality}: {len(data)} bytes ({len(data) / 1024:.1f} KB)")
        if len(data) <= max_bytes:
            return data, quality
        quality -= 5
    print(f"WARNING: could not fit under {max_bytes} bytes; using quality={best_q} ({len(best)} bytes)")
    return best, best_q


def convert_image_to_jpg_header(input_path: str, output_header_path: str) -> None:
    print(f"Opening {input_path}...")
    img = Image.open(input_path)
    print(f"Original: {img.size} mode={img.mode}")

    fitted = fit_on_black(img, DST_W, DST_H)
    print(f"Canvas: {fitted.size} (letterboxed on black)")
    print(f"Encoding JPEG (max {MAX_JPEG_BYTES} bytes)...")
    jpg_bytes, quality = encode_jpeg(fitted, MAX_JPEG_BYTES)
    print(f"Selected quality={quality}, payload={len(jpg_bytes)} bytes ({len(jpg_bytes) / 1024:.1f} KB)")

    preview_path = os.path.join(os.path.dirname(input_path), "boot_preview.jpg")
    with open(preview_path, "wb") as preview:
        preview.write(jpg_bytes)
    print(f"Wrote preview: {preview_path}")

    with open(output_header_path, "w", newline="\n") as out:
        out.write("#pragma once\n")
        out.write("#include <stdint.h>\n")
        out.write("#include <pgmspace.h>\n\n")
        out.write(
            f"// Auto-generated {DST_W}x{DST_H} boot JPEG from {os.path.basename(input_path)} "
            f"(q={quality}, {len(jpg_bytes)} bytes). Flash only — do not expand to RGB565.\n"
        )
        out.write(f"static const uint8_t boot_logo_jpg[{len(jpg_bytes)}] PROGMEM = {{\n")
        for i in range(0, len(jpg_bytes), 16):
            chunk = jpg_bytes[i : i + 16]
            hex_str = ", ".join(f"0x{b:02X}" for b in chunk)
            out.write(f"    {hex_str},\n")
        out.write("};\n")

    print(f"Saved header: {output_header_path}")


if __name__ == "__main__":
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    header_path = os.path.join(root, "include", "boot_logo.h")
    candidates = [
        os.path.join(root, "boot.png"),
        os.path.join(root, "boot.jpg"),
        os.path.join(root, "boot.jpeg"),
        os.path.join(root, "boot.bmp"),
    ]
    input_path = next((p for p in candidates if os.path.isfile(p)), None)
    if not input_path:
        raise SystemExit("No boot.png / boot.jpg / boot.bmp found in project root")
    convert_image_to_jpg_header(input_path, header_path)
