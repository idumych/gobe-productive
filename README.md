# Gobe Productive for 64-bit Haiku

A native x86_64 Haiku port of Gobe Productive 2.0.1 (BeOS R5), rebuilt from
the decompiled original (`../decomp/`). It reads its resources (menus,
dialogs, strings, icons) from the user's own original `Gobe Productive`
binary at runtime; nothing from the original is in this tree.

## Layout

| Path | Contents |
|------|----------|
| `src/core/` | Portable C++17, no Haiku API: resource reader (`ResourceFile`, `RsrReader`, `Formats`), document reader (`DocArchive`, `DocSections`, `WPPart`, `GRPart`, `SSPart`, `IMPart`, `ChartPart`, `TablePart`, `DocWriter`, `Editing`, `PartTail`, `Formula`, `Styles`, `Document`), spreadsheet evaluation and cell formatting (`SSEval`, `SSStyle`). Builds and is tested on Linux. |
| `src/app/` | Haiku UI: `TApp`, `TDocWindow` (base menu bar), `TMenu`/`TMenuItem` (MENU resources), `TDialog` (DIAL + Item resources), `TPageView` (pages, frames, text, graphics, spreadsheets), `TDocView` (plain text fallback). |
| `tests/` | Host tests against the original files (see below). |

## Building

On Haiku (`pkgman install gcc make haiku_devel zlib_devel` on a fresh
install; `pkgman install liberation_fonts` for documents to lay out as in
Gobe: Liberation Serif/Sans/Mono have the advance widths of the Times,
Helvetica and Courier designs BeOS's Bitstream fonts were, while the Noto
fallback is wider):

    make
    GOBE_ORIGINAL="/path/to/Gobe Productive 2.0/Gobe Productive" build/GobeProductive [document...]

Or put `build/GobeProductive` next to the original `Gobe Productive` binary.
Test options: `--menu <index>` opens a menu-bar menu, `--dialog <id>` opens a
DIAL resource, `--render out.png` draws the document into a PNG and quits,
`--slide <n>` shows a presentation slide, `--do '<script>'` (with
`--render`) clicks, types and saves first.

## Host tests (Linux, from the project root)

    port/tests/check_core.sh        # resource parsing == tools/gobersrc.py, all binaries
    c++ -std=c++17 -Iport/src/core port/tests/doc_sections.cpp port/src/core/*.cpp -o /tmp/ds
    /tmp/ds original/installed/.../Samples/*   # each document section consumed exactly
    # likewise tests/doc_text, doc_styles, doc_frames, doc_graphics,
    # doc_spreadsheet (parts read exactly, formulas evaluated, known values),
    # doc_images, doc_tables, doc_save (unchanged documents save byte for
    # byte), doc_edit (formulas reparse to the same tokens, edits undone
    # write the same bytes, cell input round trips)
    # and tests/ss_dump.cpp (prints cells, formulas and computed values)

## Status

Milestone 1 (done): menus and dialogs from the original resources.
- Base menu bar exactly as `TNormalDocWinHelper::InstallBaseMenus` (MENU 2-6,
  part menu in Frame), shortcuts, submenus, value menus.
- All 52 dialogs open from DIAL/Item, with Haiku layouts standing in for
  Gobe's Engineer layout classes; Gobe-specific views are placeholders.

Milestone 2 (in progress): documents.
- Reader: header, table of contents; VersionMgr, PartFactory Table, Document
  Misc, StyleMgr (properties, styles), PartMgr (all part records) read
  exactly in all 14 samples; word processing parts (paragraphs, style runs,
  formula/footnote/frame infos, text, sections) in all 65 WORD parts.
- Text properties decoded (font, size, face, color, alignment, indents,
  spacing) with style inheritance.
- Frames and portals: where every part sits (all 254 part tails read).
- Graphics parts: rectangles, rounded rectangles, ellipses, arcs, lines,
  Bezier paths with fills (solid, gradients), pens, rotation.
- Spreadsheet parts: cells, header sizes, styles (fonts, fills, borders,
  alignment, number formats), formulas evaluated (the functions the samples
  use, named ranges across parts), text overflow, grid, headers.
- Viewer (`TPageView`): pages and frames laid out as in the document, text
  flowed through its frames, graphics and spreadsheets drawn; `--render
  out.png doc` writes a PNG.
- Images (BITS), clocks (current time), charts (2D rendition of the cached
  data with legend/title; the original is 3D OpenGL), presentations (the
  current slide; layers hidden in a view aren't drawn).
- All 14 samples render: `vm/render.sh vm/renders <sample>...`.
- Fonts: Dutch801 -> Liberation Serif, Swiss721 -> Liberation Sans,
  Courier10 -> Liberation Mono (Noto when not installed); VAG Rounded and
  Haru -> sans.
- Tab stops (left/center/right/decimal, leaders) and line spacing.
- Presentations: arrows, Page Up/Down, space or a click change slides.
- Tables (the Avery label templates): cell fills, outlines, borders.

Milestone 3 (started): editing.
- Text: caret, typing, Backspace/Delete, Enter, arrows, Home/End.
- Spreadsheets: select, edit in place (values, text, dates, formulas parsed
  into Gobe's tokens), recalculation.
- File > Save / Save As / Revert to Saved; only edited parts are written,
  everything else is copied byte for byte; the spreadsheet dependency and
  link tables, text frame ranges and selections are updated to match the
  edits. Saved files load in the port; they haven't been tried in BeOS
  Gobe.
- Not yet: selections, undo, styling, frames/graphics editing.
