#!/usr/bin/env python3
"""Redraws docs/progress.svg from the README's Status tables.

Every table row under "## Status" whose last cell is 0.1.1, next,
in progress or not yet counts as one feature. The bar has a block per
feature: released, in the sources, in progress, left. The README's
<img> alt text gets the same counts.
usage: tools/readme_progress.py (from port/ or anywhere)
"""
import os
import re

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
README = os.path.join(ROOT, "README.md")
SVG = os.path.join(ROOT, "docs", "progress.svg")
STATES = ["0.1.1", "next", "in progress", "not yet"]


def count(readme):
    status = readme.split("## Status", 1)[1].split("\n## ", 1)[0]
    states = []
    for line in status.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if line.startswith("|") and cells[-1] in STATES:
            states.append(cells[-1])
    if not states:
        raise SystemExit("no feature rows under ## Status")
    return states


def svg(states):
    n = len(states)
    done = states.count("0.1.1") + states.count("next")
    width, gap, top, height = 720, 3, 34, 22
    block = (width - gap * (n - 1)) / n
    order = sorted(states, key=STATES.index)
    kinds = {"0.1.1": "released", "next": "sources", "in progress": "doing", "not yet": "left"}
    blocks = []
    for i, state in enumerate(order):
        x = i * (block + gap)
        blocks.append(f'<rect class="{kinds[state]}" x="{x:.1f}" y="{top}" '
            f'width="{block:.1f}" height="{height}" rx="2"/>')
    legend, x = [], 0
    for state, label in [("0.1.1", "released in 0.1.1"), ("next", "in the sources"),
            ("in progress", "in progress"), ("not yet", "not started")]:
        k = states.count(state)
        legend.append(f'<rect class="{kinds[state]}" x="{x}" y="70" width="12" height="12" rx="2"/>'
            f'<text class="small" x="{x + 18}" y="80">{k} {label}</text>')
        x += 180
    percent = round(100 * done / n)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="92" viewBox="0 0 {width} 92" role="img" aria-label="{done} of {n} features done, {n - done} left">
<style>
text {{ font-family: -apple-system, "Segoe UI", "Noto Sans", Helvetica, Arial, sans-serif; fill: #1f2328; }}
.title {{ font-size: 15px; font-weight: 600; }}
.small {{ font-size: 12px; fill: #59636e; }}
.released {{ fill: #1f6feb; }}
.sources {{ fill: #54aeff; }}
.doing {{ fill: #d4a72c; }}
.left {{ fill: #d1d9e0; }}
@media (prefers-color-scheme: dark) {{
  text {{ fill: #f0f6fc; }}
  .small {{ fill: #9198a1; }}
  .released {{ fill: #4493f8; }}
  .sources {{ fill: #1f4f8f; }}
  .doing {{ fill: #bb8009; }}
  .left {{ fill: #3d444d; }}
}}
</style>
<text class="title" x="0" y="20">Port progress: {done} of {n} features ({percent}%)</text>
<text class="small" x="{width}" y="20" text-anchor="end">{n - done} left</text>
{chr(10).join(blocks)}
{chr(10).join(legend)}
</svg>
''', done, n


def main():
    with open(README) as f:
        readme = f.read()
    states = count(readme)
    image, done, n = svg(states)
    os.makedirs(os.path.dirname(SVG), exist_ok=True)
    with open(SVG, "w") as f:
        f.write(image)
    alt = f"Port progress: {done} of {n} features done, {n - done} left (see Status)"
    readme = re.sub(r'(<img src="docs/progress.svg" alt=")[^"]*(")', lambda m: m.group(1) + alt + m.group(2), readme)
    with open(README, "w") as f:
        f.write(readme)
    print(f"{done} of {n} done: " + ", ".join(f"{states.count(s)} {s}" for s in STATES))


main()
