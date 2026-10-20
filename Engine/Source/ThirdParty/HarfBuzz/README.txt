HarfBuzz 14.6.0
https://github.com/harfbuzz/harfbuzz/releases/download/14.6.0/harfbuzz-14.6.0.tar.xz

Licensed under the "Old MIT" license; see COPYING.

Built as the static library HarfBuzz.lib from the amalgamated src/harfbuzz.cc
with HAVE_FREETYPE defined, so the hb-ft.h integration works against the
vendored FreeType (../FreeType/include). No other HAVE_* backend (ICU, GLib,
CoreText, DirectWrite, GDI, Uniscribe, Graphite2, WASM, Cairo) is enabled.
The subset, raster and vector libraries are not built.

Only the sources harfbuzz.cc actually includes in this configuration are
vendored (plus the public headers); tests, utilities, docs and build files
were dropped.
