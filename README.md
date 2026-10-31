# Gobe Productive, ported

Gobe Productive 2.0.1, the office suite for BeOS, rebuilt to run on today's
systems: **Haiku** (and Renku OS) and **Linux** (X11 and Wayland). It opens and saves Gobe Productive
documents and works the way the original did, down to its menus and dialogs.

The released **preview** (0.1.1) is focused on the word processor. Since
then, work has moved to spreadsheets: most of the original's spreadsheet
editing now works in the sources here and will be in the next release. See
[Status](#status).

> **Unofficial.** This is an independent reimplementation, not affiliated
> with Gobe Software or the current owners of Gobe Productive. It contains
> **no part of the original program**: no code, menus, dialogs, pictures,
> sample documents or dictionaries. It needs your own copy of the original
> (below). See [LICENSE](LICENSE) for the licence and the full notice.

## You need the original Gobe Productive 2.0

The port reads the original's menus, dialogs, strings and pictures (and its
dictionaries, help and samples) from your own copy at runtime. The original
can't be installed on any current system, so the port unpacks it itself
from what you have:

- the Gobe Productive **CD image** (an `.iso` file), or
- its **installer**, the file named `Install Gobe Productive` (from the CD
  or the download), or
- a **folder** copied from the CD or from an old BeOS installation (the
  `Gobe Productive 2.0` folder, or any folder holding the installer).

On first start, if it doesn't find it, the port says so: click **Locate…**
and choose the image, the installer or the folder. It unpacks the original
into its settings folder and remembers it, so this happens once:

| System | Unpacked into |
|---|---|
| Haiku, Renku OS | `~/config/settings/Gobe Productive port/Original` |
| Linux | `~/.config/cosmoe/Gobe Productive port/Original` |

It also looks on its own in the usual places (`~/Gobe Productive 2.0`,
`~/config/non-packaged/apps/Gobe Productive 2.0`, `/boot/apps/…`, next to
the app), and you can name the image, installer or program in the
`GOBE_ORIGINAL` environment variable.

BeOS-format (BFS) CD images can't be read yet: copy the installer or the
folder out of the image first (Haiku can mount it), and choose that.

## Install

The current release is **0.1.1 (preview)**. It changes only the Linux
AppImage, which now draws like Haiku in the places where 0.1.0 didn't
(disabled buttons, icon backgrounds, the ruler) and closes a tooltip when
the pointer leaves the window.

