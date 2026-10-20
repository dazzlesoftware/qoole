FreeType 2.14.3
https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz
(mirror: https://github.com/freetype/freetype, tag VER-2-14-3)

Used under the FreeType License (FTL); see LICENSE.TXT and docs/FTL.TXT.
(FreeType is dual-licensed FTL / GPLv2, docs/GPLv2.TXT; Genesis uses the FTL.)

Only what the TrueType/OpenType build needs is vendored: include/ (public
headers) and the src/ modules base, sfnt, truetype, cff, psaux, pshinter,
psnames, smooth and autofit. Build files, docs and tests were dropped.

Configuration is edited in place, so consumers only need include/ on their
include path (no extra defines):
  include/freetype/config/ftmodule.h  - replaced with the reduced module list
                                         (autofit, truetype, cff, psaux,
                                         psnames, pshinter, sfnt, smooth).
  include/freetype/config/ftoption.h  - FT_CONFIG_OPTION_USE_LZW,
                                         FT_CONFIG_OPTION_USE_ZLIB and
                                         FT_CONFIG_OPTION_SVG commented out
                                         (marked "Genesis: disabled").
                                         bzip2, png, brotli and harfbuzz are
                                         off by upstream default.
When updating, re-apply these two changes and keep ftmodule.h in sync with
the ClCompile list in FreeType.vcxproj.

Built as the static library FreeType.lib (FreeType.vcxproj, FT2_BUILD_LIBRARY).
