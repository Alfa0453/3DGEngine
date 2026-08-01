from __future__ import annotations

import html
import re
import textwrap
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import (
    BaseDocTemplate,
    CondPageBreak,
    Frame,
    KeepTogether,
    ListFlowable,
    ListItem,
    PageBreak,
    PageTemplate,
    Paragraph,
    Spacer,
    Table,
    TableStyle,
    XPreformatted,
)


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "docs" / "TUTORIAL_Wizards_Trial_All_Systems.md"
OUTPUT = ROOT / "output" / "pdf" / "3DGEngine_Wizards_Trial_All_Systems_Tutorial.pdf"

NAVY = colors.HexColor("#14213D")
BLUE = colors.HexColor("#2563EB")
CYAN = colors.HexColor("#06B6D4")
INK = colors.HexColor("#182230")
MUTED = colors.HexColor("#526072")
PALE = colors.HexColor("#EFF6FF")
GRID = colors.HexColor("#CFD8E6")
CODE_BG = colors.HexColor("#111827")
CODE_FG = colors.HexColor("#E5EEF9")


def inline_markup(value: str) -> str:
    value = html.escape(value.strip())
    value = re.sub(r"`([^`]+)`", r'<font name="Courier">\1</font>', value)
    value = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", value)
    value = re.sub(r"\*([^*]+)\*", r"<i>\1</i>", value)
    return value


def wrap_code(code: str, width: int = 94) -> str:
    output: list[str] = []
    for raw in code.expandtabs(4).splitlines():
        if len(raw) <= width:
            output.append(raw)
            continue
        indent = len(raw) - len(raw.lstrip())
        chunks = textwrap.wrap(
            raw.strip(),
            width=max(32, width - indent),
            subsequent_indent=" " * (indent + 4),
            break_long_words=False,
            break_on_hyphens=False,
        )
        output.extend((" " * indent + chunks[0], *chunks[1:]))
    return "\n".join(output)


class TutorialDoc(BaseDocTemplate):
    def __init__(self, filename: str):
        super().__init__(
            filename,
            pagesize=A4,
            leftMargin=18 * mm,
            rightMargin=18 * mm,
            topMargin=18 * mm,
            bottomMargin=17 * mm,
            title="Wizard's Trial - C++ and Lua Edition",
            author="3DGEngine",
            subject="Complete game-development tutorial",
        )
        content = Frame(
            self.leftMargin,
            self.bottomMargin,
            self.width,
            self.height,
            id="content",
        )
        self.addPageTemplates(
            [
                PageTemplate(id="cover", frames=[content], onPage=self.cover_page),
                PageTemplate(id="body", frames=[content], onPage=self.body_page),
            ]
        )

    def afterPage(self):
        if self.page == 1:
            self.handle_nextPageTemplate("body")

    def cover_page(self, canvas, doc):
        canvas.saveState()
        canvas.setFillColor(NAVY)
        canvas.rect(0, 0, A4[0], A4[1], fill=1, stroke=0)
        canvas.setFillColor(BLUE)
        canvas.rect(0, A4[1] - 18 * mm, A4[0], 18 * mm, fill=1, stroke=0)
        canvas.setFillColor(CYAN)
        canvas.rect(0, 0, 8 * mm, A4[1], fill=1, stroke=0)
        canvas.restoreState()

    def body_page(self, canvas, doc):
        canvas.saveState()
        canvas.setStrokeColor(GRID)
        canvas.setLineWidth(0.5)
        canvas.line(doc.leftMargin, A4[1] - 12 * mm, A4[0] - doc.rightMargin, A4[1] - 12 * mm)
        canvas.setFont("Helvetica-Bold", 8)
        canvas.setFillColor(NAVY)
        canvas.drawString(doc.leftMargin, A4[1] - 9 * mm, "3DGEngine")
        canvas.setFont("Helvetica", 8)
        canvas.setFillColor(MUTED)
        canvas.drawRightString(
            A4[0] - doc.rightMargin,
            A4[1] - 9 * mm,
            "Wizard's Trial - C++ and Lua Edition",
        )
        canvas.setStrokeColor(GRID)
        canvas.line(doc.leftMargin, 11 * mm, A4[0] - doc.rightMargin, 11 * mm)
        canvas.setFont("Helvetica", 8)
        canvas.setFillColor(MUTED)
        canvas.drawString(doc.leftMargin, 7 * mm, "Complete game tutorial")
        canvas.drawRightString(A4[0] - doc.rightMargin, 7 * mm, f"Page {doc.page}")
        canvas.restoreState()


