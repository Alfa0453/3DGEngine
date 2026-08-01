from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
RENDER = ROOT / "tmp" / "pdfs" / "complete_render"
OUTPUT = ROOT / "tmp" / "pdfs"

files = sorted(RENDER.glob("page-*.png"))
thumb_width = 260
label_height = 22
columns = 4
rows = 5
pages_per_sheet = columns * rows

for sheet_index in range((len(files) + pages_per_sheet - 1) // pages_per_sheet):
    group = files[sheet_index * pages_per_sheet:(sheet_index + 1) * pages_per_sheet]
    cells = []
    for path in group:
        image = Image.open(path).convert("RGB")
        scale = thumb_width / image.width
        resized = image.resize((thumb_width, int(image.height * scale)))
        cell = Image.new("RGB", (thumb_width, resized.height + label_height), "white")
        cell.paste(resized, (0, label_height))
        draw = ImageDraw.Draw(cell)
        page = int(path.stem.split("-")[-1])
        draw.text((7, 5), f"Page {page}", fill="#14213D", font=ImageFont.load_default())
        cells.append(cell)

    cell_height = max(cell.height for cell in cells)
    sheet = Image.new(
        "RGB",
        (thumb_width * columns, cell_height * rows),
        "#D9E2EF",
    )
    for index, cell in enumerate(cells):
        sheet.paste(
            cell,
            ((index % columns) * thumb_width, (index // columns) * cell_height),
        )
    destination = OUTPUT / f"complete_systems_contact_{sheet_index + 1}.png"
    sheet.save(destination)
    print(destination)
