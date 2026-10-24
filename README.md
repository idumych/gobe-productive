# Gobe Productive for 64-bit Haiku

A native x86_64 Haiku port of Gobe Productive 2.0.1 (BeOS R5), rebuilt from
the decompiled original (`../decomp/`). It reads its resources (menus,
dialogs, strings, icons) from the user's own original `Gobe Productive`
binary at runtime; nothing from the original is in this tree.

## Installing the preview package

    pkgman install gobe_productive_port-0.1~preview-1-x86_64.hpkg
    # or copy it to ~/config/packages

"Gobe Productive" then appears in the Deskbar's Applications. It needs your
original Gobe Productive 2.0 folder somewhere on your disks: on first start
it looks for its "Gobe Productive" program file (next to the app, in the
usual install folders, then by name on every volume) and otherwise asks
where it is; the place is remembered in
`~/config/settings/Gobe Productive port settings`. Installing
`liberation_fonts` makes documents lay out with the widths of the fonts they
were made with.

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

Without `GOBE_ORIGINAL` it finds the original as the package does.
`package/make_package.sh [version]` (on Haiku, in `port/`) builds
`build/gobe_productive_port-<version>-x86_64.hpkg`; its license file is a
placeholder until the port's terms are decided.

Test options: `--menu <index>` opens a menu-bar menu, `--dialog <id>` opens a
DIAL resource, `--render out.png` draws the document into a PNG and quits,
`--slide <n>` shows a presentation slide, `--do '<script>'` clicks, types,
sends commands and saves first (with `--render` on the offscreen page;
without it, in the document's window; see `TPageView::RunScript`).
`GOBE_DEBUG_MENUS=1` adds a menu listing every DIAL resource.

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

## Status (preview)

Works:
- Opening and saving Gobe Productive documents (only what was edited is
  rewritten, everything else is kept byte for byte); File > New (the New
  Document dialog; word processing documents so far), Open Recent, Revert,
  Insert… (text and RTF files), Page Setup, Print, Help (the original's
  User's Guide), About.
- Word processing: typing, selection (double, triple and quadruple clicks;
  Command, Option and Control with the arrows, Home/End, Page Up/Down),
  cut/copy/paste with styles, undo and redo, Find/Change (across the text
  parts, with Wrap, Entire Word and special characters), the context menu
  (copy/paste format, Open URL, suggested spellings), fonts, sizes (and Other…), faces (superscript and
  subscript too), colors, alignment, Paragraph Format, tabs, line and
  paragraph spacing, Copy/Paste Format, Show/Hide Invisibles; pages that
  come and go with the text; Document > Margins, Insert/Remove Header and
  Footer; Format > Columns, Insert Page #, Date, Time, Formula (live
  fields), Footnote and endnote (with Footnote Options), Separator Line,
  Line/Frame/Page Break; Writing Tools > Word Count; the text ruler
  (indents and tab stops dragged, added, removed, Edit Tab, the ruler's
  unit and divisions); the FormatBar (font, size, face, text color from the
  Standard color set, paragraph styles applied and made with New Style…)
  and the PartBar (alignment, line
  spacing, columns, invisibles); Window > Cascade, Tile, Cycle, Hide/Show
  FormatBar and PartBar; zoom (the zoom tool, Fit to Window) and the page
  indicator; Document > Page View off (the browse view); spell checking
  (Document > Spell Check, the SpellBar, the PartBar's button and Spell
  Dictionaries…, with the original's dictionaries; the chosen dictionaries
  last until the port quits).
- Plain text and RTF: opened as documents, saved with Save As's File Format.
- Viewing: drawings, spreadsheets (formulas evaluated, cells editable),
  charts (2D), images, tables, clocks and presentations (slides).

Not yet: the ToolBar and the other ribbons; in the FormatBar the pattern
and gradient inks, fill, pen and transparency, basic styles on text, Edit
Styles and named selections; the thesaurus and dictionary, spell checking
outside text parts,
Views & Layers, Preferences, New Window, frames (creating, linking, autogrow), drawing tools,
spreadsheet menus beyond a few, new documents of other kinds, and the HTML,
Word, Excel and SYLK translators. Documents saved by the port open in the
port; they haven't been tried in BeOS Gobe Productive.

