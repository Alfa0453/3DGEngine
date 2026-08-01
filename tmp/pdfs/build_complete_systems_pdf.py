from __future__ import annotations

import html
import re
import textwrap
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import (
    BaseDocTemplate,
    CondPageBreak,
    Frame,
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
DOCS = ROOT / "docs" / "engine-systems"
OUTPUT = ROOT / "output" / "pdf" / "3DGEngine_Complete_Systems_Reference.pdf"

CHAPTERS = [
    ("Systems overview", "README.md"),
    ("Core application and project architecture", "01-core-architecture.md"),
    ("ECS, scenes, worlds, and level streaming", "02-ecs-scenes.md"),
    ("Native assets, importing, loading, and cooking", "03-assets.md"),
    ("Rendering, lighting, cameras, environment, terrain, and water", "04-rendering.md"),
    ("Materials, textures, and shader graphs", "05-materials-shaders.md"),
    ("Animation, graphs, blend spaces, actions, sockets, and characters", "06-animation.md"),
    ("Physics, collision, controllers, joints, and ragdolls", "07-physics.md"),
    ("AI, navigation, perception, behavior trees, and blackboards", "08-ai-navigation.md"),
    ("Gameplay framework, C++ and Lua scripting, timers, and global time", "09-gameplay-scripting.md"),
    ("Audio engine, sources, cues, mixer, music, and editor", "10-audio.md"),
    ("Particle simulation, modules, rendering, and scripting", "11-particles.md"),
    ("Runtime UI, HUD documents, bindings, fonts, and editor", "12-ui-hud.md"),
    ("Editor application, panels, asset editors, and Play mode", "13-editor.md"),
    ("Build, standalone player, packaging, diagnostics, and tests", "14-build-runtime-testing.md"),
    ("Public source coverage appendix", "SOURCE_COVERAGE.md"),
]

NAVY = colors.HexColor("#12233F")
BLUE = colors.HexColor("#2563EB")
CYAN = colors.HexColor("#0EA5B7")
INK = colors.HexColor("#172033")
MUTED = colors.HexColor("#526176")
PALE = colors.HexColor("#EFF6FF")
GRID = colors.HexColor("#CBD5E1")
CODE_BG = colors.HexColor("#111827")
CODE_FG = colors.HexColor("#E5EEF9")


def ascii_punctuation(value: str) -> str:
    return (
        value.replace("\u2011", "-")
        .replace("\u2013", "-")
        .replace("\u2014", "-")
        .replace("\u2212", "-")
        .replace("\u2192", "->")
        .replace("\u2018", "'")
        .replace("\u2019", "'")
        .replace("\u201c", '"')
        .replace("\u201d", '"')
    )


def inline_markup(value: str) -> str:
    value = ascii_punctuation(value.strip())
    value = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", value)
    value = html.escape(value)
    value = re.sub(r"`([^`]+)`", r'<font name="Courier">\1</font>', value)
    value = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", value)
    value = re.sub(r"\*([^*]+)\*", r"<i>\1</i>", value)
    return value


def wrap_code(code: str, width: int = 96) -> str:
    output: list[str] = []
    for raw in ascii_punctuation(code).expandtabs(4).splitlines():
        if len(raw) <= width:
            output.append(raw)
            continue
        indent = len(raw) - len(raw.lstrip())
        chunks = textwrap.wrap(
            raw.strip(),
            width=max(34, width - indent),
            subsequent_indent=" " * (indent + 4),
            break_long_words=True,
            break_on_hyphens=False,
        )
        output.extend((" " * indent + chunks[0], *chunks[1:]))
    return "\n".join(output)


def make_styles():
    sheet = getSampleStyleSheet()
    return {
        "cover_title": ParagraphStyle(
            "CoverTitle",
            parent=sheet["Title"],
            fontName="Helvetica-Bold",
            fontSize=31,
            leading=36,
            textColor=colors.white,
            alignment=TA_LEFT,
            spaceAfter=8 * mm,
        ),
        "cover_sub": ParagraphStyle(
            "CoverSub",
            parent=sheet["BodyText"],
            fontName="Helvetica",
            fontSize=13.5,
            leading=19,
            textColor=colors.HexColor("#DCEAFF"),
            spaceAfter=5 * mm,
        ),
        "cover_badge": ParagraphStyle(
            "CoverBadge",
            parent=sheet["BodyText"],
            fontName="Helvetica-Bold",
            fontSize=9.5,
            leading=13,
            textColor=colors.white,
            backColor=BLUE,
            borderPadding=(6, 10, 6, 10),
            spaceBefore=5 * mm,
        ),
        "chapter": ParagraphStyle(
            "Chapter",
            parent=sheet["Heading1"],
            fontName="Helvetica-Bold",
            fontSize=19,
            leading=23,
            textColor=NAVY,
            spaceAfter=5 * mm,
            keepWithNext=True,
        ),
        "h2": ParagraphStyle(
            "H2",
            parent=sheet["Heading2"],
            fontName="Helvetica-Bold",
            fontSize=14.5,
            leading=18,
            textColor=BLUE,
            spaceBefore=4 * mm,
            spaceAfter=2.5 * mm,
            keepWithNext=True,
        ),
        "h3": ParagraphStyle(
            "H3",
            parent=sheet["Heading3"],
            fontName="Helvetica-Bold",
            fontSize=11.2,
            leading=14,
            textColor=NAVY,
            spaceBefore=3 * mm,
            spaceAfter=1.8 * mm,
            keepWithNext=True,
        ),
        "body": ParagraphStyle(
            "Body",
            parent=sheet["BodyText"],
            fontName="Helvetica",
            fontSize=9,
            leading=12.7,
            textColor=INK,
            spaceAfter=2.2 * mm,
            splitLongWords=True,
        ),
        "bullet": ParagraphStyle(
            "Bullet",
            parent=sheet["BodyText"],
            fontName="Helvetica",
            fontSize=8.8,
            leading=12.2,
            textColor=INK,
            splitLongWords=True,
        ),
        "code_label": ParagraphStyle(
            "CodeLabel",
            parent=sheet["BodyText"],
            fontName="Helvetica-Bold",
            fontSize=7,
            leading=8,
            textColor=BLUE,
            spaceAfter=1,
            keepWithNext=True,
        ),
        "code": ParagraphStyle(
            "Code",
            fontName="Courier",
            fontSize=6.6,
            leading=8.4,
            textColor=CODE_FG,
            backColor=CODE_BG,
            borderColor=colors.HexColor("#334155"),
            borderWidth=0.6,
            borderPadding=7,
            spaceBefore=1 * mm,
            spaceAfter=3 * mm,
        ),
        "table": ParagraphStyle(
            "Table",
            parent=sheet["BodyText"],
            fontName="Helvetica",
            fontSize=7.1,
            leading=9.2,
            textColor=INK,
            splitLongWords=True,
        ),
        "table_head": ParagraphStyle(
            "TableHead",
            parent=sheet["BodyText"],
            fontName="Helvetica-Bold",
            fontSize=7.1,
            leading=9.2,
            textColor=colors.white,
        ),
        "toc": ParagraphStyle(
            "Toc",
            parent=sheet["BodyText"],
            fontName="Helvetica",
            fontSize=9.5,
            leading=13.5,
            textColor=INK,
            leftIndent=6 * mm,
            firstLineIndent=-6 * mm,
            spaceAfter=1.5 * mm,
        ),
    }


class SystemsReferenceDoc(BaseDocTemplate):
    def __init__(self, filename: str):
        super().__init__(
            filename,
            pagesize=A4,
            leftMargin=18 * mm,
            rightMargin=18 * mm,
            topMargin=18 * mm,
            bottomMargin=17 * mm,
            title="3DGEngine Complete Systems Reference",
            author="3DGEngine",
            subject="Current engine, editor, asset, gameplay, and runtime systems",
            creator="3DGEngine documentation build",
        )
        frame = Frame(
            self.leftMargin,
            self.bottomMargin,
            self.width,
            self.height,
            id="content",
        )
        self.addPageTemplates(
            [
                PageTemplate(id="cover", frames=[frame], onPage=self.cover_page),
                PageTemplate(id="body", frames=[frame], onPage=self.body_page),
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
            "Complete Systems Reference - July 2026",
        )
        canvas.line(doc.leftMargin, 11 * mm, A4[0] - doc.rightMargin, 11 * mm)
        canvas.drawString(doc.leftMargin, 7 * mm, "Engine and editor documentation")
        canvas.drawRightString(A4[0] - doc.rightMargin, 7 * mm, f"Page {doc.page}")
        canvas.restoreState()


def parse_markdown(text: str, story: list, styles: dict):
    lines = text.splitlines()
    i = 0
    paragraph: list[str] = []

    def flush_paragraph():
        nonlocal paragraph
        if paragraph:
            joined = " ".join(p.strip() for p in paragraph)
            story.append(Paragraph(inline_markup(joined), styles["body"]))
            paragraph = []

    while i < len(lines):
        stripped = lines[i].strip()

        if stripped.startswith("```"):
            flush_paragraph()
            language = stripped[3:].strip() or "configuration"
            i += 1
            block: list[str] = []
            while i < len(lines) and not lines[i].strip().startswith("```"):
                block.append(lines[i])
                i += 1
            story.append(Paragraph(html.escape(language.upper()), styles["code_label"]))
            story.append(
                XPreformatted(
                    html.escape(wrap_code("\n".join(block))),
                    styles["code"],
                )
            )
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
                    style = styles["table_head"] if row_index == 0 else styles["table"]
                    data.append([Paragraph(inline_markup(cell), style) for cell in row])
                widths = [(A4[0] - 36 * mm) / cols] * cols
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
            title = inline_markup(heading.group(2))
            if level == 1:
                # The chapter title is supplied by the document assembly.
                i += 1
                continue
            story.append(CondPageBreak(22 * mm))
            story.append(Paragraph(title, styles["h2" if level == 2 else "h3"]))
            i += 1
            continue

        if re.match(r"^[-*]\s+", stripped):
            flush_paragraph()
            items: list[ListItem] = []
            while i < len(lines) and re.match(r"^[-*]\s+", lines[i].strip()):
                item = re.sub(r"^[-*]\s+", "", lines[i].strip())
                items.append(ListItem(Paragraph(inline_markup(item), styles["bullet"])))
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

        if re.match(r"^\d+\.\s+", stripped):
            flush_paragraph()
            items: list[ListItem] = []
            while i < len(lines):
                match = re.match(r"^\d+\.\s+(.+)$", lines[i].strip())
                if not match:
                    break
                items.append(ListItem(Paragraph(inline_markup(match.group(1)), styles["bullet"])))
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


def build_story():
    styles = make_styles()
    story = [
        Spacer(1, 43 * mm),
        Paragraph("3DGENGINE", styles["cover_title"]),
        Paragraph("Complete Systems Reference", styles["cover_title"]),
        Paragraph(
            "A source-aligned guide to the engine, editor, native asset pipeline, "
            "runtime, and gameplay framework.",
            styles["cover_sub"],
        ),
        Paragraph(
            "JULY 2026 EDITION",
            styles["cover_badge"],
        ),
        Spacer(1, 19 * mm),
        Paragraph(
            "Includes level-as-asset streaming, C++ and Lua scripting, named timers, "
            "global time dilation, hit stops, animation, physics, AI, particles, "
            "audio, HUD, cameras, shaders, native assets, cooking, and diagnostics.",
            styles["cover_sub"],
        ),
        Spacer(1, 32 * mm),
        Paragraph(
            "109 runtime headers | 35 editor headers | 14 system chapters | "
            "source coverage appendix",
            styles["cover_sub"],
        ),
        PageBreak(),
        Paragraph("Contents", styles["chapter"]),
    ]

    for index, (title, _) in enumerate(CHAPTERS, start=1):
        story.append(
            Paragraph(
                f'<font color="#2563EB"><b>{index:02d}</b></font>  {inline_markup(title)}',
                styles["toc"],
            )
        )

    for index, (title, filename) in enumerate(CHAPTERS, start=1):
        story.append(PageBreak())
        story.append(
            Paragraph(
                f'<font color="#2563EB">{index:02d}</font>  {inline_markup(title)}',
                styles["chapter"],
            )
        )
        parse_markdown((DOCS / filename).read_text(encoding="utf-8"), story, styles)

    return story


def main():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    document = SystemsReferenceDoc(str(OUTPUT))
    document.build(build_story())
    print(OUTPUT)


if __name__ == "__main__":
    main()