def styles():
    sheet = getSampleStyleSheet()
    return {
        "cover_title": ParagraphStyle(
            "CoverTitle",
            parent=sheet["Title"],
            fontName="Helvetica-Bold",
            fontSize=32,
            leading=37,
            textColor=colors.white,
            alignment=TA_LEFT,
            spaceAfter=10 * mm,
        ),
        "cover_sub": ParagraphStyle(
            "CoverSub",
            parent=sheet["BodyText"],
            fontName="Helvetica",
            fontSize=14,
            leading=20,
            textColor=colors.HexColor("#D8E8FF"),
            spaceAfter=5 * mm,
        ),
        "cover_badge": ParagraphStyle(
            "CoverBadge",
            parent=sheet["BodyText"],
            fontName="Helvetica-Bold",
            fontSize=10,
            leading=14,
            textColor=colors.white,
            backColor=BLUE,
            borderPadding=(6, 10, 6, 10),
            spaceBefore=8 * mm,
        ),
        "h1": ParagraphStyle(
            "H1",
            parent=sheet["Heading1"],
            fontName="Helvetica-Bold",
            fontSize=19,
            leading=23,
            textColor=NAVY,
            spaceBefore=2 * mm,
            spaceAfter=4 * mm,
            keepWithNext=True,
        ),
        "h2": ParagraphStyle(
            "H2",
            parent=sheet["Heading2"],
            fontName="Helvetica-Bold",
            fontSize=15,
            leading=19,
            textColor=BLUE,
            spaceBefore=4 * mm,
            spaceAfter=3 * mm,
            keepWithNext=True,
        ),
        "h3": ParagraphStyle(
            "H3",
            parent=sheet["Heading3"],
            fontName="Helvetica-Bold",
            fontSize=11.5,
            leading=15,
            textColor=NAVY,
            spaceBefore=3 * mm,
            spaceAfter=2 * mm,
            keepWithNext=True,
        ),
        "body": ParagraphStyle(
            "Body",
            parent=sheet["BodyText"],
            fontName="Helvetica",
            fontSize=9.2,
            leading=13.2,
            textColor=INK,
            spaceAfter=2.3 * mm,
        ),
        "bullet": ParagraphStyle(
            "Bullet",
            parent=sheet["BodyText"],
            fontName="Helvetica",
            fontSize=9,
            leading=12.6,
            leftIndent=4 * mm,
            firstLineIndent=-2.5 * mm,
            textColor=INK,
            spaceAfter=1.2 * mm,
        ),
        "code": ParagraphStyle(
            "Code",
            fontName="Courier",
            fontSize=6.3,
            leading=7.7,
            textColor=CODE_FG,
            backColor=CODE_BG,
            borderColor=colors.HexColor("#334155"),
            borderWidth=0.6,
            borderPadding=7,
            spaceBefore=1.5 * mm,
            spaceAfter=3 * mm,
        ),
        "table": ParagraphStyle(
            "Table",
            parent=sheet["BodyText"],
            fontName="Helvetica",
            fontSize=7.1,
            leading=9.3,
            textColor=INK,
        ),
        "table_head": ParagraphStyle(
            "TableHead",
            parent=sheet["BodyText"],
            fontName="Helvetica-Bold",
            fontSize=7.2,
            leading=9.3,
            textColor=colors.white,
        ),
    }


