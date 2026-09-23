"""Server-side deterministic CSV and PDF journal exports."""

from __future__ import annotations

import csv
import io
from xml.sax.saxutils import escape

from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle
from reportlab.lib import colors


def _csv_safe(value) -> str:
    text = str(value or "")
    return "'" + text if text.startswith(("=", "+", "-", "@")) else text


def csv_export(task, entries) -> bytes:
    stream = io.StringIO(newline="")
    writer = csv.writer(stream)
    writer.writerow(["task_name", "task_number", "task_created_at", "sequence_number",
                     "timestamp", "username", "display_name", "text"])
    for entry in entries:
        writer.writerow([_csv_safe(value) for value in [
            task["name"], task["task_number"], task["created_at"], entry["sequence_number"],
            entry["server_timestamp"], entry["username"], entry["display_name"], entry["text"],
        ]])
    return stream.getvalue().encode("utf-8-sig")


def pdf_export(task, entries) -> bytes:
    output = io.BytesIO()
    document = SimpleDocTemplate(
        output, pagesize=A4, leftMargin=16 * mm, rightMargin=16 * mm,
        topMargin=16 * mm, bottomMargin=16 * mm,
        title=f"{task['task_number']} – {task['name']}",
    )
    styles = getSampleStyleSheet()
    body = ParagraphStyle("JournalBody", parent=styles["BodyText"], fontName="Helvetica", fontSize=9,
                          leading=12, alignment=TA_LEFT)
    story = [
        Paragraph("Tehtäväpäiväkirja", styles["Title"]),
        Paragraph(f"<b>Tehtävä:</b> {escape(task['name'])}", body),
        Paragraph(f"<b>Tehtävänumero:</b> {escape(task['task_number'])}", body),
        Paragraph(f"<b>Aloitusaika:</b> {escape(task['created_at'])}", body),
        Paragraph(f"<b>Päättymisaika:</b> {escape(task['ended_at'] or '–')}", body),
        Spacer(1, 6 * mm),
    ]
    rows = [["#", "Aikaleima", "Käyttäjä", "Merkintä"]]
    for entry in entries:
        rows.append([
            str(entry["sequence_number"]), entry["server_timestamp"],
            Paragraph(escape(entry["display_name"]), body), Paragraph(escape(entry["text"]), body),
        ])
    table = Table(rows, colWidths=[12 * mm, 38 * mm, 36 * mm, 90 * mm], repeatRows=1)
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#d52b1e")),
        ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
        ("GRID", (0, 0), (-1, -1), 0.25, colors.grey),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("FONTSIZE", (0, 0), (-1, 0), 9),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f4f4f4")]),
    ]))
    story.append(table)
    document.build(story)
    return output.getvalue()
