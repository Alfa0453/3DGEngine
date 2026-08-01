from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
PAGES = [1, 11, 22, 23, 27, 28, 34, 41, 47]
files = [ROOT / "tmp" / "pdfs" / f"wizard_updated-{page:02d}.png" for page in PAGES]
images = [Image.open(path).convert("RGB") for path in files]

thumb_width = 420
label_height = 28
thumbs = []
for page, image in zip(PAGES, images):
    scale = thumb_width / image.width
    resized = image.resize((thumb_width, int(image.height * scale)))
    cell = Image.new("RGB", (thumb_width, resized.height + label_height), "white")
    cell.paste(resized, (0, label_height))
    draw = ImageDraw.Draw(cell)
    draw.text((8, 7), f"Page {page}", fill="#14213D", font=ImageFont.load_default())
    thumbs.append(cell)

cell_height = max(image.height for image in thumbs)
sheet = Image.new("RGB", (thumb_width * 3, cell_height * 3), "#D9E2EF")
for index, image in enumerate(thumbs):
    x = (index % 3) * thumb_width
    y = (index // 3) * cell_height
    sheet.paste(image, (x, y))

output = ROOT / "tmp" / "pdfs" / "wizard_updated_contact_sheet.png"
sheet.save(output)
print(output)
