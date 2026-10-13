#!/bin/sh
# Erzeugt core/font_*.h aus tools/fonts (Nunito, Lilita One; beide SIL Open Font License 1.1).
set -e
cd "$(dirname "$0")/.."
[ tools/mkfont -nt tools/mkfont.swift ] || swiftc -O tools/mkfont.swift -o tools/mkfont
N=tools/fonts/Nunito-Regular-latin.woff2
L=tools/fonts/LilitaOne-Regular-latin.woff2
tools/mkfont $N 18 0.7 small ascii core/font_small.h
tools/mkfont $N 23 0.9 body ascii core/font_body.h
tools/mkfont $L 34 0 title ascii core/font_title.h
tools/mkfont $L 72 0 big "0123456789%:×·-+?! " core/font_big.h
