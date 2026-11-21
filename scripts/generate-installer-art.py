import os
from PIL import Image, ImageDraw, ImageFont

def generate_artwork():
    os.makedirs("installer", exist_ok=True)
    icon = Image.open("assets/icons/vynx-arc.png").convert("RGBA")

    # 1. Wizard Large Image (328 x 628) for Welcome / Finish page sidebar
    w_large, h_large = 328, 628
    large_img = Image.new("RGB", (w_large, h_large), color=(18, 22, 29))
    draw_l = ImageDraw.Draw(large_img)

    # Vertical tech gradient
    for y in range(h_large):
        ratio = y / h_large
        r = int(14 + (24 - 14) * ratio * 0.8)
        g = int(18 + (34 - 18) * ratio * 0.8)
        b = int(26 + (50 - 26) * ratio * 0.8)
        draw_l.line([(0, y), (w_large, y)], fill=(r, g, b))

    # Subtle modern geometric background lines / accent
    accent_color = (37, 99, 235, 45) # #2563eb with alpha
    overlay = Image.new("RGBA", (w_large, h_large), (0, 0, 0, 0))
    draw_ov = ImageDraw.Draw(overlay)
    
    # Draw subtle grid / diagonals
    for x in range(-200, w_large + 200, 48):
        draw_ov.line([(x, 0), (x + 250, h_large)], fill=(37, 99, 235, 18), width=1)
    
    # Subtle blue glow circle behind logo
    glow_cx, glow_cy = w_large // 2, 180
    for radius in range(120, 20, -10):
        alpha = int(18 * (1 - (radius / 120)))
        draw_ov.ellipse([glow_cx - radius, glow_cy - radius, glow_cx + radius, glow_cy + radius], 
                        fill=(37, 99, 235, alpha))

    # Composite overlay
    large_img = Image.alpha_composite(large_img.convert("RGBA"), overlay).convert("RGB")

    # Paste emblem
    logo_size = 140
    resized_icon = icon.resize((logo_size, logo_size), Image.Resampling.LANCZOS)
    large_img.paste(resized_icon, ((w_large - logo_size) // 2, 110), resized_icon)

    draw_l = ImageDraw.Draw(large_img)
    
    # Add typography
    try:
        font_brand = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 26)
        font_ver = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 15)
        font_tag = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 12)
    except Exception:
        font_brand = font_ver = font_tag = ImageFont.load_default()

    draw_l.text((w_large // 2, 275), "VYNX ARC", fill=(240, 243, 248), font=font_brand, anchor="mm")
    draw_l.text((w_large // 2, 305), "Version 1.0.0", fill=(165, 173, 187), font=font_ver, anchor="mm")
    
    # Bottom decorative accent badge
    draw_l.line([(w_large // 2 - 40, 335), (w_large // 2 + 40, 335)], fill=(37, 99, 235), width=2)
    draw_l.text((w_large // 2, 570), "HIGH PERFORMANCE ARCHIVER", fill=(120, 130, 145), font=font_tag, anchor="mm")
    draw_l.text((w_large // 2, 590), "OFFLINE · ISOLATED · SECURE", fill=(80, 95, 115), font=font_tag, anchor="mm")

    large_img.save("installer/wizard-large.bmp", "BMP")
    print("Created installer/wizard-large.bmp")

    # 2. Wizard Small Image (110 x 116) for inner pages header
    w_small, h_small = 110, 116
    small_img = Image.new("RGB", (w_small, h_small), color=(255, 255, 255))
    small_size = 64
    small_icon = icon.resize((small_size, small_size), Image.Resampling.LANCZOS)
    small_img.paste(small_icon, ((w_small - small_size) // 2, (h_small - small_size) // 2), small_icon)
    small_img.save("installer/wizard-small.bmp", "BMP")
    print("Created installer/wizard-small.bmp")

if __name__ == "__main__":
    generate_artwork()
