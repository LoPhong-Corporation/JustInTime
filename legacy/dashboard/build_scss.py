"""
Script tuy chon de bien dich lai static/scss/main.scss thanh
static/style.css sau khi ban sua theme.

Cai dat:
    pip install libsass

Chay:
    python build_scss.py
"""

import sass

with open("static/scss/main.scss", "r", encoding="utf-8") as f:
    scss_source = f.read()

compiled_css = sass.compile(string=scss_source)

with open("static/style.css", "w", encoding="utf-8") as f:
    f.write(compiled_css)

print("Da bien dich static/scss/main.scss -> static/style.css")
