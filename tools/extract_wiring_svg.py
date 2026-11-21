#!/usr/bin/env python3
"""Extract the diagrams of the rig wiring page into standalone SVG files.

The HTML page in docs/wiring/ is the master. Each <svg> with a
data-export="<name>.svg" attribute goes to docs/wiring/<name>.svg, with
the light colours of the page added, so that GitHub and a Markdown
viewer show it. The blind page has no data-export attributes yet.

Usage: python3 tools/extract_wiring_svg.py docs/wiring/rig-wiring.html
"""
import re
import sys
from pathlib import Path

# The light colours of the page (the :root values in its <style>).
# If you change the colours of the page, change them here also.
STYLE = """<style>
  svg { color: #34464c; font-family: "IBM Plex Mono", ui-monospace, Menlo, Consolas, monospace; }
  text { font-size: 12px; fill: currentColor; }
  .t-strong { font-family: "IBM Plex Sans", -apple-system, "Segoe UI", Helvetica, Arial, sans-serif; font-size: 13px; font-weight: 600; }
  .t-muted { fill: #5b6b70; }
  .new { color: #0b7a84; }
  .new text { fill: #0b7a84; }
</style>
<rect width="100%" height="100%" fill="#ffffff"/>"""

SVG_RE = re.compile(r'<svg\b([^>]*)\bdata-export="([^"]+)"([^>]*)>(.*?)</svg>', re.S)


def main(path: str) -> int:
    page = Path(path)
    html = page.read_text(encoding="utf-8")
    found = 0
    for m in SVG_RE.finditer(html):
        before, name, after, inner = m.groups()
        attrs = (before + after).replace('class="dia"', "")
        vb = re.search(r'viewBox="0 0 (\d+) (\d+)"', attrs)
        if not vb:
            print(f"{name}: no viewBox", file=sys.stderr)
            return 1
        w, h = vb.groups()
        attrs = " ".join(attrs.split())
        out = (
            '<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<svg xmlns="http://www.w3.org/2000/svg" '
            f'xmlns:xlink="http://www.w3.org/1999/xlink" width="{w}" height="{h}" {attrs}>\n'
            f"{STYLE}{inner}</svg>\n"
        )
        (page.parent / name).write_text(out, encoding="utf-8")
        print(f"wrote {page.parent / name}")
        found += 1
    if found == 0:
        print("no <svg data-export=...> found", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
