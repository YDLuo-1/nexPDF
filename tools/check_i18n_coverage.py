#!/usr/bin/env python3
"""Check that every tr("...") source string in app sources has a Chinese mapping."""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = ["app/main_window.cpp", "app/pdf_canvas.cpp", "app/main.cpp"]
TABLE = ROOT / "app" / "app_translator.cpp"

src = "".join((ROOT / f).read_text(encoding="utf-8") for f in SOURCES)
tr_strings = set(re.findall(r'tr\("((?:[^"\\]|\\.)*)"', src))
mapped = set(re.findall(r'\{"((?:[^"\\]|\\.)*)",\s*"', TABLE.read_text(encoding="utf-8")))
missing = sorted(s for s in tr_strings if s not in mapped and not s.startswith("nexPDF "))
print("tr() strings:", len(tr_strings))
print("unmapped:", missing if missing else "NONE")
raise SystemExit(1 if missing else 0)
