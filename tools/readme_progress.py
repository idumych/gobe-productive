#!/usr/bin/env python3
"""Redraws the progress bar (docs/progress-<done>-<total>.svg) from the
README's feature tables.

Every table row under "## Features" whose last cell is "done" or "not yet"
counts as one feature. The bar is filled to the share that is done; the
README's <img> points at it and its alt text gets the same counts.
usage: tools/readme_progress.py (from port/ or anywhere)
"""
import glob
import os
import re

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
README = os.path.join(ROOT, "README.md")
DOCS = os.path.join(ROOT, "docs")
STATES = ["done", "not yet"]


def count(readme):
    status = readme.split("## Features", 1)[1].split("\n## ", 1)[0]
    states = []
    for line in status.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if line.startswith("|") and cells[-1] in STATES:
            states.append(cells[-1])
    if not states:
        raise SystemExit("no feature rows under ## Features")
    return states


def svg(states):
    n = len(states)
    done = states.count("done")
    width, top, height = 720, 34, 22
    fill = width * done / n
    percent = round(100 * done / n)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="64" viewBox="0 0 {width} 64" role="img" aria-label="{done} of {n} features done ({percent}%)">
<style>
text {{ font-family: -apple-system, "Segoe UI", "Noto Sans", Helvetica, Arial, sans-serif; fill: #1f2328; }}
.title {{ font-size: 15px; font-weight: 600; }}
.small {{ font-size: 12px; fill: #59636e; }}
.done {{ fill: #1f6feb; }}
.left {{ fill: #d1d9e0; }}
@media (prefers-color-scheme: dark) {{
  text {{ fill: #f0f6fc; }}
  .small {{ fill: #9198a1; }}
  .done {{ fill: #4493f8; }}
  .left {{ fill: #3d444d; }}
}}
</style>
<text class="title" x="0" y="20">Port progress: {percent}%</text>
<text class="small" x="{width}" y="20" text-anchor="end">{done} of {n} features done</text>
<clipPath id="bar"><rect x="0" y="{top}" width="{width}" height="{height}" rx="4"/></clipPath>
<g clip-path="url(#bar)">
<rect class="left" x="0" y="{top}" width="{width}" height="{height}"/>
<rect class="done" x="0" y="{top}" width="{fill:.1f}" height="{height}"/>
</g>
</svg>
''', done, n


def main():
    with open(README) as f:
        readme = f.read()
    states = count(readme)
    image, done, n = svg(states)
    # a new file name whenever the counts change (progress-<done>-<total>.svg),
    # the old ones removed: GitHub's caches keep an
    # image by its address for minutes and ignore a query string
    os.makedirs(DOCS, exist_ok=True)
    name = f"progress-{done}-{n}.svg"
    for old in glob.glob(os.path.join(DOCS, "progress*.svg")):
        if os.path.basename(old) != name:
            os.remove(old)
    with open(os.path.join(DOCS, name), "w") as f:
        f.write(image)
    alt = f"Port progress: {done} of {n} features done (see Features)"
    readme = re.sub(r'<img src="docs/progress[^"]*" alt="[^"]*"',
        f'<img src="docs/{name}" alt="{alt}"', readme)
    with open(README, "w") as f:
        f.write(readme)
    print(f"{done} of {n} done: " + ", ".join(f"{states.count(s)} {s}" for s in STATES))


main()