| System | Download | Install |
|---|---|---|
| Haiku R1/beta6, Renku OS (x86_64) | [gobeproductive-0.1.1-1-x86_64.hpkg](https://github.com/idumych/gobe-productive/releases/download/v0.1.1/gobeproductive-0.1.1-1-x86_64.hpkg) | `pkgman install ./gobeproductive-0.1.1-1-x86_64.hpkg` |
| Linux, glibc 2.39+ (x86_64) | [GobeProductive-0.1.1-x86_64.AppImage](https://github.com/idumych/gobe-productive/releases/download/v0.1.1/GobeProductive-0.1.1-x86_64.AppImage) | `chmod +x GobeProductive-0.1.1-x86_64.AppImage`, then run it |
| Linux, glibc 2.39+ (aarch64) | [GobeProductive-0.1.1-aarch64.AppImage](https://github.com/idumych/gobe-productive/releases/download/v0.1.1/GobeProductive-0.1.1-aarch64.AppImage) | `chmod +x GobeProductive-0.1.1-aarch64.AppImage`, then run it |

Checksums and release notes are on the
[release page](https://github.com/idumych/gobe-productive/releases/tag/v0.1.1).
On Haiku it appears in the Deskbar's Applications menu.
The Command key is **Alt** on Haiku and **Ctrl** on Linux.

Documents lay out best with the Liberation fonts (metrically like the
Times, Helvetica and Courier designs of BeOS's Bitstream fonts): on Haiku
`pkgman install liberation_fonts`; the AppImage brings them along.
Without them the port uses Noto, which is wider, so lines break in other
places than they did in Gobe.

### Where 0.1.1 was tested

| System | What was tested |
|---|---|
| Haiku R1/beta6 (x86_64) | Installed the package, unpacked the original from its installer, opened a document, typed, saved, reopened, Edit Styles |
| Linux x86_64, X11 | The same, with the AppImage (Xvfb and Openbox), and a FormatBar tooltip closing when the pointer leaves the window. Choosing a CD image in the file panel was tried during development, not on a release build |
| Linux, Wayland | Started on headless sway (x86_64 and aarch64): the window appears |
| Linux aarch64, X11 | Started on an arm64 machine: the window appears |
| Renku OS | Not tried; the Haiku package should install there |

Also untested so far: real hardware, interactive Wayland desktops, real
Gobe Productive CD images (only images made from the installer), and
opening documents saved by the port in the original on BeOS.

## Screenshots

On Haiku, with the History floater:

![Gobe Productive on Haiku: a document and the History floater](docs/screenshots/haiku-history.png)

On Linux (X11), with Edit Styles:

![Gobe Productive on Linux: the Edit Styles floater](docs/screenshots/linux-edit-styles.png)

## Status

The 0.1.1 preview is about the word processor; spreadsheets in it are
shown and their cells can be edited, with formulas evaluated. Everything
listed under [Since 0.1.1](#since-011-spreadsheets) is in the sources but
not released yet: build it to try it.

**Works in 0.1.1**

- Opening and saving Gobe Productive documents. Only what was edited is
  rewritten; everything else is kept byte for byte. File > New (word
  processing documents), Open Recent, Revert, Insert… (text and RTF), Page
  Setup, Print, Help (the original's User's Guide), About.
- Text: typing, selection (double, triple and quadruple clicks; the arrow
  keys with Command, Option and Control; Home/End, Page Up/Down), cut, copy
  and paste with styles, undo and redo, Find/Change, the context menu,
  fonts, sizes, faces, colours, alignment, Paragraph Format, tabs, line and
  paragraph spacing, Copy/Paste Format, Show/Hide Invisibles; pages that
  come and go with the text; margins, headers and footers; columns; page
  numbers, dates, times and formulas as live fields; footnotes and endnotes;
  separator lines and line, frame and page breaks; Word Count; the text
  ruler; the FormatBar and the PartBar; zoom; Page View off (the browse
  view); spell checking with the original's dictionaries; Preferences;
  History; Document Size; styles: applying paragraph and basic styles, New
  Style, Edit Styles (Modify, Copy, Cut, Paste and Clear Style, and
  formatting a style directly).
- Plain text and RTF: opened as documents, saved with Save As's File Format.
- SYLK: a spreadsheet saved as SYLK with Save As's File Format (values as
  calculated, formulas in R1C1, column widths). Opening SYLK files waits
  for File > New for spreadsheets.
- Spreadsheets: shown, cells edited (values and formulas, evaluated), cut,
  copy and paste, Fill Down and Fill Right, the selected rows inserted and
  deleted.
- Viewing: drawings, charts (2D), images, tables, clocks and presentations.

### Since 0.1.1: spreadsheets

With cells selected, the original's Format and Options menus appear, and
these work as they did in Gobe Productive, with its dialogs and alerts:

- **Formats:** Number… (the original's number format categories), Lock
  Cells and Unlock Cells (locked cells refuse changes), Column Width…, Row
  Height… (or drag a heading's edge), Display Options… (grid, locked cells shaded, formulas instead of
  their results, column and row headings, circular references marked).
- **Filling:** Fill Down, Right, Up and Left.
- **Moving cells:** Move Cells… and the context menu's Move Cells Here;
  formulas, and named ranges, follow the cells they refer to.
- **Inserting and deleting:** Insert Cells… and Delete Cells… shift cells
  down or right, up or left; whole rows and columns, selected by clicking
  their headings, go in and out, the sheet growing and shrinking with them.
  References to deleted cells become #REF!.
- **Sorting:** Sort… by up to three keys, each ascending or descending, in
  the original's order (numbers, text, true/false, errors, empty cells).
- **Copy and paste:** cells copied and pasted as cells, formulas
  referring to the cells around where they land (absolute references stay)
  and formats coming along; Paste Special… (values and formulas or values
  only, with or without the formatting).
- **Calculating:** Calculate Now, Auto Calculate, Go To Cell….
- **The EntryBar:** the active cell's contents under the PartBar; typing
  on a cell edits there, Enter (or Accept) puts it in and moves down, Tab
  moves right, Escape (or Cancel) leaves the cell as it was; its function
  button opens Paste Function (the original's functions by category, with
  their help) to put a function in. A double click still edits in the cell.
- **Named selections:** Name Selection… in the FormatBar's Names menu
  names the selected cells (refusing a name that's taken or isn't valid);
  the name then appears in that menu to select them again, works in
  formulas (`=SUM(Rates)`) and is saved with the document. Edit Names…
  shows each name's cells and renames or deletes them (undoably).
- **The FormatBar for cells:** font, size, faces, text color and fill
  color for the selected cells, the bar showing the active cell's.
- **The PartBar for cells:** the selection's address (a double click goes
  to a cell), the horizontal and vertical alignment menus with Wrap Text,
  cell borders (the outline or one side, a color and a transparency),
  Number Formatting. Make Chart is shown but off.
- **Selecting:** whole rows and columns by their headings (shift-click or
  drag for more), the corner for every cell in use; several ranges at once
  with Command-click (Alt on Haiku), which formats, locks, fills, clears,
  moves and sorts act on together; Select Dependents (the formulas that
  depend on the selection); the cell context menu.
- The edits can be undone (display options and Auto Calculate are
  settings, not edits, as in the original), and the sheet's locks, sizes,
  headings and options are saved with the document.

Tested on Haiku R1/beta6 in a virtual machine, with the original's sample
documents.

**Not yet**

The ToolBar; pattern and gradient inks, fill, pen and transparency in the
FormatBar; named selections; Import and Export Styles; New Window; Show
Clipboard; creating and linking frames; drawing tools; in spreadsheets
charts made from cells; new documents of other kinds; the Word, Excel,
HTML and SYLK translators; the thesaurus and dictionary (they need data
files that weren't part of Gobe Productive). Documents saved by the port
open in the port; they haven't been tried in the original on BeOS.

## Building

The same sources build on both systems; the Makefile finds out which
one it's on.

- **Haiku:** `pkgman install gcc make haiku_devel zlib_devel`, then `make`.
- **Linux:** build [Hokku](https://github.com/idumych/hokku) (the Haiku API
  as a library, a fork of Cosmoe), then `make COSMOE_ROOT=<its prefix>`.

Run `./GobeProductive [document…]`. `make test` runs the host-side tests of
the portable core (`src/core`, no Haiku API) on any system; with the
original's files at `../original/installed` they also run on its sample
documents. The packages are built by `packaging/` (`build-hpkg.sh` on
Haiku; `appimage.yml` in GitHub Actions).

| Path | Contents |
|---|---|
| `src/core/` | Portable C++17, no Haiku API: reading and writing documents, editing, styles, spreadsheet formulas, fields, spelling, text and RTF, the original's resources, unpacking the original (`OriginalImport`). |
| `src/app/` | The interface on the Haiku API: windows, menus and dialogs from the original's resources, the page view, the ribbons, the floaters. |
| `tests/` | Host-side tests. |

Test options (used by the test scripts): `--menu <index>`, `--dialog <id>`,
`--render out.png`, `--slide <n>`, `--do '<script>'` (see
`TPageView::RunScript`); `GOBE_DEBUG_MENUS=1` adds a menu of every dialog.

## Licence

MIT, with a notice: see [LICENSE](LICENSE). "Gobe Productive" is a trademark
of its owners.
