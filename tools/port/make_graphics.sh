#!/bin/sh
# Generates the converted graphics (.4bpp, .pmdpal, ...) that the game
# sources INCBIN, with the decomp's converter (tools/gbagfx). The port only
# needs their sizes and hashes; the files themselves never enter the binary.
# Run from the repository root.
set -e
[ -x tools/gbagfx/gbagfx ] || make -C tools/gbagfx > /dev/null
[ -x tools/preproc/preproc ] || make -C tools/preproc > /dev/null
n=0
for f in $( (cat $(find src -name '*.c' -o -name '*.h') | tr '\n' ' ' | grep -oE 'INCBIN_[A-Z0-9]+\(("[^"]+"\s*,?\s*)+\)'; \
             grep -ohE 'incbin\s+"[^"]+"' data/*.s) | grep -oE '"[^"]+"' | tr -d '"' | grep -v baserom.gba | sort -u); do
    [ -f "$f" ] && continue
    case "$f" in
        *.4bpp) tools/gbagfx/gbagfx "${f%.4bpp}.png" "$f" ;;
        *.8bpp) tools/gbagfx/gbagfx "${f%.8bpp}.png" "$f" ;;
        *.pmdpal) tools/gbagfx/gbagfx "${f%.pmdpal}.pal" "$f" ;;
        *.gbapal) tools/gbagfx/gbagfx "${f%.gbapal}.pal" "$f" ;;
        *) make "$f" > /dev/null ;;
    esac
    n=$((n + 1))
done
[ "$n" -gt 0 ] && echo "make_graphics: generated $n files" || true