def parse_markdown(text: str):
    st = styles()
    lines = text.splitlines()
    story = [
        Spacer(1, 44 * mm),
        Paragraph("WIZARD'S TRIAL", st["cover_title"]),
        Paragraph(
            "A complete 3DGEngine game tutorial using every engine system",
            st["cover_sub"],
        ),
        Paragraph(
            "C++ AND LUA EDITION",
            st["cover_badge"],
        ),
        Spacer(1, 18 * mm),
        Paragraph(
            "Build an animated wizard, author staff-tip fireballs, create grounded "
            "behavior-tree enemies, connect HUD and audio, and ship a cooked game.",
            st["cover_sub"],
        ),
        Spacer(1, 40 * mm),
        Paragraph(
            "Project workflow | Native assets | Animation | Physics | AI | "
            "Particles | Audio | UI | Cameras | Packaging",
            st["cover_sub"],
        ),
        PageBreak(),
    ]

    i = 2  # The cover replaces the source title and subtitle.
    paragraph: list[str] = []
    main_section_count = 0

    def flush_paragraph():
        nonlocal paragraph
        if paragraph:
            story.append(Paragraph(inline_markup(" ".join(p.strip() for p in paragraph)), st["body"]))
            paragraph = []

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if stripped.startswith("```"):
            flush_paragraph()
            language = stripped[3:].strip()
            i += 1
            block: list[str] = []
            while i < len(lines) and not lines[i].strip().startswith("```"):
                block.append(lines[i])
                i += 1
            label = language.upper() if language else "CONFIGURATION"
            code_label = Paragraph(
                f'<font color="#2563EB"><b>{html.escape(label)}</b></font>',
                ParagraphStyle(
                    "CodeLabel",
                    parent=st["body"],
                    fontSize=7,
                    leading=8,
                    spaceAfter=0,
                    keepWithNext=True,
                ),
            )
            code_body = XPreformatted(html.escape(wrap_code("\n".join(block))), st["code"])
            if len(block) <= 30:
                story.append(KeepTogether([code_label, Spacer(1, 2 * mm), code_body]))
            else:
                story.extend([code_label, Spacer(1, 2 * mm), code_body])
            i += 1
            continue

        if stripped.startswith("|") and "|" in stripped[1:]:
            flush_paragraph()
            rows: list[list[str]] = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                raw = [cell.strip() for cell in lines[i].strip().strip("|").split("|")]
                if not all(re.fullmatch(r":?-{3,}:?", cell or "") for cell in raw):
                    rows.append(raw)
                i += 1
            if rows:
                cols = max(len(row) for row in rows)
                normalized = [row + [""] * (cols - len(row)) for row in rows]
                data = []
                for row_index, row in enumerate(normalized):
                    style = st["table_head"] if row_index == 0 else st["table"]
                    data.append([Paragraph(inline_markup(cell), style) for cell in row])
                widths = [doc_width(cols)] * cols
                table = Table(data, colWidths=widths, repeatRows=1, hAlign="LEFT")
                table.setStyle(
                    TableStyle(
                        [
                            ("BACKGROUND", (0, 0), (-1, 0), NAVY),
                            ("GRID", (0, 0), (-1, -1), 0.35, GRID),
                            ("VALIGN", (0, 0), (-1, -1), "TOP"),
                            ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, PALE]),
                            ("LEFTPADDING", (0, 0), (-1, -1), 4),
                            ("RIGHTPADDING", (0, 0), (-1, -1), 4),
                            ("TOPPADDING", (0, 0), (-1, -1), 4),
                            ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
                        ]
                    )
                )
                story.extend([table, Spacer(1, 3 * mm)])
            continue

        heading = re.match(r"^(#{1,3})\s+(.+)$", stripped)
        if heading:
            flush_paragraph()
            level = len(heading.group(1))
            title = heading.group(2)
            if level == 2:
                main_section_count += 1
                if main_section_count > 1:
                    story.append(PageBreak())
                story.append(Paragraph(inline_markup(title), st["h1"]))
            elif level == 3:
                if title == "14.1 Lua game-manager version":
                    story.append(CondPageBreak(165 * mm))
                story.append(Paragraph(inline_markup(title), st["h2"]))
            else:
                story.append(Paragraph(inline_markup(title), st["h1"]))
            i += 1
            continue

        if re.match(r"^[-*]\s+", stripped):
            flush_paragraph()
            items: list[ListItem] = []
            while i < len(lines) and re.match(r"^[-*]\s+", lines[i].strip()):
                item = re.sub(r"^[-*]\s+", "", lines[i].strip())
                items.append(ListItem(Paragraph(inline_markup(item), st["bullet"])))
                i += 1
            story.append(
                ListFlowable(
                    items,
                    bulletType="bullet",
                    start="circle",
                    leftIndent=6 * mm,
                    bulletFontName="Helvetica",
                    bulletFontSize=7,
                    bulletColor=BLUE,
                    spaceAfter=2 * mm,
                )
            )
            continue

        numbered = re.match(r"^\d+\.\s+(.+)$", stripped)
        if numbered:
            flush_paragraph()
            items = []
            while i < len(lines):
                match = re.match(r"^\d+\.\s+(.+)$", lines[i].strip())
                if not match:
                    break
                items.append(ListItem(Paragraph(inline_markup(match.group(1)), st["bullet"])))
                i += 1
            story.append(
                ListFlowable(
                    items,
                    bulletType="1",
                    leftIndent=8 * mm,
                    bulletFontName="Helvetica-Bold",
                    bulletFontSize=8,
                    bulletColor=BLUE,
                    spaceAfter=2 * mm,
                )
            )
            continue

        if stripped == "---":
            flush_paragraph()
            story.append(Spacer(1, 2 * mm))
            i += 1
            continue

        if not stripped:
            flush_paragraph()
            i += 1
            continue

        paragraph.append(stripped)
        i += 1

    flush_paragraph()
    return story


def doc_width(columns: int) -> float:
    usable = A4[0] - 36 * mm
    return usable / max(1, columns)


def main():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    document = TutorialDoc(str(OUTPUT))
    document.build(parse_markdown(SOURCE.read_text(encoding="utf-8")))
    print(OUTPUT)


if __name__ == "__main__":
    main()
