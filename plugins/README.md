# Image plug-ins

The image part's Plugins menu runs these, as the original Gobe Productive
did. They are separate programs and add-ons, under their own licences:

- `libimagemanip/`: Edmund Vermeulen's image manipulation library, release
  1.0.0 (public domain; LibImageManip.zip from the BeOS archives), with
  small changes for 64-bit systems (an add-on id is as wide as a pointer).
- `ImageManip/`: the ImageManip add-ons Gobe shipped, from the source it
  published with Gobe Productive (`src.zip`, see `README.src`): GIMP filters
  ported to libimagemanip, under the GNU General Public License (`COPYING`).
  That source left out each add-on's `Config.h`; the port rebuilt them from
  how the sources use them and from the shipped add-ons' layouts. `long`
  became `int32` (BeOS's `long` was 32 bits).
- `GIMP/`: the GIMP 0.54 filters Gobe shipped and their `gimp.c` library,
  under the GNU General Public License (`COPYING`).
- `compat/`: `libprefs` (the settings library the add-ons used) and the
  old headers they need, written for the port (public domain).

`make` on Haiku, `make COSMOE_ROOT=<Hokku prefix>` on Linux; the results are
in `build/`.
