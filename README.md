# Gobe Productive, ported

Gobe Productive 2.0.1, the office suite for BeOS, rebuilt to run on today's
systems: **Haiku** (and Renku OS), **VitruvianOS** (a BeOS-like OS based on
Linux) and **Linux** (X11 and Wayland). It opens and saves Gobe Productive
documents and works the way the original did, down to its menus and dialogs.

This is a **preview** focused on the word processor. See [Status](#status).

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
| Haiku, Renku OS, VitruvianOS | `~/config/settings/Gobe Productive port/Original` |
| Linux | `~/.config/cosmoe/Gobe Productive port/Original` |

It also looks on its own in the usual places (`~/Gobe Productive 2.0`,
`~/config/non-packaged/apps/Gobe Productive 2.0`, `/boot/apps/…`, next to
the app), and you can name the image, installer or program in the
`GOBE_ORIGINAL` environment variable.

BeOS-format (BFS) CD images can't be read yet: copy the installer or the
folder out of the image first (Haiku can mount it), and choose that.

## Install

| System | Package | Install |
|---|---|---|
| Haiku R1/beta6, Renku OS (x86_64) | `gobeproductive-<version>-1-x86_64.hpkg` | `pkgman install ./gobeproductive-<version>-1-x86_64.hpkg` |
| VitruvianOS 0.6 (amd64, arm64) | `gobeproductive_<version>-1_amd64.deb` (or `_arm64`) | `sudo apt install ./gobeproductive_<version>-1_amd64.deb` |
| Linux, glibc 2.39+ (x86_64, aarch64) | `GobeProductive-<version>-x86_64.AppImage` (or `-aarch64`) | `chmod +x GobeProductive-<version>-x86_64.AppImage`, then run it |

Downloads are on the [releases page](https://github.com/idumych/gobe-productive/releases).
On Haiku and VitruvianOS it appears in the Deskbar's Applications menu.
The Command key is **Alt** on Haiku and **Ctrl** on VitruvianOS and Linux.

Documents lay out best with the Liberation fonts (metrically like the
Times, Helvetica and Courier designs of BeOS's Bitstream fonts): on Haiku
`pkgman install liberation_fonts`; the AppImage brings them along.

## Screenshots

(added with the release)

## Status

This preview is about the word processor.

**Works**

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
- Viewing: drawings, spreadsheets (formulas evaluated, cells editable),
  charts (2D), images, tables, clocks and presentations.

**Not yet**

The ToolBar; pattern and gradient inks, fill, pen and transparency in the
FormatBar; named selections; Import and Export Styles; New Window; Show
Clipboard; creating and linking frames; drawing tools; most spreadsheet
menus; new documents of other kinds; the Word, Excel, HTML and SYLK
translators; the thesaurus and dictionary (they need data files that weren't part of
Gobe Productive). Documents saved by the port open in the port; they
haven't been tried in the original on BeOS.

## Building

The same sources build on all three systems; the Makefile finds out which
one it's on.

- **Haiku:** `pkgman install gcc make haiku_devel zlib_devel`, then `make`.
- **VitruvianOS:** with its headers (`/system/develop/headers`) or
  `pkg-config vos`: `make`.
- **Linux:** build [Hokku](https://github.com/idumych/hokku) (the Haiku API
  as a library, a fork of Cosmoe), then `make COSMOE_ROOT=<its prefix>`.

Run `./GobeProductive [document…]`. `make test` runs the host-side tests of
the portable core (`src/core`, no Haiku API) on any system; with the
original's files at `../original/installed` they also run on its sample
documents. The packages are built by `packaging/` (`build-hpkg.sh` on
Haiku; `deb.yml` and `appimage.yml` in GitHub Actions).

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
