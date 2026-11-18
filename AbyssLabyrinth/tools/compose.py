import sys, glob, os
from PIL import Image, ImageDraw, ImageFont

reg = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
bold = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"
KR = 1  # index of KR face in ttc
small = ImageFont.truetype(reg, 11, index=KR)
big = ImageFont.truetype(bold if os.path.exists(bold) else reg, 20, index=KR)

def c565(c):
    return (((c >> 11) & 31) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3)

out = []
for ppm in sorted(glob.glob("/tmp/shots/*.ppm")):
    im = Image.open(ppm).convert("RGB")
    d = ImageDraw.Draw(im)
    for ln in open(ppm[:-4] + ".txt", encoding="utf-8"):
        p = ln.rstrip("\n").split(" ", 4)
        if len(p) < 5: continue
        x, y, c, fl, s = int(p[0]), int(p[1]), int(p[2]), int(p[3]), p[4]
        f = big if fl & 8 else small
        if fl & 4:
            d.text((x + 1, y + 1 - 1), s, font=f, fill=(0, 0, 0))
        d.text((x, y - 1), s, font=f, fill=c565(c))
    im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
    png = ppm[:-4] + ".png"
    im.save(png)
    out.append(png)
# contact sheet
ims = [Image.open(p) for p in out]
cols = 4
w, h = ims[0].size
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (w + 8), rows * (h + 8)), (40, 40, 40))
for i, m in enumerate(ims):
    sheet.paste(m, ((i % cols) * (w + 8), (i // cols) * (h + 8)))
sheet.save("/tmp/shots/sheet.png")
print(len(out))
