# Test plan: GobeProductive

## Fixtures

The original Gobe Productive 2.0 installer, from the tester's own copy
(never from the release, which doesn't contain it): the file
`Install Gobe Productive` (10,366,125 bytes). Copy it to the guest:

- Haiku: `scp "<installer>" haiku-b6-test:"/boot/home/Install Gobe Productive"`
- V\OS: `scp "<installer>" vos-test:"/home/vos-live/Install Gobe Productive"`
- Linux (Xvfb): copy it into the throwaway HOME as `$HOME_DIR/Install Gobe Productive`

The first start unpacks it (OriginalImport) into the settings folder, and
the feature checks use the sample document `Samples/Resume` from it.

## Fresh state

Delete before launch:

- Haiku, V\OS: `~/config/settings/Gobe Productive port`,
  `~/config/settings/Gobe Productive port settings`,
  `~/config/settings/Gobe Productive Recent Documents`
- Linux: the throwaway HOME (`$HOME_DIR`)

Launch the first time with `GOBE_ORIGINAL="$HOME/Install Gobe Productive"`
(the installer, so the import runs); no window should complain about the
original. Then:

    O="<settings>/Gobe Productive port/Original/Gobe Productive 2.0b2/Gobe Productive 2.0"
    cp "$O/Samples/Resume" "$HOME/resume-{p}"

(`<settings>`: `~/config/settings` on Haiku and V\OS, `$HOME_DIR/.config/cosmoe` on Linux.)

## Feature checks

### open-document
Steps: launch the app with `"$HOME/resume-{p}"` as its argument (and
GOBE_ORIGINAL as above the first time). Take a screenshot.
PASS when: OCR of the window (`{p}-open.txt`) shows the resume's text:
"Ernie", "Objective" or "Qualifications".

### type-save-reopen
Steps: click at the start of the word "Objective" in the document; type
`hello from {p} `; save with Command+S (Alt+S on Haiku, Ctrl+S on V\OS and
Linux); quit with Command+Q. Launch the app again with
`"$HOME/resume-{p}"` (no GOBE_ORIGINAL: the unpacked original is
remembered) and take a screenshot.
PASS when: OCR of the reopened window (`{p}-reopen.txt`) shows
"hello from {p}".

### edit-styles
Steps: in the reopened document, click the FormatBar's style pop-up (the
field showing "Body+" or "Body"), then its item "Edit Styles…". Take a
screenshot.
PASS when: OCR (`{p}-styles.txt`) shows the floater's "Available Styles".
